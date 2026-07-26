/*
 * main.c — TrailCurrent Milepost firmware for the Waveshare
 * ESP32-S3-Touch-LCD-7B.
 *
 * Structure:
 *
 *   [1] Hardware bring-up. These sections embed hard-won fixes for the
 *       long-sleep wake bug and MUST NOT be touched without re-testing
 *       that path:
 *         - CH32V003 IO extender (I2C 0x24)
 *         - Backlight (PWM via reg 0x05 + IO2 enable, with 50 ms settle)
 *         - ST7701 RGB panel (double-buffered, vsync-synced, PSRAM FBs)
 *         - GT911 touch (0x5D/0x14 probe, INT-pin address select)
 *         - LVGL flush + touch drivers (bounded vsync wait, coord clamp)
 *         - TWAI (CAN) driver install on core 1, IRAM-safe ISR, 0x00-0x3F
 *           acceptance filter, bus recovery
 *         - Wake ceremony: hard panel recovery, GT911 reset on long idles
 *
 *   [2] CAN receive dispatcher (handle_can_frame) — parses standard-frame
 *       IDs 0x00–0x3E and updates volatile state flags. update_ui_from_can
 *       (below) drains those flags on the main loop and calls the
 *       set_var_* API in vars.c to paint the UI.
 *
 *   [3] Bus-services CAN triggers: OTA (0x00), WiFi provisioning (0x01),
 *       Discovery (0x02) — forwarded to ota.c / wifi_config.c /
 *       discovery.c.
 *
 *   [4] app_main — bring up hardware, LVGL, ui_init(), all UI init
 *       helpers (init_metric_charts, init_notif_icon_ack_taps, etc.),
 *       then CAN + main loop.
 */

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/twai.h"
#include "esp_intr_alloc.h"
#include "can_common.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

#include "ui/ui.h"
#include "include/ota.h"
#include "include/discovery.h"
#include "include/wifi_config.h"
#include "ui/vars.h"
#include "ui/styles.h"
#include "button_config.h"

#include "milepost_vars.h"
#include "app_state.h"
#include "milepost_config.h"
#include "alarms.h"

static const char *TAG = "milepost";

/* Display resolution (Waveshare 7B panel) */
#define SCREEN_WIDTH  1024
#define SCREEN_HEIGHT 600

/* ==========================================================================
 * lvgl_port_lock/unlock — recursive mutex serializing LVGL widget access
 * between the main loop (running lv_timer_handler) and background tasks
 * that also touch widgets (WiFi event callback in app_state.c, OTA HTTP
 * task in ota.c). Header lives at include/esp_lvgl_port.h so shared
 * source with that BSP dependency compiles without modification.
 * ========================================================================== */
static SemaphoreHandle_t s_lvgl_lock = NULL;

bool lvgl_port_lock(uint32_t timeout_ms)
{
    if (!s_lvgl_lock) return true;   /* pre-init — main hasn't started LVGL yet */
    TickType_t t = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_lock, t) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    if (!s_lvgl_lock) return;
    xSemaphoreGiveRecursive(s_lvgl_lock);
}

/* Compatibility shim so shared vars.c can call set_lcd_blight() without
 * pulling in a BSP. Forwarded to apply_brightness below. */
static void set_backlight(uint8_t brightness);
esp_err_t set_lcd_blight(uint32_t brightness_pct)
{
    if (brightness_pct > 100) brightness_pct = 100;
    /* Settings slider stores 0..100 %; PWM stage below wants 0..255. Scale. */
    set_backlight((uint8_t)(brightness_pct * 255 / 100));
    return ESP_OK;
}

/* ==========================================================================
 * [1] Hardware — CH32V003 IO extender (I2C 0x24)
 *
 * Register-based protocol:
 *   0x02 = mode control (write 0xFF to set all pins as output)
 *   0x03 = IO output bitfield
 *   0x05 = PWM output (backlight brightness, inverted: 0=bright, 247=dimmest)
 * Pin mapping in the IO byte:
 *   Bit 1 = IO1 = Touch RST
 *   Bit 2 = IO2 = Backlight enable
 *   Bit 3 = IO3 = LCD RST
 *   Bit 5 = IO5 = CAN_SEL (high=CAN, low=USB)
 * ========================================================================== */
#define IO_EXT_ADDR          0x24
#define IO_EXT_REG_MODE      0x02
#define IO_EXT_REG_OUTPUT    0x03
#define IO_EXT_REG_PWM       0x05

#define IO_EXT_IO1_BIT  (1 << 1)
#define IO_EXT_IO2_BIT  (1 << 2)
#define IO_EXT_IO3_BIT  (1 << 3)
#define IO_EXT_IO5_BIT  (1 << 5)

#define I2C_SDA_PIN  8
#define I2C_SCL_PIN  9
#define I2C_FREQ_HZ  400000

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t io_ext_dev = NULL;
static uint8_t io_ext_out = 0;

static esp_err_t io_ext_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    esp_err_t err = i2c_master_transmit(io_ext_dev, buf, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[wakediag] CH32V003 I2C write FAILED reg=0x%02X val=0x%02X err=%s",
                 reg, val, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t io_ext_set_bit(uint8_t bit, bool high)
{
    if (high) io_ext_out |= bit;
    else      io_ext_out &= ~bit;
    return io_ext_write_reg(IO_EXT_REG_OUTPUT, io_ext_out);
}

static void io_ext_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    i2c_device_config_t io_ext_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = IO_EXT_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &io_ext_cfg, &io_ext_dev));

    esp_err_t err = io_ext_write_reg(IO_EXT_REG_MODE, 0xFF);
    ESP_LOGI(TAG, "IO extension init %s", err == ESP_OK ? "OK" : "FAILED");

    io_ext_out = 0xFF;
    io_ext_write_reg(IO_EXT_REG_OUTPUT, io_ext_out);
}

/* ==========================================================================
 * [1] Hardware — Backlight
 *
 * apply_brightness() has a hard-won 50 ms settle delay on IO2 low→high
 * transitions because the CH32V003 firmware drops its PWM value across
 * that edge. Do NOT remove.
 * ========================================================================== */
#define BRIGHTNESS_MIN_USER  32

static uint8_t desired_brightness = 255;
/* Visible to other TUs so they can gate their own widget-writing paths on
 * !screen_timed_out. The user requirement is ZERO gauge refresh while dim;
 * on wake, the next CAN frame's setter call repopulates. */
bool           screen_timed_out   = false;
static int64_t s_last_wake_us = 0;

int64_t screen_wake_age_us(void)
{
    return esp_timer_get_time() - s_last_wake_us;
}

static void apply_brightness(uint8_t brightness)
{
    bool io2_was_low = !(io_ext_out & IO_EXT_IO2_BIT);
    ESP_LOGI(TAG, "[wakediag] apply_brightness(%u) io2_was_low=%d io_ext_out=0x%02X",
             brightness, io2_was_low, io_ext_out);

    esp_err_t io2_err = io_ext_set_bit(IO_EXT_IO2_BIT, true);
    ESP_LOGI(TAG, "[wakediag]  IO2 high write -> %s", esp_err_to_name(io2_err));

    if (io2_was_low) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    uint8_t pwm_val = (brightness >= 255) ? 0 : (uint8_t)(255 - brightness);
    if (pwm_val > 247) pwm_val = 247;
    esp_err_t pwm_err = io_ext_write_reg(IO_EXT_REG_PWM, pwm_val);
    ESP_LOGI(TAG, "[wakediag]  PWM write reg=0x05 val=%u -> %s",
             pwm_val, esp_err_to_name(pwm_err));
}

static void backlight_off(void)
{
    esp_err_t err = io_ext_set_bit(IO_EXT_IO2_BIT, false);
    ESP_LOGI(TAG, "[wakediag] backlight_off IO2 low -> %s io_ext_out=0x%02X",
             esp_err_to_name(err), io_ext_out);
}

static void set_backlight(uint8_t brightness)
{
    if (brightness < BRIGHTNESS_MIN_USER) brightness = BRIGHTNESS_MIN_USER;
    desired_brightness = brightness;
    if (!screen_timed_out) apply_brightness(brightness);
}

/* ==========================================================================
 * [1] Hardware — ST7701 RGB panel (double-buffered, vsync-synced)
 * ========================================================================== */
static esp_lcd_panel_handle_t panel_handle = NULL;

/* Two semaphores now:
 *   - vsync_sem: kept for wake ceremony's xQueueReset drain (invalidates any
 *     stale wait state after a hard recovery). No longer gates the flush.
 *   - swap_done_sem: given from on_frame_buf_complete, which fires only when
 *     a newly-submitted framebuffer has actually latched into scan-out.
 *     There are no stale tokens by construction, so waiting on it is real.
 *
 * Prior to Fix 1 the flush gated on vsync_sem, which the on_vsync ISR gave
 * unconditionally every vsync (~30 ms). A stale token was almost always
 * pending when lvgl_flush_cb ran, so the "wait for swap to latch" returned
 * instantly and LVGL began rendering into the framebuffer the RGB peripheral
 * was still scanning out — the classic tearing on large dirty regions. */
static SemaphoreHandle_t vsync_sem = NULL;
static SemaphoreHandle_t swap_done_sem = NULL;

/* Free-running vsync tally + flush-timeout counters. Used by wakediag +
 * hard-recovery threshold. */
static volatile uint32_t s_vsync_count = 0;
static volatile uint32_t s_fb_complete_count = 0;
static volatile uint32_t s_flush_count = 0;
static volatile uint32_t s_flush_take_timeout_us_max = 0;
static volatile uint32_t s_flush_timeout_count = 0;
#define HARD_RECOVERY_THRESHOLD 30
static volatile uint32_t s_consecutive_flush_timeouts = 0;

static volatile bool s_wake_recovery_pending       = false;
static volatile bool s_panel_hard_recovery_pending = false;
static int64_t s_dim_start_us = 0;

static IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t panel,
                                const esp_lcd_rgb_panel_event_data_t *edata,
                                void *user_ctx)
{
    (void)panel; (void)edata; (void)user_ctx;
    s_vsync_count++;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(vsync_sem, &woken);
    return woken == pdTRUE;
}

/* Fires when a framebuffer previously submitted via esp_lcd_panel_draw_bitmap
 * has finished loading into scan-out. Gating flush on this eliminates the
 * stale-token race described above. */
static IRAM_ATTR bool on_fb_complete(esp_lcd_panel_handle_t panel,
                                     const esp_lcd_rgb_panel_event_data_t *edata,
                                     void *user_ctx)
{
    (void)panel; (void)edata; (void)user_ctx;
    s_fb_complete_count++;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(swap_done_sem, &woken);
    return woken == pdTRUE;
}

static void lcd_reset(void)
{
    io_ext_set_bit(IO_EXT_IO3_BIT, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    io_ext_set_bit(IO_EXT_IO3_BIT, true);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void lcd_init(void)
{
    lcd_reset();
    vsync_sem = xSemaphoreCreateBinary();
    swap_done_sem = xSemaphoreCreateBinary();

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            /* Reduced from vendor's 30.85 MHz to 20 MHz. 25 MHz reduced
             * the whole-screen shift tearing but didn't eliminate it —
             * shift was still visible during widget-update bursts
             * (arcs / labels updating). 20 MHz widens the DMA time
             * budget by ~35% vs vendor. Cost: refresh 33.6 Hz → 21.8 Hz.
             * For a mostly-static gauge UI this is imperceptible. */
            .pclk_hz = 20000000,
            .h_res = SCREEN_WIDTH,
            .v_res = SCREEN_HEIGHT,
            .hsync_pulse_width = 162,
            .hsync_back_porch = 152,
            .hsync_front_porch = 48,
            .vsync_pulse_width = 45,
            .vsync_back_porch = 13,
            .vsync_front_porch = 3,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = SCREEN_WIDTH * 10,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .de_gpio_num = 5,
        .pclk_gpio_num = 7,
        .vsync_gpio_num = 3,
        .hsync_gpio_num = 46,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            /* B[0:4]  */ 14, 38, 18, 17, 10,
            /* G[0:5]  */ 39, 0,  45, 48, 47, 21,
            /* R[0:4]  */ 1,  2,  42, 41, 40,
        },
        .flags.fb_in_psram = true,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = on_vsync,
        .on_frame_buf_complete = on_fb_complete,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL));

    ESP_LOGI(TAG, "RGB LCD initialized (double-buffered, vsync-synced)");
}

/* ==========================================================================
 * [1] Hardware — GT911 touch
 * ========================================================================== */
static esp_lcd_touch_handle_t touch_handle = NULL;

static bool i2c_probe(uint8_t addr)
{
    return i2c_master_probe(i2c_bus, addr, pdMS_TO_TICKS(50)) == ESP_OK;
}

static void touch_init(void)
{
    /* GT911 I2C address is selected by INT pin state at RST rising edge:
     *   INT low  → 0x5D (default)
     *   INT high → 0x14 (backup) */
    gpio_config_t int_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(4),
    };
    gpio_config(&int_cfg);

    io_ext_set_bit(IO_EXT_IO1_BIT, false);    /* RST low */
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(4, 0);                      /* INT low → selects 0x5D */
    vTaskDelay(pdMS_TO_TICKS(100));
    io_ext_set_bit(IO_EXT_IO1_BIT, true);      /* RST high (INT still low) */
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_direction(4, GPIO_MODE_INPUT);    /* Release INT for interrupt use */

    uint8_t gt911_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    if (!i2c_probe(0x5D)) {
        if (i2c_probe(0x14)) {
            gt911_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        } else {
            ESP_LOGE(TAG, "GT911 not found at 0x5D or 0x14 — touch disabled");
            return;
        }
    }

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.dev_addr = gt911_addr;
    tp_io_config.scl_speed_hz = I2C_FREQ_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = SCREEN_WIDTH,
        .y_max = SCREEN_HEIGHT,
        .rst_gpio_num = -1,
        .int_gpio_num = 4,
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle));
    ESP_LOGI(TAG, "GT911 touch initialized at 0x%02X", gt911_addr);

    /* One-shot health/config dump — read-only diagnostic. */
    uint8_t pid[4]      = {0};
    uint8_t cfg[5]      = {0};
    uint8_t fw_res[4]   = {0};
    esp_err_t r_pid = esp_lcd_panel_io_rx_param(tp_io_handle, 0x8140, pid,    sizeof(pid));
    esp_err_t r_cfg = esp_lcd_panel_io_rx_param(tp_io_handle, 0x8047, cfg,    sizeof(cfg));
    esp_err_t r_fwr = esp_lcd_panel_io_rx_param(tp_io_handle, 0x8146, fw_res, sizeof(fw_res));
    if (r_pid == ESP_OK) {
        ESP_LOGI(TAG, "GT911 PID='%c%c%c%c' (%02X %02X %02X %02X)",
                 (pid[0] >= 0x20 && pid[0] < 0x7F) ? pid[0] : '?',
                 (pid[1] >= 0x20 && pid[1] < 0x7F) ? pid[1] : '?',
                 (pid[2] >= 0x20 && pid[2] < 0x7F) ? pid[2] : '?',
                 (pid[3] >= 0x20 && pid[3] < 0x7F) ? pid[3] : '?',
                 pid[0], pid[1], pid[2], pid[3]);
    }
    if (r_cfg == ESP_OK && r_fwr == ESP_OK) {
        uint16_t x_max = (uint16_t)cfg[1]    | ((uint16_t)cfg[2]    << 8);
        uint16_t y_max = (uint16_t)cfg[3]    | ((uint16_t)cfg[4]    << 8);
        uint16_t fw_x  = (uint16_t)fw_res[0] | ((uint16_t)fw_res[1] << 8);
        uint16_t fw_y  = (uint16_t)fw_res[2] | ((uint16_t)fw_res[3] << 8);
        ESP_LOGI(TAG, "GT911 cfg_ver=0x%02X x_max=%u y_max=%u fw_x_res=%u fw_y_res=%u panel=%dx%d",
                 cfg[0], x_max, y_max, fw_x, fw_y, SCREEN_WIDTH, SCREEN_HEIGHT);
        if (x_max != SCREEN_WIDTH || y_max != SCREEN_HEIGHT) {
            ESP_LOGE(TAG, "GT911 output max %ux%u does NOT match panel %dx%d",
                     x_max, y_max, SCREEN_WIDTH, SCREEN_HEIGHT);
        }
    }
}

/* ==========================================================================
 * [1] Hardware — LVGL flush + touch drivers
 * ========================================================================== */
static void lvgl_tick_cb(void *arg) { (void)arg; lv_tick_inc(1); }

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    (void)area;
    if (lv_disp_flush_is_last(drv)) {
        /* Fix 1: DRAIN → DRAW → TAKE. Matches Spotter's flush_cb exactly
         * (TrailCurrentSpotter/main/main.c). Spotter is a working port
         * on the same S3 + LVGL + on_frame_buf_complete architecture and
         * uses this exact ordering; we were running draw→drain→take
         * ("corrected" from a race theory that wasn't validated by
         * evidence). Realigning to the empirically-working sequence. */
        s_flush_count++;
        xSemaphoreTake(swap_done_sem, 0);
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, color_map);
        int64_t take_start_us = esp_timer_get_time();
        BaseType_t got = xSemaphoreTake(swap_done_sem, pdMS_TO_TICKS(100));
        uint32_t took_us = (uint32_t)(esp_timer_get_time() - take_start_us);
        if (took_us > s_flush_take_timeout_us_max) s_flush_take_timeout_us_max = took_us;
        if (got != pdTRUE) {
            s_flush_timeout_count++;
            uint32_t consec = ++s_consecutive_flush_timeouts;
            if (consec >= HARD_RECOVERY_THRESHOLD && !s_panel_hard_recovery_pending) {
                s_panel_hard_recovery_pending = true;
                ESP_LOGE(TAG,
                    "[wakediag] flush pipeline STUCK (>=%d consecutive timeouts) — hard recovery requested",
                    HARD_RECOVERY_THRESHOLD);
            }
            static int64_t s_last_log_us = 0;
            int64_t now = esp_timer_get_time();
            if (now - s_last_log_us > 1000000) {
                s_last_log_us = now;
                ESP_LOGE(TAG,
                    "[wakediag] lvgl_flush_cb swap WAIT TIMEOUT (>100ms) count=%lu consec=%lu vsync=%lu",
                    (unsigned long)s_flush_timeout_count,
                    (unsigned long)consec,
                    (unsigned long)s_vsync_count);
            }
        } else {
            s_consecutive_flush_timeouts = 0;
        }
    }
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    if (!touch_handle) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    static int64_t s_last_err_log_us = 0;
    static uint32_t s_err_count = 0;
    esp_err_t rd_err = esp_lcd_touch_read_data(touch_handle);
    if (rd_err != ESP_OK) {
        s_err_count++;
        int64_t now = esp_timer_get_time();
        if (now - s_last_err_log_us > 1000000) {
            s_last_err_log_us = now;
            ESP_LOGW(TAG, "[wakediag] GT911 read_data err=%s (count=%lu since last log)",
                     esp_err_to_name(rd_err), (unsigned long)s_err_count);
            s_err_count = 0;
        }
    }

    esp_lcd_touch_point_data_t pt;
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(touch_handle, &pt, &count, 1) == ESP_OK && count > 0) {
        /* Clamp to panel bounds — LVGL 8 silently drops out-of-range coords. */
        uint16_t raw_x = pt.x, raw_y = pt.y;
        bool clamped = false;
        if (pt.x >= SCREEN_WIDTH)  { pt.x = SCREEN_WIDTH  - 1; clamped = true; }
        if (pt.y >= SCREEN_HEIGHT) { pt.y = SCREEN_HEIGHT - 1; clamped = true; }
        if (clamped) {
            static int64_t s_last_clamp_log_us = 0;
            int64_t now = esp_timer_get_time();
            if (now - s_last_clamp_log_us > 5000000) {
                s_last_clamp_log_us = now;
                ESP_LOGW(TAG, "GT911 coord out of range raw=(%u,%u) panel=%dx%d — clamped",
                         (unsigned)raw_x, (unsigned)raw_y, SCREEN_WIDTH, SCREEN_HEIGHT);
            }
        }
        data->state = LV_INDEV_STATE_PR;
        data->point.x = pt.x;
        data->point.y = pt.y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void lvgl_init(void)
{
    lv_init();

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));

    void *fb[2] = {NULL, NULL};
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &fb[0], &fb[1]));

    uint32_t buf_size = SCREEN_WIDTH * SCREEN_HEIGHT;
    lv_disp_draw_buf_init(&draw_buf, (lv_color_t *)fb[0], (lv_color_t *)fb[1], buf_size);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.direct_mode = 1;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "LVGL initialized (direct mode)");
}

/* ==========================================================================
 * [2] CAN receive dispatcher
 * ========================================================================== */
#define CAN_TX_PIN           20
#define CAN_RX_PIN           19

#define CAN_ID_OTA_TRIGGER        0x00
#define CAN_ID_WIFI_CONFIG        0x01
#define CAN_ID_DISCOVERY_TRIGGER  0x02
#define CAN_ID_DATETIME           0x06
#define CAN_ID_GPS_SAT_SPEED      0x07
#define CAN_ID_GPS_ALTITUDE       0x08
#define CAN_ID_GPS_LATLON         0x09
#define CAN_ID_TEMPERATURE        0x1F
#define CAN_ID_BATT_SHUNT1        0x23
#define CAN_ID_BATT_SHUNT2        0x24
#define CAN_ID_SOLAR_MPPT1        0x2C
/* Plateau leveling — broadcast every 500 ms. 0x37 carries the tilt +
 * height diffs we display; 0x38 carries per-corner adjustment mm (unused
 * on Milepost's leveling UI); 0x39 is IMU health / cal-quality (also
 * unused). See TrailCurrentPlateau/main/main.c:38-41. */
#define CAN_ID_PLATEAU_TILT       0x37
#define CAN_ID_WATER_TANK_LEVELS  0x3E

/* Button state flag — set when any mapped Torrent/Switchback status frame lands */
volatile bool g_device_status_updated = false;

/* Clock (CAN 0x06 from Bearing, UTC) */
static volatile uint16_t g_clock_year = 0;
static volatile uint8_t  g_clock_month = 0, g_clock_day = 0;
static volatile uint8_t  g_clock_hour = 0, g_clock_minute = 0, g_clock_second = 0;
static volatile bool     g_datetime_updated = false;

/* Temperature / humidity / AQ (CAN 0x1F from Borealis) */
static volatile uint8_t  g_interior_temp_f = 0;
static volatile int8_t   g_interior_temp_c = 0;
static volatile uint16_t g_humidity_raw = 0;
static volatile uint16_t g_tvoc_ppb = 0;
static volatile uint16_t g_eco2_ppm = 0;
static volatile bool     g_temperature_updated = false;

/* GPS (CAN 0x07/0x08/0x09) */
static volatile uint8_t  g_gps_num_sats = 0;
static volatile uint8_t  g_gps_gnss_mode = 0;
static volatile uint32_t g_gps_altitude_raw = 0;
static volatile float    g_gps_latitude = 0.0f;
static volatile float    g_gps_longitude = 0.0f;
static volatile bool     g_gps_sat_updated = false;
static volatile bool     g_gps_alt_updated = false;
static volatile bool     g_gps_latlon_updated = false;

/* Battery shunt (CAN 0x23/0x24) */
static volatile uint8_t  g_batt_voltage_whole = 0, g_batt_voltage_dec = 0;
static volatile uint8_t  g_batt_soc_whole = 0, g_batt_soc_dec = 0;
static volatile bool     g_batt_shunt1_updated = false;
static volatile uint8_t  g_is_wattage_negative = 0;
static volatile uint16_t g_shunt_wattage = 0;
static volatile uint16_t g_time_to_go_min = 0;
static volatile bool     g_batt_shunt2_updated = false;

/* Solar MPPT (CAN 0x2C) */
static volatile uint16_t g_solar_wattage = 0;
static volatile uint8_t  g_solar_charge_status = 0;
static volatile bool     g_solar_mppt1_updated = false;

/* Water tank levels (CAN 0x3E) */
static volatile uint8_t  g_fresh_water_level = 0, g_grey_water_level = 0, g_black_water_level = 0;
static volatile bool     g_water_levels_updated = false;

/* Plateau tilt (CAN 0x37) — pitch/roll are int16 × 100 (degrees × 100),
 * diffs are int16 mm. Stored as scaled integers to keep the volatiles
 * lock-free; conversion to float happens on the drain in
 * update_ui_from_can(). */
static volatile int16_t  g_tilt_pitch_x100 = 0;
static volatile int16_t  g_tilt_roll_x100  = 0;
static volatile int16_t  g_tilt_fb_diff_mm = 0;
static volatile int16_t  g_tilt_lr_diff_mm = 0;
static volatile bool     g_tilt_updated    = false;

static void handle_can_frame(const twai_message_t *msg)
{
    if (msg->rtr) return;

    ESP_LOGD(TAG, "CAN RX id=0x%03lX dlc=%u data=%02X %02X %02X %02X %02X %02X %02X %02X",
             (unsigned long)msg->identifier, (unsigned)msg->data_length_code,
             msg->data[0], msg->data[1], msg->data[2], msg->data[3],
             msg->data[4], msg->data[5], msg->data[6], msg->data[7]);

    switch (msg->identifier) {
    case CAN_ID_OTA_TRIGGER:
        ota_handle_trigger(msg->data, msg->data_length_code);
        return;
    case CAN_ID_WIFI_CONFIG:
        wifi_config_handle_can(msg->data, msg->data_length_code);
        return;
    case CAN_ID_DISCOVERY_TRIGGER:
        discovery_handle_trigger();
        return;
    }

    /* Torrent status: 8 bytes, one PWM value per channel (one message per instance) */
    for (int inst = 0; inst < 3; inst++) {
        if (msg->identifier == TORRENT_STATUS_ID[inst] && msg->data_length_code == 8) {
            for (int btn = 0; btn < NUM_BUTTONS; btn++) {
                if (g_buttons[btn].module_type == MOD_TORRENT &&
                    g_buttons[btn].instance    == inst) {
                    uint8_t ch = g_buttons[btn].channel;
                    if (ch < 8) g_button_state[btn] = msg->data[ch];
                }
            }
            g_device_status_updated = true;
            return;
        }
    }

    /* Switchback status: 1-byte bitfield (bit N = relay N state).
     * Drives the button tiles ONLY — this frame reports relay OUTPUT state,
     * not sensor inputs, so it must not feed the alarm subsystem. Alarms
     * key off SWITCHBACK_INPUT_ID (0x12-0x14) below. */
    for (int inst = 0; inst < 3; inst++) {
        if (msg->identifier == SWITCHBACK_STATUS_ID[inst] && msg->data_length_code >= 1) {
            uint8_t bits = msg->data[0];
            for (int btn = 0; btn < NUM_BUTTONS; btn++) {
                if (g_buttons[btn].module_type == MOD_SWITCHBACK &&
                    g_buttons[btn].instance    == inst) {
                    uint8_t ch = g_buttons[btn].channel;
                    if (ch < 8) g_button_state[btn] = (bits >> ch) & 1;
                }
            }
            g_device_status_updated = true;
            return;
        }
    }

    /* Switchback digital-input broadcast (Picket-variant firmware): 1-byte
     * bitfield where bit N reflects the debounced state of DI N. Feeds the
     * alarm evaluator so armed sensors surface in the topbar notification. */
    for (int inst = 0; inst < 3; inst++) {
        if (msg->identifier == SWITCHBACK_INPUT_ID[inst] && msg->data_length_code >= 1) {
            alarms_apply_inputs(ALARM_SRC_SWITCHBACK, (uint8_t)inst, msg->data[0]);
            return;
        }
    }

    if (msg->identifier == CAN_ID_DATETIME && msg->data_length_code >= 7) {
        g_clock_year   = ((uint16_t)msg->data[0] << 8) | msg->data[1];
        g_clock_month  = msg->data[2];
        g_clock_day    = msg->data[3];
        g_clock_hour   = msg->data[4];
        g_clock_minute = msg->data[5];
        g_clock_second = msg->data[6];
        g_datetime_updated = true;
        return;
    }

    if (msg->identifier == CAN_ID_TEMPERATURE && msg->data_length_code >= 4) {
        g_interior_temp_c = (int8_t)msg->data[0];
        g_interior_temp_f = msg->data[1];
        g_humidity_raw = ((uint16_t)msg->data[2] << 8) | msg->data[3];
        if (msg->data_length_code >= 8) {
            g_tvoc_ppb = ((uint16_t)msg->data[4] << 8) | msg->data[5];
            g_eco2_ppm = ((uint16_t)msg->data[6] << 8) | msg->data[7];
        }
        g_temperature_updated = true;
    } else if (msg->identifier == CAN_ID_GPS_SAT_SPEED && msg->data_length_code >= 6) {
        g_gps_num_sats  = msg->data[0];
        g_gps_gnss_mode = msg->data[5];
        g_gps_sat_updated = true;
    } else if (msg->identifier == CAN_ID_GPS_ALTITUDE && msg->data_length_code >= 4) {
        g_gps_altitude_raw = ((uint32_t)msg->data[0] << 24) |
                             ((uint32_t)msg->data[1] << 16) |
                             ((uint32_t)msg->data[2] << 8) |
                              (uint32_t)msg->data[3];
        g_gps_alt_updated = true;
    } else if (msg->identifier == CAN_ID_GPS_LATLON && msg->data_length_code >= 8) {
        /* Bearing 0x09: [lat_sign, lat2, lat1, lat0, lon_sign, lon2, lon1, lon0]
         * with abs = ((b1<<16)|(b2<<8)|b3) / 10000. */
        uint32_t lat_abs = ((uint32_t)msg->data[1] << 16) |
                           ((uint32_t)msg->data[2] << 8)  |
                            (uint32_t)msg->data[3];
        uint32_t lon_abs = ((uint32_t)msg->data[5] << 16) |
                           ((uint32_t)msg->data[6] << 8)  |
                            (uint32_t)msg->data[7];
        float lat = (float)lat_abs / 10000.0f;
        float lon = (float)lon_abs / 10000.0f;
        if (msg->data[0]) lat = -lat;
        if (msg->data[4]) lon = -lon;
        g_gps_latitude  = lat;
        g_gps_longitude = lon;
        g_gps_latlon_updated = true;
    } else if (msg->identifier == CAN_ID_BATT_SHUNT1 && msg->data_length_code >= 7) {
        g_batt_voltage_whole = msg->data[0];
        g_batt_voltage_dec   = msg->data[1];
        g_batt_soc_whole     = msg->data[5];
        g_batt_soc_dec       = msg->data[6];
        g_batt_shunt1_updated = true;
    } else if (msg->identifier == CAN_ID_BATT_SHUNT2 && msg->data_length_code >= 5) {
        g_is_wattage_negative = msg->data[0];
        g_shunt_wattage  = ((uint16_t)msg->data[1] << 8) | msg->data[2];
        g_time_to_go_min = ((uint16_t)msg->data[3] << 8) | msg->data[4];
        g_batt_shunt2_updated = true;
    } else if (msg->identifier == CAN_ID_SOLAR_MPPT1 && msg->data_length_code >= 7) {
        g_solar_wattage = ((uint16_t)msg->data[2] << 8) | msg->data[3];
        g_solar_charge_status = msg->data[6];
        g_solar_mppt1_updated = true;
    } else if (msg->identifier == CAN_ID_WATER_TANK_LEVELS && msg->data_length_code >= 3) {
        g_fresh_water_level = msg->data[0] > 100 ? 100 : msg->data[0];
        g_grey_water_level  = msg->data[1] > 100 ? 100 : msg->data[1];
        g_black_water_level = msg->data[2] > 100 ? 100 : msg->data[2];
        g_water_levels_updated = true;
    } else if (msg->identifier == CAN_ID_PLATEAU_TILT && msg->data_length_code >= 8) {
        /* Plateau 0x37: [pitch_hi, pitch_lo, roll_hi, roll_lo,
         *                fb_diff_hi, fb_diff_lo, lr_diff_hi, lr_diff_lo]
         * pitch/roll = int16 × 100 (degrees × 100), diffs = int16 mm. */
        g_tilt_pitch_x100 = (int16_t)(((uint16_t)msg->data[0] << 8) | msg->data[1]);
        g_tilt_roll_x100  = (int16_t)(((uint16_t)msg->data[2] << 8) | msg->data[3]);
        g_tilt_fb_diff_mm = (int16_t)(((uint16_t)msg->data[4] << 8) | msg->data[5]);
        g_tilt_lr_diff_mm = (int16_t)(((uint16_t)msg->data[6] << 8) | msg->data[7]);
        g_tilt_updated = true;
    }
}

/* ==========================================================================
 * [1] Hardware — TWAI (CAN) driver
 *
 * Driver install happens INSIDE can_rx_task so the TWAI ISR pins to core 1,
 * off core 0 where the RGB panel + GDMA bounce-buffer refill ISRs live.
 * ========================================================================== */
static twai_general_config_t s_twai_g_config;
static twai_timing_config_t  s_twai_t_config;
static twai_filter_config_t  s_twai_f_config;
static volatile bool         s_twai_installed = false;

static void can_rx_task(void *arg)
{
    (void)arg;
    esp_err_t r = twai_driver_install(&s_twai_g_config, &s_twai_t_config, &s_twai_f_config);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "twai_driver_install failed on core 1: %s", esp_err_to_name(r));
        vTaskDelete(NULL);
        return;
    }
    r = twai_start();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "twai_start failed on core 1: %s", esp_err_to_name(r));
        twai_driver_uninstall();
        vTaskDelete(NULL);
        return;
    }
    s_twai_installed = true;
    ESP_LOGI(TAG, "TWAI started on TX=%d RX=%d at 500kbps (ISR pinned to core %d)",
             CAN_TX_PIN, CAN_RX_PIN, xPortGetCoreID());

    twai_reconfigure_alerts(CAN_COMMON_ALERTS, NULL);
    can_common_version_broadcast();

    TickType_t last_status_log = xTaskGetTickCount();
    while (1) {
        uint32_t triggered = 0;
        twai_read_alerts(&triggered, pdMS_TO_TICKS(100));

        if (triggered & TWAI_ALERT_BUS_OFF) {
            ESP_LOGE(TAG, "TWAI bus-off, initiating recovery");
            twai_initiate_recovery();
        }
        if (triggered & TWAI_ALERT_BUS_RECOVERED) {
            ESP_LOGI(TAG, "TWAI bus recovered, restarting");
            twai_start();
        }
        if (triggered & TWAI_ALERT_ERR_PASS) {
            ESP_LOGW(TAG, "TWAI error passive (no peers ACKing?)");
        }
        if (triggered & TWAI_ALERT_ERR_ACTIVE) {
            ESP_LOGI(TAG, "TWAI error active (bus healthy)");
        }
        if (triggered & TWAI_ALERT_RX_QUEUE_FULL) {
            ESP_LOGW(TAG, "TWAI RX queue full");
        }
        if (triggered & TWAI_ALERT_TX_FAILED) {
            ESP_LOGW(TAG, "TWAI TX failed");
        }
        if (triggered & TWAI_ALERT_RX_DATA) {
            twai_message_t msg;
            while (twai_receive(&msg, 0) == ESP_OK) {
                handle_can_frame(&msg);
            }
        }

        if ((xTaskGetTickCount() - last_status_log) >= pdMS_TO_TICKS(2000)) {
            last_status_log = xTaskGetTickCount();
            twai_status_info_t st;
            if (twai_get_status_info(&st) == ESP_OK) {
                const char *state_str =
                    (st.state == TWAI_STATE_STOPPED)    ? "STOPPED" :
                    (st.state == TWAI_STATE_RUNNING)    ? "RUNNING" :
                    (st.state == TWAI_STATE_BUS_OFF)    ? "BUS_OFF" :
                    (st.state == TWAI_STATE_RECOVERING) ? "RECOVER" : "?";
                ESP_LOGI(TAG,
                    "TWAI %s tec=%lu rec=%lu tx_fail=%lu rx_q=%lu rx_miss=%lu rx_ovr=%lu arb_lost=%lu bus_err=%lu",
                    state_str,
                    (unsigned long)st.tx_error_counter,
                    (unsigned long)st.rx_error_counter,
                    (unsigned long)st.tx_failed_count,
                    (unsigned long)st.msgs_to_rx,
                    (unsigned long)st.rx_missed_count,
                    (unsigned long)st.rx_overrun_count,
                    (unsigned long)st.arb_lost_count,
                    (unsigned long)st.bus_error_count);
            }
        }
    }
}

static void can_init(void)
{
    ESP_LOGI(TAG, "Switching to CAN mode");
    io_ext_set_bit(IO_EXT_IO5_BIT, true);
    vTaskDelay(pdMS_TO_TICKS(5));

    s_twai_g_config = (twai_general_config_t)TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    s_twai_g_config.rx_queue_len = 32;
    /* IRAM ISR — cache disables (NVS write, SPI flash op) can't push a full
     * frame worth of pending events into a rx_missed. Requires
     * CONFIG_TWAI_ISR_IN_IRAM=y in sdkconfig. */
    s_twai_g_config.intr_flags |= ESP_INTR_FLAG_IRAM;
    s_twai_t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();

    /* Hardware acceptance filter — accept only standard-frame IDs in the
     * range 0x000–0x03F. Every ID handle_can_frame() cares about is in
     * this window; rejects never reach the ISR so a busy bus doesn't
     * generate thousands of interrupts/sec on core 0 for IDs we'd
     * ignore anyway. */
    s_twai_f_config.acceptance_code = 0x00000000;
    s_twai_f_config.acceptance_mask = 0x07FFFFFF;
    s_twai_f_config.single_filter   = true;

    /* Priority 1 (was 5) — handle_can_frame just flag-sets, it must not
     * outrank the main/LVGL task under CAN storms. Kept on core 1 so
     * the TWAI ISR pins there and doesn't compete with the RGB DMA ISR
     * on core 0. */
    xTaskCreatePinnedToCore(can_rx_task, "can_rx", 4096, NULL, 1, NULL, 1);
}

bool can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (!s_twai_installed) return false;
    twai_message_t msg = {0};
    msg.identifier = id;
    msg.data_length_code = len;
    memcpy(msg.data, data, len);
    return twai_transmit(&msg, pdMS_TO_TICKS(10)) == ESP_OK;
}

/* ==========================================================================
 * [1] Hardware — Screen timeout / wake ceremony
 * ========================================================================== */
static lv_obj_t *s_wake_overlay = NULL;

static void wake_touch_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "[wakediag] >>> wake_touch_cb ENTERED (overlay click received)");
    screen_timed_out = false;
    apply_brightness(desired_brightness);
    s_last_wake_us = esp_timer_get_time();
    if (s_wake_overlay) {
        lv_obj_del(s_wake_overlay);
        s_wake_overlay = NULL;
    }
    s_wake_recovery_pending = true;
    ESP_LOGI(TAG, "[wakediag] <<< wake_touch_cb EXIT (restored brightness %u)",
             desired_brightness);
}

/* Main-loop counter — incremented once per outer while(1) iteration below. */
static volatile uint32_t s_main_loop_count = 0;

static void wakediag_periodic(void)
{
    static int64_t s_last_log_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - s_last_log_us < 5000000) return;
    s_last_log_us = now;

    static uint32_t s_last_vsync_count = 0;
    static uint32_t s_last_loop_count  = 0;
    uint32_t vsync_now = s_vsync_count;
    uint32_t vsync_delta = vsync_now - s_last_vsync_count;
    s_last_vsync_count = vsync_now;
    uint32_t loop_now = s_main_loop_count;
    uint32_t loop_delta = loop_now - s_last_loop_count;
    s_last_loop_count = loop_now;
    /* /5s → per-second; vsync is ~35 Hz, loop is ~200 Hz baseline. */
    uint32_t vsync_hz = vsync_delta / 5;
    uint32_t loop_hz  = loop_delta / 5;

    if (screen_timed_out) {
        uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
        bool ch32_ok = (i2c_master_probe(i2c_bus, IO_EXT_ADDR, pdMS_TO_TICKS(50)) == ESP_OK);
        bool gt911_ok = (i2c_master_probe(i2c_bus, 0x5D, pdMS_TO_TICKS(50)) == ESP_OK) ||
                        (i2c_master_probe(i2c_bus, 0x14, pdMS_TO_TICKS(50)) == ESP_OK);
        int int_pin = gpio_get_level(4);
        ESP_LOGI(TAG, "[wakediag] sleep_tick inactive_ms=%lu CH32V003=%s GT911=%s INT=%d io_ext_out=0x%02X vsync=%lu (+%lu/5s=%luHz) loop=%luHz flush_timeouts=%lu",
                 (unsigned long)inactive_ms,
                 ch32_ok ? "OK" : "NACK",
                 gt911_ok ? "OK" : "NACK",
                 int_pin, io_ext_out,
                 (unsigned long)vsync_now,
                 (unsigned long)vsync_delta,
                 (unsigned long)vsync_hz,
                 (unsigned long)loop_hz,
                 (unsigned long)s_flush_timeout_count);
    } else {
        static uint32_t s_last_fb_count = 0, s_last_flush_count = 0;
        uint32_t fb_now = s_fb_complete_count;
        uint32_t fb_delta = fb_now - s_last_fb_count;
        s_last_fb_count = fb_now;
        uint32_t flush_now = s_flush_count;
        uint32_t flush_delta = flush_now - s_last_flush_count;
        s_last_flush_count = flush_now;
        uint32_t max_us = s_flush_take_timeout_us_max;
        s_flush_take_timeout_us_max = 0;
        ESP_LOGI(TAG, "[wakediag] awake_tick vsync=%luHz fb_complete=%luHz flush=%luHz max_wait=%luus loop=%luHz timeouts=%lu psram=%u/%u",
                 (unsigned long)vsync_hz,
                 (unsigned long)(fb_delta / 5),
                 (unsigned long)(flush_delta / 5),
                 (unsigned long)max_us,
                 (unsigned long)loop_hz,
                 (unsigned long)s_flush_timeout_count,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    }
}

static void handle_screen_timeout(void)
{
    wakediag_periodic();
    if (screen_timed_out) return;

    int32_t timeout_min = get_var_screen_timeout_minutes();
    if (timeout_min <= 0) return;

    uint32_t timeout_ms = (uint32_t)timeout_min * 60U * 1000U;
    uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
    if (inactive_ms < timeout_ms) return;

    screen_timed_out = true;
    s_dim_start_us = esp_timer_get_time();
    backlight_off();

    s_wake_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_wake_overlay);
    lv_obj_set_size(s_wake_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_wake_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_wake_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_wake_overlay, wake_touch_cb, LV_EVENT_CLICKED, NULL);
    ESP_LOGI(TAG, "[wakediag] timeout: dimming after %d min (desired=%u)",
             (int)timeout_min, desired_brightness);
}

#define WAKE_TOUCH_RESET_DIM_US   (10LL * 60LL * 1000000LL)  /* 10 minutes */

static void gt911_hardware_reset(void)
{
    gpio_config_t int_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(4),
    };
    gpio_config(&int_cfg);
    gpio_set_level(4, 0);
    io_ext_set_bit(IO_EXT_IO1_BIT, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    io_ext_set_bit(IO_EXT_IO1_BIT, true);
    vTaskDelay(pdMS_TO_TICKS(80));
    gpio_set_direction(4, GPIO_MODE_INPUT);
}

static void perform_wake_recovery(void)
{
    int64_t dim_us = esp_timer_get_time() - s_dim_start_us;
    ESP_LOGI(TAG, "[wakediag] wake ceremony: dim_duration=%lld ms flush_timeouts=%lu consec=%lu",
             (long long)(dim_us / 1000),
             (unsigned long)s_flush_timeout_count,
             (unsigned long)s_consecutive_flush_timeouts);

    xQueueReset(vsync_sem);
    xQueueReset(swap_done_sem);

    if (dim_us >= WAKE_TOUCH_RESET_DIM_US) {
        ESP_LOGI(TAG, "[wakediag] wake ceremony: resetting GT911 (dim >= %lld min)",
                 (long long)(WAKE_TOUCH_RESET_DIM_US / 60000000LL));
        gt911_hardware_reset();
    }

    lv_obj_invalidate(lv_scr_act());
}

static void perform_panel_hard_recovery(void)
{
    ESP_LOGE(TAG, "[wakediag] HARD panel recovery: LCD reset + drain sem + invalidate (flush_timeouts=%lu)",
             (unsigned long)s_flush_timeout_count);
    lcd_reset();
    xQueueReset(vsync_sem);
    xQueueReset(swap_done_sem);
    lv_obj_invalidate(lv_scr_act());
    s_consecutive_flush_timeouts = 0;
}

/* ==========================================================================
 * [2] CAN → set_var_* bridge
 *
 * update_ui_from_can() runs on the main loop under the LVGL lock. It walks
 * the g_<subsystem>_updated flags set by handle_can_frame and dispatches to
 * the setter API in vars.c. All widget updates happen inside those
 * setters — this file NEVER pokes objects.foo directly.
 *
 * While the panel is dim we drain the update flags but skip the setter
 * calls; that avoids fragmenting the LVGL mem pool with invisible redraws
 * over long idles.
 * ========================================================================== */
static bool s_system_time_set = false;

/* Pure UTC epoch calculation — Howard Hinnant "days_from_civil" algorithm.
 * Replaces the setenv("TZ","UTC0")+tzset()+mktime()+setenv(restore)+tzset()
 * dance. That dance leaked ~26 B of PSRAM per setenv overwrite on newlib;
 * with Bearing broadcasting CAN 0x06 at 1 Hz that was ~52 B/s, exhausting
 * the 4 MB PSRAM budget in ~22 h and wedging LVGL's next arc-mask
 * allocation. Symptom: tearing + dead touch + stale data after long uptime,
 * only clears on power cycle. See long-sleep-wake-bug memory for the full
 * root-cause writeup. This function is pure arithmetic — no allocation,
 * no libc TZ machinery. */
static int64_t utc_epoch_from_ymdhms(uint16_t y, uint8_t m, uint8_t d,
                                     uint8_t H, uint8_t M, uint8_t S)
{
    int32_t ye = (m <= 2) ? (int32_t)y - 1 : (int32_t)y;
    int32_t era = (ye >= 0 ? ye : ye - 399) / 400;
    uint32_t yoe = (uint32_t)(ye - era * 400);
    uint32_t doy = (153U * (m > 2 ? (uint32_t)m - 3U : (uint32_t)m + 9U) + 2U) / 5U
                   + (uint32_t)d - 1U;
    uint32_t doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    return days * 86400LL + (int64_t)H * 3600 + (int64_t)M * 60 + (int64_t)S;
}

static void set_system_time_from_bearing(void)
{
    /* Reject pre-fix garbage from GNSS. */
    if (g_clock_year < 2025 || g_clock_year > 2099) return;
    if (g_clock_month < 1 || g_clock_month > 12) return;
    if (g_clock_day   < 1 || g_clock_day   > 31) return;
    if (g_clock_hour  > 23) return;
    if (g_clock_minute > 59) return;
    if (g_clock_second > 60) return;

    int64_t gnss_epoch = utc_epoch_from_ymdhms(
        g_clock_year, g_clock_month, g_clock_day,
        g_clock_hour, g_clock_minute, g_clock_second);
    if (gnss_epoch <= 0) return;

    if (s_system_time_set) {
        time_t now; time(&now);
        int64_t diff = gnss_epoch > (int64_t)now
                       ? gnss_epoch - (int64_t)now
                       : (int64_t)now - gnss_epoch;
        if (diff <= 2) return;
    }

    struct timeval tv = { .tv_sec = (time_t)gnss_epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    s_system_time_set = true;
}

static const char *gnss_mode_string(uint8_t mode)
{
    switch (mode) {
    case 1: return "Gps";
    case 2: return "Beidou";
    case 3: return "Gps + Beidou";
    case 4: return "Glonass";
    case 5: return "Gps + Glonass";
    case 6: return "Beidou + Glonass";
    case 7: return "Gps + Beidou + Glonass";
    default: return "No Fix";
    }
}

static const char *solar_state_string(uint8_t s)
{
    switch (s) {
    case 0: return "Off";
    case 2: return "Fault";
    case 3: return "Bulk";
    case 4: return "Absorption";
    case 5: return "Float";
    default: return "Unknown";
    }
}

/* Called from actions.c on device toggle; also from vars.c to repaint the
 * home tile CHECKED state when a Torrent/Switchback status echoes back. */
extern void set_var_device01_status(int32_t v);
extern void set_var_device02_status(int32_t v);
extern void set_var_device03_status(int32_t v);
extern void set_var_device04_status(int32_t v);
extern void set_var_device05_status(int32_t v);
extern void set_var_device06_status(int32_t v);
extern void set_var_device07_status(int32_t v);
extern void set_var_device08_status(int32_t v);

static void push_device_states(void)
{
    typedef void (*setter_t)(int32_t);
    static const setter_t setters[NUM_BUTTONS] = {
        set_var_device01_status, set_var_device02_status,
        set_var_device03_status, set_var_device04_status,
        set_var_device05_status, set_var_device06_status,
        set_var_device07_status, set_var_device08_status,
    };
    for (int i = 0; i < NUM_BUTTONS; i++) {
        setters[i]((int32_t)g_button_state[i]);
    }
}

static void update_ui_from_can(void)
{
    /* Panel dim → drain state flags without touching LVGL (see comment
     * in the section header). Still seed the system clock so wake shows
     * the correct time. */
    if (screen_timed_out) {
        if (g_datetime_updated) {
            g_datetime_updated = false;
            set_system_time_from_bearing();
        }
        return;
    }

    if (g_datetime_updated) {
        g_datetime_updated = false;
        set_system_time_from_bearing();
    }

    if (g_device_status_updated) {
        g_device_status_updated = false;
        push_device_states();
    }

    if (g_temperature_updated) {
        g_temperature_updated = false;
        set_var_current_interior_temperature((int32_t)g_interior_temp_f);
        set_var_humidity((float)(g_humidity_raw / 100.0f));
        set_var_tvoc((int32_t)g_tvoc_ppb);
        set_var_co2((int32_t)g_eco2_ppm);
    }

    if (g_gps_sat_updated) {
        g_gps_sat_updated = false;
        set_var_satellite_count((int32_t)g_gps_num_sats);
        set_var_gnss_mode(gnss_mode_string(g_gps_gnss_mode));
    }

    if (g_gps_alt_updated) {
        g_gps_alt_updated = false;
        /* Bearing altitude is centimeters. Convert to feet. */
        double alt_m  = (double)g_gps_altitude_raw * 0.01;
        double alt_ft = alt_m * 3.28084;
        set_var_altitude((float)alt_ft);
    }

    if (g_gps_latlon_updated) {
        g_gps_latlon_updated = false;
        set_var_latitude(g_gps_latitude);
        set_var_longitude(g_gps_longitude);
    }

    if (g_batt_shunt1_updated) {
        g_batt_shunt1_updated = false;
        /* Shunt1: voltage (whole.dec) + SOC (whole.dec). */
        float volts = (float)g_batt_voltage_whole + (float)g_batt_voltage_dec / 100.0f;
        int32_t soc = (int32_t)g_batt_soc_whole;
        set_var_battery_voltage(volts);
        set_var_battery_soc(soc);
    }

    if (g_batt_shunt2_updated) {
        g_batt_shunt2_updated = false;
        int32_t watts = (int32_t)g_shunt_wattage;
        if (g_is_wattage_negative == 0xFF) watts = -watts;
        set_var_consumption_watts(watts);
        uint16_t ttg = g_time_to_go_min;
        if (ttg == 0 || ttg == 0xFFFF) {
            set_var_time_remaining(-1);
        } else {
            set_var_time_remaining((int32_t)ttg);
        }
    }

    if (g_solar_mppt1_updated) {
        g_solar_mppt1_updated = false;
        set_var_solar_watts((int32_t)g_solar_wattage);
        set_var_solar_status(solar_state_string(g_solar_charge_status));
    }

    if (g_water_levels_updated) {
        g_water_levels_updated = false;
        set_var_water_levels((int32_t)g_fresh_water_level,
                             (int32_t)g_grey_water_level,
                             (int32_t)g_black_water_level);
    }

    if (g_tilt_updated) {
        g_tilt_updated = false;
        float pitch = (float)g_tilt_pitch_x100 / 100.0f;
        float roll  = (float)g_tilt_roll_x100  / 100.0f;
        set_var_leveling(pitch, roll,
                         (int32_t)g_tilt_fb_diff_mm,
                         (int32_t)g_tilt_lr_diff_mm);
    }

    /* 1 Hz clock repaint. update_clock_display() lives in vars.c. */
    static int64_t s_last_clock_tick_us = 0;
    int64_t now_us_clock = esp_timer_get_time();
    if (s_system_time_set && (now_us_clock - s_last_clock_tick_us) >= 1000000) {
        s_last_clock_tick_us = now_us_clock;
        update_clock_display();
    }
}

/* ==========================================================================
 * [4] app_main
 * ========================================================================== */
void app_main(void)
{
    /* Boot banner — reset reason FIRST so panic / TWDT / brownout events
     * are visible even if a later init blows up. */
    {
        esp_reset_reason_t r = esp_reset_reason();
        const char *name = "?";
        switch (r) {
            case ESP_RST_UNKNOWN:    name = "UNKNOWN";    break;
            case ESP_RST_POWERON:    name = "POWERON";    break;
            case ESP_RST_EXT:        name = "EXT_PIN";    break;
            case ESP_RST_SW:         name = "SW";         break;
            case ESP_RST_PANIC:      name = "PANIC";      break;
            case ESP_RST_INT_WDT:    name = "INT_WDT";    break;
            case ESP_RST_TASK_WDT:   name = "TASK_WDT";   break;
            case ESP_RST_WDT:        name = "OTHER_WDT";  break;
            case ESP_RST_DEEPSLEEP:  name = "DEEPSLEEP";  break;
            case ESP_RST_BROWNOUT:   name = "BROWNOUT";   break;
            case ESP_RST_SDIO:       name = "SDIO";       break;
            case ESP_RST_USB:        name = "USB";        break;
            case ESP_RST_JTAG:       name = "JTAG";       break;
            case ESP_RST_EFUSE:      name = "EFUSE";      break;
            case ESP_RST_PWR_GLITCH: name = "PWR_GLITCH"; break;
            case ESP_RST_CPU_LOCKUP: name = "CPU_LOCKUP"; break;
            default: break;
        }
        printf("\n\n[wakediag] ======== BOOT reset_reason=%d (%s) ========\n\n", (int)r, name);
    }

    /* NVS + WiFi config (populates hostname from MAC). wifi_config_init()
     * calls nvs_flash_init internally. */
    ESP_ERROR_CHECK(wifi_config_init());
    wifi_config_load();
    ESP_ERROR_CHECK(milepost_config_init());
    alarms_init();

    /* OTA + discovery — Milepost keeps the Milepost-native modules for
     * these. Both spawn WiFi on demand from CAN triggers. */
    ota_init();
    discovery_init();

    /* Hardware init (IO extension first — LCD and touch need it for reset) */
    io_ext_init();
    lcd_init();
    touch_init();
    lvgl_init();

    /* Recursive mutex serializing non-main-task access to LVGL widgets
     * (WiFi event task in app_state.c, OTA task in ota.c). Must exist
     * before ui_init in case any init helper spawns a worker. */
    s_lvgl_lock = xSemaphoreCreateRecursiveMutex();

    /* Confirm firmware is good (OTA rollback protection). */
    esp_ota_mark_app_valid_cancel_rollback();

    /* Load button configuration BEFORE ui_init so apply_to_ui has real
     * labels to write. ui_init() creates the widgets — apply runs right
     * after. */
    button_config_init();

    /* EEZ Studio UI + all UI init helpers, all under the LVGL lock.
     * ui_init() must run first so subsequent helpers can reach the
     * objects they need to bind. */
    ui_init();
    {
        const esp_app_desc_t *app = esp_app_get_description();
        if (app && objects.label_version_number) {
            lv_label_set_text(objects.label_version_number, app->version);
        }

        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char macbuf[18];
        snprintf(macbuf, sizeof(macbuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        set_var_mcu_mac_address(macbuf);
    }
    set_var_rotation_degrees(0);
    restore_user_settings();
    button_config_apply_to_ui();

    /* Land on Home immediately. */
    if (objects.page_home) lv_scr_load(objects.page_home);

    /* Milepost UI helpers — see Milepost main.c for ordering rationale.
     * We SKIP init_battery_poll (no ADC-fed battery), audio_init /
     * peregrine_voice_init (no speaker/mic), and sd_config_load
     * (no SD card in Milepost enclosure). init_wifi_rssi_poll runs
     * only when WiFi is up, and is safe to call while it's not. */
    init_clock_blink();
    init_metric_charts();
    init_wifi_rssi_poll();
    init_notif_icon_ack_taps();
    init_touch_target_hit_areas();
    {
        extern void perf_init(void);
        perf_init();
    }
    init_screen_timeout();
    reset_placeholders();
    {
        extern void init_button_edit_bindings(void);
        init_button_edit_bindings();
    }

    /* Apply persisted brightness (milepost NVS namespace, populated by
     * restore_user_settings above). Slider stores 0..100 %; scale to
     * the 0..255 range apply_brightness expects. */
    {
        int32_t v = get_var_screen_brightness();
        if (v < 10)  v = 10;
        if (v > 100) v = 100;
        set_backlight((uint8_t)(v * 255 / 100));
    }

    /* Start CAN (switches GPIO19/20 from USB to CAN transceiver via
     * CH32V003 IO5, then spawns can_rx_task pinned to core 1). */
    can_init();

    ESP_LOGI(TAG, "Initialization complete");

    /* Subscribe the main (LVGL) task to the Task WDT. If any call in the
     * loop below blocks for longer than CONFIG_ESP_TASK_WDT_TIMEOUT_S,
     * the WDT backtrace points straight at the stuck task. */
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err != ESP_OK) {
        ESP_LOGW(TAG, "[wakediag] esp_task_wdt_add(main)=%s", esp_err_to_name(wdt_err));
    }

    while (1) {
        esp_task_wdt_reset();
        s_main_loop_count++;

        /* Recovery actions BEFORE lv_timer_handler so the full-screen
         * invalidate they schedule is picked up in this iteration. Hard
         * panel recovery goes first — its lcd_reset() reinstates the
         * pipeline the wake ceremony's vsync-drain then syncs to. */
        if (s_panel_hard_recovery_pending) {
            s_panel_hard_recovery_pending = false;
            perform_panel_hard_recovery();
        }
        if (s_wake_recovery_pending) {
            s_wake_recovery_pending = false;
            perform_wake_recovery();
        }

        if (lvgl_port_lock(0)) {
            /* lv_timer_handler must keep running during dim so the touch
             * pipeline (wake_touch_cb via LVGL event system) fires — but
             * everything below that would push new content into widgets
             * is gated on !screen_timed_out per user requirement of
             * "STOP ALL refreshing of all gauges while screen is off." */
            lv_timer_handler();
            update_ui_from_can();       /* self-gated: returns early on dim */
            handle_screen_timeout();    /* runs during dim: manages the state */
            if (!screen_timed_out) {
                ota_update_ui();
                discovery_update_ui();
                wifi_config_check_timeout();
            }
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
