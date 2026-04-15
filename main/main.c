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
#include "can_common.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "lvgl.h"

#include "ui/ui.h"
#include "include/ota.h"
#include "include/discovery.h"
#include "include/wifi_config.h"
#include "ui/vars.h"
#include "ui/styles.h"
#include "button_config.h"

static const char *TAG = "milepost";

// Display resolution (ESP32-S3-Touch-LCD-7B: 1024x600)
#define SCREEN_WIDTH  1024
#define SCREEN_HEIGHT 600

// ============================================================================
// IO Extension (CH32V003 MCU) via I2C
// Register-based protocol at address 0x24:
//   0x02 = mode control (0xFF = all pins output)
//   0x03 = IO output    (bitfield for IO0-IO7)
//   0x05 = PWM output   (backlight brightness: 0=bright, ~247=dimmest)
//   0x06 = ADC input    (battery voltage)
// Pin mapping within the IO output byte:
//   Bit 1 = IO1 = Touch RST
//   Bit 2 = IO2 = Backlight enable
//   Bit 3 = IO3 = LCD RST
//   Bit 4 = IO4 = SD_CS
//   Bit 5 = IO5 = CAN_SEL (high=CAN, low=USB)
// ============================================================================
#define IO_EXT_ADDR          0x24
#define IO_EXT_REG_MODE      0x02
#define IO_EXT_REG_OUTPUT    0x03
#define IO_EXT_REG_PWM       0x05

#define IO_EXT_IO1_BIT  (1 << 1)   // Touch RST
#define IO_EXT_IO2_BIT  (1 << 2)   // Backlight enable
#define IO_EXT_IO3_BIT  (1 << 3)   // LCD RST
#define IO_EXT_IO5_BIT  (1 << 5)   // CAN_SEL

#define I2C_SDA_PIN  8
#define I2C_SCL_PIN  9
#define I2C_FREQ_HZ  400000

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t io_ext_dev = NULL;
static uint8_t io_ext_out = 0;

static esp_err_t io_ext_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(io_ext_dev, buf, 2, pdMS_TO_TICKS(100));
}

static void io_ext_set_bit(uint8_t bit, bool high)
{
    if (high) io_ext_out |= bit;
    else      io_ext_out &= ~bit;
    io_ext_write_reg(IO_EXT_REG_OUTPUT, io_ext_out);
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

    // Set all IO pins to output mode
    esp_err_t err = io_ext_write_reg(IO_EXT_REG_MODE, 0xFF);
    ESP_LOGI(TAG, "IO extension init %s", err == ESP_OK ? "OK" : "FAILED");

    // Initialize all pins HIGH (matches Waveshare demo default).
    // Specific pins are pulled low later as needed (e.g. CAN_SEL for USB mode).
    io_ext_out = 0xFF;
    io_ext_write_reg(IO_EXT_REG_OUTPUT, io_ext_out);
}

// ============================================================================
// Backlight (PWM via CH32V003 IO extension register 0x05)
// PWM register is inverted: 0 = full bright, ~247 = dimmest safe value.
// The CH32V003 caps at 97% duty to prevent the backlight from fully turning
// off via PWM alone — use IO2 (backlight enable) for true off.
// ============================================================================
// Minimum user-settable brightness. The CH32V003 PWM is inverted —
// brightness N maps to PWM (255-N) — so low numbers look almost black.
// 32 still lets the user dim the screen aggressively while staying
// clearly visible, so even a clamped value looks lit instead of dead.
#define BRIGHTNESS_MIN_USER  32

// User's desired brightness. This is the source of truth for what the
// screen SHOULD show when it's awake. The timeout code path NEVER writes
// this variable — it only calls apply_brightness(0) directly to dim the
// physical backlight while leaving `desired_brightness` untouched, so
// waking the screen restores whatever the user last asked for.
static uint8_t desired_brightness = 255;
static bool    screen_timed_out   = false;

// Wall-clock of the last wake-from-timeout event. actions.c reads this
// via screen_wake_age_us() to ignore brightness-slider RELEASED events
// that fire during a wake-tap so the user's stored brightness can't
// be clobbered by a touch that was intended only to wake the screen.
static int64_t s_last_wake_us = 0;
int64_t screen_wake_age_us(void)
{
    return esp_timer_get_time() - s_last_wake_us;
}

// Map brightness (0-255) to CH32V003 PWM register. PWM-only — the IO2
// backlight-enable pin is left high after boot and never toggled. We
// tried cycling IO2 low to fully blank the screen on timeout and back
// high to wake, but the CH32V003 dropped its latched PWM value across
// that transition (even with PWM written before AND twice after the
// IO2 high edge), so the backlight would never come back.
//
// With PWM only, the CH32V003 caps at 247/247 ≈ 97% duty on an inverted
// driver — i.e. the screen never goes 100% dark on "timeout". It'll
// glow at roughly 3% duty, which is very dim in a lit room and a faint
// glow in the dark, but the wake path is 100% reliable because we're
// just writing a new PWM value on a line that's already active.
static void apply_brightness(uint8_t brightness)
{
    // Ensure backlight enable is on (only actually writes if the bit
    // isn't already set — io_ext_set_bit is idempotent).
    io_ext_set_bit(IO_EXT_IO2_BIT, true);

    // Invert: brightness 255 → PWM 0 (full bright),
    //         brightness 0   → PWM 247 (dimmest we can achieve via PWM)
    uint8_t pwm_val = (brightness >= 255) ? 0 : (uint8_t)(255 - brightness);
    if (pwm_val > 247) pwm_val = 247;
    io_ext_write_reg(IO_EXT_REG_PWM, pwm_val);
}

// Update the user's desired brightness AND (if the screen isn't currently
// dimmed by the timeout logic) push it to the hardware. Called from the
// brightness slider's PRESSING handler. Clamps to BRIGHTNESS_MIN_USER so
// stray slider events during a wake-tap can't lock us out.
void set_backlight(uint8_t brightness)
{
    if (brightness < BRIGHTNESS_MIN_USER) brightness = BRIGHTNESS_MIN_USER;
    desired_brightness = brightness;
    if (!screen_timed_out) {
        apply_brightness(brightness);
    }
}

uint8_t get_backlight(void)
{
    return desired_brightness;
}

// ============================================================================
// NVS settings
// ============================================================================
static nvs_handle_t nvs_settings;

// ============================================================================
// RGB LCD panel (double-buffered with vsync synchronization)
// ============================================================================
static esp_lcd_panel_handle_t panel_handle = NULL;
static SemaphoreHandle_t vsync_sem = NULL;

// Called from ISR at each vertical blanking interval — signals safe to swap FB
static IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t panel,
                                const esp_lcd_rgb_panel_event_data_t *edata,
                                void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(vsync_sem, &woken);
    return woken == pdTRUE;
}

static void lcd_reset(void)
{
    // Pulse LCD RST via IO extension IO3
    io_ext_set_bit(IO_EXT_IO3_BIT, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    io_ext_set_bit(IO_EXT_IO3_BIT, true);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void lcd_init(void)
{
    lcd_reset();
    vsync_sem = xSemaphoreCreateBinary();

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 30850000,
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
            // B[0:4]
            14, 38, 18, 17, 10,
            // G[0:5]
            39, 0, 45, 48, 47, 21,
            // R[0:4]
            1, 2, 42, 41, 40,
        },
        .flags.fb_in_psram = true,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Register vsync callback so we can synchronize framebuffer swaps
    esp_lcd_rgb_panel_event_callbacks_t cbs = { .on_vsync = on_vsync };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL));

    ESP_LOGI(TAG, "RGB LCD initialized (double-buffered, vsync-synced)");
}

// ============================================================================
// GT911 touch
// ============================================================================
static esp_lcd_touch_handle_t touch_handle = NULL;

// Probe an I2C address using the new driver's built-in probe.
static bool i2c_probe(uint8_t addr)
{
    return i2c_master_probe(i2c_bus, addr, pdMS_TO_TICKS(50)) == ESP_OK;
}

static void touch_init(void)
{
    // GT911 I2C address is selected by INT pin state at RST rising edge:
    //   INT low  → 0x5D (default)
    //   INT high → 0x14 (backup)
    // Sequence from Waveshare demo: RST low 100ms, INT low 100ms, RST high, 200ms settle.
    gpio_config_t int_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(4),
    };
    gpio_config(&int_cfg);

    io_ext_set_bit(IO_EXT_IO1_BIT, false);   // RST low
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(4, 0);                     // INT low → selects address 0x5D
    vTaskDelay(pdMS_TO_TICKS(100));
    io_ext_set_bit(IO_EXT_IO1_BIT, true);     // RST high (INT still low)
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_direction(4, GPIO_MODE_INPUT);   // Release INT for interrupt use

    // Probe GT911 at both possible addresses
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
        .rst_gpio_num = -1,   // RST handled via IO extension above
        .int_gpio_num = 4,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle));
    ESP_LOGI(TAG, "GT911 touch initialized at 0x%02X", gt911_addr);
}

// ============================================================================
// LVGL tick (driven by esp_timer, 1ms period)
// ============================================================================
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

// ============================================================================
// LVGL display driver (double-buffered direct mode, vsync-synced)
// LVGL renders into one PSRAM framebuffer while DMA reads from the other.
// On flush, we wait for vsync then swap — zero tearing.
// ============================================================================
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    if (lv_disp_flush_is_last(drv)) {
        // Wait for the current frame to finish displaying (vsync)
        xSemaphoreTake(vsync_sem, portMAX_DELAY);
        // Swap DMA to read from the buffer LVGL just finished rendering
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, color_map);
    }
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    if (!touch_handle) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    esp_lcd_touch_read_data(touch_handle);

    esp_lcd_touch_point_data_t pt;
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(touch_handle, &pt, &count, 1) == ESP_OK && count > 0) {
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

    // 1ms tick timer for LVGL
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));  // 1ms

    // Get both PSRAM framebuffers for tear-free double-buffered direct mode
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

    // Touch input
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "LVGL initialized (direct mode)");
}

// ============================================================================
// CAN bus
// ============================================================================
#define CAN_TX_PIN           20
#define CAN_RX_PIN           19
#define CAN_BAUDRATE_KBPS    500

#define CAN_ID_OTA_TRIGGER        0x00
#define CAN_ID_WIFI_CONFIG        0x01
#define CAN_ID_DISCOVERY_TRIGGER  0x02
#define CAN_ID_DATETIME           0x06
#define CAN_ID_GPS_SAT_SPEED      0x07
#define CAN_ID_GPS_ALTITUDE       0x08
#define CAN_ID_GPS_LATLON         0x09
#define CAN_ID_TEMPERATURE   0x1F
#define CAN_ID_BATT_SHUNT1   0x23
#define CAN_ID_BATT_SHUNT2   0x24
#define CAN_ID_SOLAR_MPPT1   0x2C
#define CAN_ID_WATER_TANK_LEVELS 0x3E

// Button status flag — set when any mapped button's state changes
volatile bool g_device_status_updated = false;

// Clock state (CAN 0x06 from Bearing) — UTC
volatile uint16_t g_clock_year = 0;
volatile uint8_t  g_clock_month = 0;
volatile uint8_t  g_clock_day = 0;
volatile uint8_t  g_clock_hour = 0;
volatile uint8_t  g_clock_minute = 0;
volatile uint8_t  g_clock_second = 0;
volatile bool     g_datetime_updated = false;

// Temperature & humidity + air quality (CAN ID 0x1F from Borealis)
// Payload: [tempC_int8, tempF_uint8, hum_hi, hum_lo, tvoc_hi, tvoc_lo, eco2_hi, eco2_lo]
volatile uint8_t  g_interior_temp_f = 0;
volatile int8_t   g_interior_temp_c = 0;
volatile uint16_t g_humidity_raw = 0;
volatile uint16_t g_tvoc_ppb = 0;
volatile uint16_t g_eco2_ppm = 0;
volatile bool g_temperature_updated = false;

// GPS (CAN IDs 0x07, 0x08)
volatile uint8_t  g_gps_num_sats = 0;
volatile uint8_t  g_gps_gnss_mode = 0;
volatile uint32_t g_gps_altitude_raw = 0;
volatile float    g_gps_latitude = 0.0f;
volatile float    g_gps_longitude = 0.0f;
volatile bool g_gps_sat_updated = false;
volatile bool g_gps_alt_updated = false;
volatile bool g_gps_latlon_updated = false;

// Battery shunt (CAN IDs 0x23, 0x24)
volatile uint8_t  g_batt_voltage_whole = 0;
volatile uint8_t  g_batt_voltage_dec = 0;
volatile uint8_t  g_batt_soc_whole = 0;
volatile uint8_t  g_batt_soc_dec = 0;
volatile bool     g_batt_shunt1_updated = false;

volatile uint8_t  g_is_wattage_negative = 0;
volatile uint16_t g_shunt_wattage = 0;
volatile uint16_t g_time_to_go_min = 0;
volatile bool     g_batt_shunt2_updated = false;

// Solar MPPT (CAN ID 0x2C)
volatile uint16_t g_solar_wattage = 0;
volatile uint8_t  g_solar_charge_status = 0;
volatile bool     g_solar_mppt1_updated = false;

// Water tank levels from Reservoir (CAN ID 0x3E)
volatile uint8_t  g_fresh_water_level = 0;
volatile uint8_t  g_grey_water_level = 0;
volatile uint8_t  g_black_water_level = 0;
volatile bool     g_water_levels_updated = false;


static void handle_can_frame(const twai_message_t *msg)
{
    if (msg->rtr) return;

    // Per-frame debug trace — LOGD so it only shows when the log level
    // is bumped to DEBUG. Keeps the default INFO stream readable.
    ESP_LOGD(TAG, "CAN RX id=0x%03lX dlc=%u data=%02X %02X %02X %02X %02X %02X %02X %02X",
             (unsigned long)msg->identifier,
             (unsigned)msg->data_length_code,
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

    // Torrent status: 8 bytes, one PWM value per channel (one message per instance)
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

    // Switchback status: 1-byte bitfield (bit N = relay N state)
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
        g_gps_num_sats = msg->data[0];
        g_gps_gnss_mode = msg->data[5];
        g_gps_sat_updated = true;
    } else if (msg->identifier == CAN_ID_GPS_ALTITUDE && msg->data_length_code >= 4) {
        g_gps_altitude_raw = ((uint32_t)msg->data[0] << 24) |
                             ((uint32_t)msg->data[1] << 16) |
                             ((uint32_t)msg->data[2] << 8) |
                             msg->data[3];
        g_gps_alt_updated = true;
    } else if (msg->identifier == CAN_ID_GPS_LATLON && msg->data_length_code >= 8) {
        // Bearing 0x09: [lat_sign, lat2, lat1, lat0, lon_sign, lon2, lon1, lon0]
        // where abs_value = ((b1<<16)|(b2<<8)|b3) and real = abs_value / 10000.
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
        g_shunt_wattage = ((uint16_t)msg->data[1] << 8) | msg->data[2];
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
        ESP_LOGI(TAG, "CAN 0x3E WaterTankLevels: fresh=%u%% grey=%u%% black=%u%% (raw: %02X %02X %02X dlc=%u)",
                 (unsigned)g_fresh_water_level,
                 (unsigned)g_grey_water_level,
                 (unsigned)g_black_water_level,
                 msg->data[0], msg->data[1], msg->data[2],
                 (unsigned)msg->data_length_code);
    }
}

// Alert-driven CAN task. Follows the pattern used by Switchback/Torrent/Borealis/
// Bearing: twai_read_alerts() drives everything, bus-off is recovered via
// twai_initiate_recovery() + TWAI_ALERT_BUS_RECOVERED + twai_start(). Milepost
// has no periodic TX heartbeat, so there is no TX_ACTIVE/TX_PROBING state
// machine — user-initiated can_send() calls are best-effort.
static void can_rx_task(void *arg)
{
    // Configure alerts BEFORE any bus activity so no error transitions are missed.
    twai_reconfigure_alerts(CAN_COMMON_ALERTS, NULL);

    // Alerts armed — one-shot broadcast at startup; NO repeat on BUS_RECOVERED
    // (Milepost is RX-only — no TX_PROBING guard, so a repeat would cycle).
    can_common_version_broadcast();

    TickType_t last_status_log = xTaskGetTickCount();

    while (1) {
        uint32_t triggered = 0;
        twai_read_alerts(&triggered, pdMS_TO_TICKS(100));

        // --- Bus error handling ---
        if (triggered & TWAI_ALERT_BUS_OFF) {
            ESP_LOGE(TAG, "TWAI bus-off, initiating recovery");
            twai_initiate_recovery();
            // No continue — fall through so RX_DATA in the same poll is still processed.
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

        // --- Drain received messages ---
        if (triggered & TWAI_ALERT_RX_DATA) {
            twai_message_t msg;
            while (twai_receive(&msg, 0) == ESP_OK) {
                handle_can_frame(&msg);
            }
        }

        // --- Periodic bus health status (every 2s) for debugging ---
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
    // Switch GPIO19/20 from USB to CAN transceiver
    ESP_LOGI(TAG, "Switching to CAN mode");
    io_ext_set_bit(IO_EXT_IO5_BIT, true);
    vTaskDelay(pdMS_TO_TICKS(5));

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 32;
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK &&
        twai_start() == ESP_OK) {
        ESP_LOGI(TAG, "TWAI started on TX=%d RX=%d at 500kbps", CAN_TX_PIN, CAN_RX_PIN);
        // Version broadcast is sent in can_rx_task() after alerts are armed.
        xTaskCreatePinnedToCore(can_rx_task, "can_rx", 4096, NULL, 5, NULL, 1);
    } else {
        ESP_LOGE(TAG, "TWAI initialization failed");
    }
}

// ============================================================================
// CAN transmit (called from actions.c)
// ============================================================================
bool can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    twai_message_t msg = {0};
    msg.identifier = id;
    msg.data_length_code = len;
    memcpy(msg.data, data, len);
    return twai_transmit(&msg, pdMS_TO_TICKS(10)) == ESP_OK;
}

// ============================================================================
// Device status indicator update
// ============================================================================
void update_device_status_indicators(bool force)
{
    static bool prev_on[NUM_BUTTONS] = {false};

    lv_obj_t *indicators[NUM_BUTTONS] = {
        objects.lbl_device01_status_ind,
        objects.lbl_device02_status_ind,
        objects.lbl_device03_status_ind,
        objects.lbl_device04_status_ind,
        objects.lbl_device05_status_ind,
        objects.lbl_device06_status_ind,
        objects.lbl_device07_status_ind,
        objects.lbl_device08_status_ind,
    };

    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool is_on = g_button_state[i] > 0;
        if (force || is_on != prev_on[i]) {
            lv_obj_remove_local_style_prop(indicators[i],
                LV_STYLE_TEXT_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
            if (is_on) {
                remove_style_style_device_status_ind_off(indicators[i]);
                add_style_style_device_status_ind_on(indicators[i]);
            } else {
                remove_style_style_device_status_ind_on(indicators[i]);
                add_style_style_device_status_ind_off(indicators[i]);
            }
            prev_on[i] = is_on;
        }
    }
}

// ============================================================================
// Clock (CAN 0x06 DateTime from Bearing)
// ============================================================================

// POSIX TZ strings — index matches the DropDownSelectedTimeZone options:
// "Alaska / Chicago, Illinois / Denver, Colorado / Hawaii / Los Angeles /
// New York / Phoenix".
static const char *TIMEZONE_POSIX[] = {
    "AKST9AKDT,M3.2.0/2:00:00,M11.1.0/2:00:00",  // Alaska
    "CST6CDT,M3.2.0/2:00:00,M11.1.0/2:00:00",    // Chicago
    "MST7MDT,M3.2.0/2:00:00,M11.1.0/2:00:00",    // Denver
    "HST10",                                      // Hawaii (no DST)
    "PST8PDT,M3.2.0/2:00:00,M11.1.0/2:00:00",    // Los Angeles
    "EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00",    // New York
    "MST7",                                       // Phoenix (no DST)
};
#define TIMEZONE_COUNT (sizeof(TIMEZONE_POSIX) / sizeof(TIMEZONE_POSIX[0]))

static int32_t s_tz_index = 5;       // New York default
static bool    s_system_time_set = false;

// Apply the currently-selected POSIX TZ string to libc's time routines.
static void apply_user_timezone(void)
{
    int idx = s_tz_index;
    if (idx < 0 || idx >= (int)TIMEZONE_COUNT) idx = 0;
    setenv("TZ", TIMEZONE_POSIX[idx], 1);
    tzset();
}

// Seed the system clock from the most recent CAN 0x06 datetime frame.
// Bearing's datetime is UTC from GNSS, so we interpret it as UTC.
static void set_system_time_from_bearing(void)
{
    if (g_clock_year < 2020) return;  // wait for valid GNSS lock

    // Convert UTC fields → epoch via mktime with TZ temporarily UTC
    setenv("TZ", "UTC0", 1);
    tzset();

    struct tm tm_utc = {
        .tm_year = g_clock_year - 1900,
        .tm_mon  = g_clock_month - 1,
        .tm_mday = g_clock_day,
        .tm_hour = g_clock_hour,
        .tm_min  = g_clock_minute,
        .tm_sec  = g_clock_second,
    };
    time_t t = mktime(&tm_utc);
    if (t <= 0) return;

    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    s_system_time_set = true;

    // Restore user's chosen timezone for display
    apply_user_timezone();
}

static const char *k_month_names[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

// Memoized display state — reset via clock_invalidate_cache() when the
// user changes timezone so the next update_clock_display() repaints.
static int s_last_min  = -1;
static int s_last_hour = -1;
static int s_last_pm   = -1;
static int s_last_mday = -1;

static void clock_invalidate_cache(void)
{
    s_last_min = s_last_hour = s_last_pm = s_last_mday = -1;
}

static void update_clock_display(void)
{
    if (!s_system_time_set) return;

    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);

    if (ti.tm_min != s_last_min) {
        char min_buf[4];
        snprintf(min_buf, sizeof(min_buf), "%02d", ti.tm_min);
        lv_label_set_text(objects.lbl_time_minutes, min_buf);
        s_last_min = ti.tm_min;
    }
    if (ti.tm_hour != s_last_hour) {
        int h12 = ti.tm_hour % 12;
        if (h12 == 0) h12 = 12;
        char hour_buf[4];
        snprintf(hour_buf, sizeof(hour_buf), "%d", h12);
        lv_label_set_text(objects.lbl_time_hour, hour_buf);
        s_last_hour = ti.tm_hour;

        int pm = ti.tm_hour >= 12 ? 1 : 0;
        if (pm != s_last_pm) {
            lv_label_set_text(objects.lbl_time_am_pm, pm ? "PM" : "AM");
            s_last_pm = pm;
        }
    }
    if (ti.tm_mday != s_last_mday) {
        char date_buf[32];
        snprintf(date_buf, sizeof(date_buf), "%s %d, %d",
                 k_month_names[ti.tm_mon], ti.tm_mday, ti.tm_year + 1900);
        lv_label_set_text(objects.lbl_date, date_buf);
        s_last_mday = ti.tm_mday;
    }
}

// Public entry points so actions.c can drive TZ changes.
void clock_set_timezone_index(int32_t idx)
{
    if (idx < 0 || idx >= (int)TIMEZONE_COUNT) return;
    s_tz_index = idx;
    apply_user_timezone();
    clock_invalidate_cache();
    update_clock_display();
}

int32_t clock_get_timezone_index(void) { return s_tz_index; }
int32_t clock_get_timezone_count(void) { return (int32_t)TIMEZONE_COUNT; }

// ============================================================================
// Temperature unit (Fahrenheit / Celsius)
// ============================================================================
// vars.c's set_var_temperature_unit stores the raw int; this helper converts
// a Fahrenheit reading into whatever unit the user currently wants to see.
int32_t fahrenheit_to_display(int32_t temp_f)
{
    if (get_var_temperature_unit() == 1) {
        return (temp_f - 32) * 5 / 9;
    }
    return temp_f;
}

const char *temperature_unit_suffix(void)
{
    return (get_var_temperature_unit() == 1) ? "\u00b0C" : "\u00b0F";
}

// ============================================================================
// Screen timeout — Fireside pattern
// ============================================================================
// When the display has been inactive for N minutes, dim the backlight to 0
// and drop a fullscreen CLICKABLE overlay onto lv_layer_top(). The overlay
// absorbs the first touch after blanking so it can't accidentally trigger
// an underlying widget (like the brightness slider on PageSettings), then
// restores the backlight to the user's chosen level and destroys itself.
//
// Ported verbatim from TrailCurrentFireside's main loop (which has been
// battle-tested). The only Milepost-specific change: we drive the PWM via
// apply_brightness() / desired_brightness on the CH32V003 IO extender
// instead of Fireside's bsp_display_brightness_set() BSP call.

static lv_obj_t *s_wake_overlay = NULL;

static void wake_touch_cb(lv_event_t *e)
{
    (void)e;
    screen_timed_out = false;
    apply_brightness(desired_brightness);
    s_last_wake_us = esp_timer_get_time();
    if (s_wake_overlay) {
        lv_obj_del(s_wake_overlay);
        s_wake_overlay = NULL;
    }
    ESP_LOGI(TAG, "wake: restored brightness %u", desired_brightness);
}

static void handle_screen_timeout(void)
{
    if (screen_timed_out) return;

    int32_t timeout_min = get_var_screen_timeout_value();
    if (timeout_min <= 0) return;

    uint32_t timeout_ms = (uint32_t)timeout_min * 60U * 1000U;
    uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
    if (inactive_ms < timeout_ms) return;

    screen_timed_out = true;
    apply_brightness(0);

    // Fullscreen click-absorber overlay on the top layer so the first
    // wake-tap can't reach any widget underneath it.
    s_wake_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_wake_overlay);
    lv_obj_set_size(s_wake_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_wake_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_wake_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_wake_overlay, wake_touch_cb, LV_EVENT_CLICKED, NULL);
    ESP_LOGI(TAG, "timeout: dimming after %d min (desired=%u)",
             (int)timeout_min, desired_brightness);
}

// ============================================================================
// UI data update (called each loop iteration)
// ============================================================================
static void update_ui_from_can(void)
{
    if (g_datetime_updated) {
        g_datetime_updated = false;
        if (!s_system_time_set) {
            set_system_time_from_bearing();
        }
    }
    // Tick the clock display at most once per second
    static int64_t s_last_clock_tick_us = 0;
    int64_t now_us_clock = esp_timer_get_time();
    if (s_system_time_set && (now_us_clock - s_last_clock_tick_us) >= 500000) {
        s_last_clock_tick_us = now_us_clock;
        update_clock_display();
    }

    if (g_device_status_updated) {
        g_device_status_updated = false;
        update_device_status_indicators(false);
        bool any_on = false;
        for (int i = 0; i < NUM_BUTTONS; i++) {
            if (g_buttons[i].module_type != MOD_NONE && g_button_state[i] > 0) {
                any_on = true;
                break;
            }
        }
        lv_label_set_text(objects.lbl_all_on_off, any_on ? "All Off" : "All On");
    }

    if (g_temperature_updated) {
        g_temperature_updated = false;
        int32_t temp_f = (int32_t)g_interior_temp_f;

        // vars.c setter paints the home thermostat + air quality temperature
        // tile in the user's chosen unit.
        set_var_current_interior_temperature(temp_f);

        // Humidity — PageAirQuality tile (legacy PageTrailer arcs are gone)
        int hum_whole = g_humidity_raw / 100;
        lv_label_set_text_fmt(objects.label_air_quality_humdity_value, "%d", hum_whole);

        // Borealis sensor data: TVOC + eCO2 (bytes 4-7 of the 8-byte payload)
        lv_label_set_text_fmt(objects.label_air_quality_tvoc_value, "%u",
                              (unsigned)g_tvoc_ppb);
        lv_label_set_text_fmt(objects.label_air_quality_co2_value, "%u",
                              (unsigned)g_eco2_ppm);
    }

    if (g_gps_sat_updated) {
        g_gps_sat_updated = false;
        lv_label_set_text_fmt(objects.label_number_of_sats_value, "%d", (int)g_gps_num_sats);
        set_var_satellite_count((int32_t)g_gps_num_sats);
        const char *mode_str;
        switch (g_gps_gnss_mode) {
            case 1: mode_str = "Gps";                    break;
            case 2: mode_str = "Beidou";                 break;
            case 3: mode_str = "Gps + Beidou";           break;
            case 4: mode_str = "Glonass";                break;
            case 5: mode_str = "Gps + Glonass";          break;
            case 6: mode_str = "Beidou + Glonass";       break;
            case 7: mode_str = "Gps + Beidou + Glonass"; break;
            default: mode_str = "No Fix";                break;
        }
        lv_label_set_text(objects.label_gps_mode_value, mode_str);
    }

    if (g_gps_alt_updated) {
        g_gps_alt_updated = false;
        double alt_m = (double)g_gps_altitude_raw * 0.01;
        int alt_ft = (int)(alt_m * 3.28084);
        lv_label_set_text_fmt(objects.label_elevation_value, "%d", alt_ft);
    }

    if (g_gps_latlon_updated) {
        g_gps_latlon_updated = false;
        // %+.4f: explicit leading sign, 4 decimal places (matches Bearing's
        // × 10000 scaling). Max width: "-180.0000" = 9 chars.
        lv_label_set_text_fmt(objects.label_latitude_value,  "%+.4f", (double)g_gps_latitude);
        lv_label_set_text_fmt(objects.label_longitude_value, "%+.4f", (double)g_gps_longitude);
    }

    if (g_batt_shunt1_updated) {
        g_batt_shunt1_updated = false;
        lv_label_set_text_fmt(objects.label_battery_voltage_value, "%d.%02d",
            (int)g_batt_voltage_whole, (int)g_batt_voltage_dec);
        int soc = (int)g_batt_soc_whole;
        lv_label_set_text_fmt(objects.label_battery_soc_percent, "%d", soc);
        lv_bar_set_value(objects.bar_battery_soc, soc, LV_ANIM_OFF);
    }

    if (g_batt_shunt2_updated) {
        g_batt_shunt2_updated = false;
        int watts = (int)g_shunt_wattage;
        if (g_is_wattage_negative == 0xFF) watts = -watts;
        lv_label_set_text_fmt(objects.label_battery_load_watts, "%d", watts);
        uint16_t ttg = g_time_to_go_min;
        if (ttg == 0xFFFF || ttg == 0) {
            lv_label_set_text(objects.label_battery_time_to_go_hours, "-");
            lv_label_set_text(objects.label_time_to_go_measurement_type, "");
        } else {
            int hours = ttg / 60;
            int mins = ttg % 60;
            lv_label_set_text_fmt(objects.label_battery_time_to_go_hours, "%d:%02d", hours, mins);
            lv_label_set_text(objects.label_time_to_go_measurement_type, "Hrs");
        }
        int arc_val = (ttg > 2000) ? 2000 : (int)ttg;
        lv_arc_set_value(objects.power_arc_remaining_hours, arc_val);
    }

    if (g_solar_mppt1_updated) {
        g_solar_mppt1_updated = false;
        lv_label_set_text_fmt(objects.label_solar_power_watts, "%d", (int)g_solar_wattage);
        const char *charge_str;
        switch (g_solar_charge_status) {
            case 0: charge_str = "Off";        break;
            case 2: charge_str = "Fault";      break;
            case 3: charge_str = "Bulk";       break;
            case 4: charge_str = "Absorption"; break;
            case 5: charge_str = "Float";      break;
            default: charge_str = "Unknown";   break;
        }
        lv_label_set_text(objects.label_solar_charge_state, charge_str);
    }

    // Water tank levels from Reservoir (CAN 0x3E).
    // Reservoir transmits every 1000ms; if 3 expected intervals pass with no
    // frame, treat the data as stale and show "- %" / empty bars.
    static int64_t s_water_last_rx_ms = 0;
    static bool    s_water_data_valid = false;
    const int64_t  WATER_TIMEOUT_MS = 3000;
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (g_water_levels_updated) {
        g_water_levels_updated = false;
        s_water_last_rx_ms = now_ms;
        s_water_data_valid = true;
        lv_bar_set_value(objects.bar_fresh_water_value, g_fresh_water_level, LV_ANIM_OFF);
        lv_bar_set_value(objects.bar_grey_water_value,  g_grey_water_level,  LV_ANIM_OFF);
        lv_bar_set_value(objects.bar_black_water_value, g_black_water_level, LV_ANIM_OFF);
        lv_label_set_text_fmt(objects.label_fresh_water_value, "%u%%", (unsigned)g_fresh_water_level);
        lv_label_set_text_fmt(objects.label_grey_water_value,  "%u%%", (unsigned)g_grey_water_level);
        lv_label_set_text_fmt(objects.label_black_water_value, "%u%%", (unsigned)g_black_water_level);
    } else if (s_water_data_valid && (now_ms - s_water_last_rx_ms) > WATER_TIMEOUT_MS) {
        s_water_data_valid = false;
        ESP_LOGW(TAG, "Water tank data stale (>%lldms since last 0x3E), invalidating",
                 WATER_TIMEOUT_MS);
        lv_bar_set_value(objects.bar_fresh_water_value, 0, LV_ANIM_OFF);
        lv_bar_set_value(objects.bar_grey_water_value,  0, LV_ANIM_OFF);
        lv_bar_set_value(objects.bar_black_water_value, 0, LV_ANIM_OFF);
        lv_label_set_text(objects.label_fresh_water_value, "- %");
        lv_label_set_text(objects.label_grey_water_value,  "- %");
        lv_label_set_text(objects.label_black_water_value, "- %");
    }

    // Save settings to NVS when changed
    if (get_var_user_settings_changed()) {
        nvs_set_i32(nvs_settings, "brightness", desired_brightness);
        nvs_set_i32(nvs_settings, "timeout", get_var_screen_timeout_value());
        nvs_set_i32(nvs_settings, "theme", get_var_selected_theme());
        nvs_set_i32(nvs_settings, "tzIndex", s_tz_index);
        nvs_set_i32(nvs_settings, "desiredTemp", get_var_desired_temperature());
        nvs_set_i32(nvs_settings, "tempUnit", get_var_temperature_unit());
        nvs_commit(nvs_settings);
        set_var_user_settings_changed(false);
    }
}

// ============================================================================
// app_main
// ============================================================================
void app_main(void)
{
    // WiFi config (handles NVS flash init and hostname from MAC)
    ESP_ERROR_CHECK(wifi_config_init());
    wifi_config_load();
    ESP_ERROR_CHECK(nvs_open("settings", NVS_READWRITE, &nvs_settings));

    // OTA & Discovery init
    ota_init();
    discovery_init();

    // Hardware init (IO extension first — LCD and touch need it for reset)
    io_ext_init();
    lcd_init();
    touch_init();
    lvgl_init();

    // Confirm firmware is good (OTA rollback protection)
    esp_ota_mark_app_valid_cancel_rollback();

    // Load button configuration BEFORE ui_init so apply_to_ui has real labels
    // to write. ui_init creates the widgets — apply runs right after.
    button_config_init();

    // EEZ Studio UI
    ui_init();
    button_config_apply_to_ui();
    extern void ui_bind_button_edit_keyboard(void);
    ui_bind_button_edit_keyboard();

    // Populate About screen: firmware version and MAC address
    {
        const esp_app_desc_t *app = esp_app_get_description();
        lv_label_set_text(objects.label_version_number, app->version);

        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        lv_label_set_text(objects.mcu_mac_address_value, mac_str);
    }

    // Load saved settings
    int32_t saved_brightness = 255, saved_timeout = 5, saved_theme = 0;
    nvs_get_i32(nvs_settings, "brightness", &saved_brightness);
    nvs_get_i32(nvs_settings, "timeout", &saved_timeout);
    nvs_get_i32(nvs_settings, "theme", &saved_theme);

    // Load saved timezone index (default: New York)
    int32_t saved_tz = 5;
    nvs_get_i32(nvs_settings, "tzIndex", &saved_tz);
    s_tz_index = saved_tz;
    apply_user_timezone();
    lv_dropdown_set_selected(objects.drop_down_selected_time_zone, (uint16_t)saved_tz);

    // Load saved temperature unit (default: Fahrenheit)
    int32_t saved_unit = 0;
    nvs_get_i32(nvs_settings, "tempUnit", &saved_unit);
    set_var_temperature_unit(saved_unit);

    // Load from NVS via set_backlight so the clamp applies — protects
    // against a corrupt value (e.g. a stuck-at-0 from an earlier build)
    // locking the screen dark on boot.
    set_backlight((uint8_t)saved_brightness);
    int slider_pct = (desired_brightness * 100) / 255;
    lv_slider_set_value(objects.slider_screen_brightness, slider_pct, LV_ANIM_OFF);

    set_var_screen_timeout_value(saved_timeout);
    lv_label_set_text_fmt(objects.label_screen_timeout_value, "%d", (int)saved_timeout);

    if (saved_theme != 0) {
        change_color_theme((uint32_t)saved_theme);
        lv_obj_clear_state(objects.btn_theme_light, LV_STATE_CHECKED);
        lv_obj_add_state(objects.btn_theme_dark, LV_STATE_CHECKED);
    }
    set_var_selected_theme(saved_theme);

    // Load home screen instantly (override EEZ fade animation)
    lv_disp_load_scr(objects.page_home);

    // Default all CAN-sourced labels to "-". PageAirQuality tiles are
    // now on the Fireside-style 4-box layout (temp / humidity / TVOC / CO2).
    lv_label_set_text(objects.label_air_quality_temperature_value, "--");
    lv_label_set_text(objects.label_air_quality_humdity_value, "--");
    lv_label_set_text(objects.label_air_quality_tvoc_value, "--");
    lv_label_set_text(objects.label_air_quality_co2_value, "--");
    lv_label_set_text(objects.label_elevation_value, "-");
    lv_label_set_text(objects.label_latitude_value,  "+0.0000");
    lv_label_set_text(objects.label_longitude_value, "+0.0000");
    lv_label_set_text(objects.label_number_of_sats_value, "-");
    lv_label_set_text(objects.label_gps_mode_value, "-");
    lv_label_set_text(objects.label_front_level_value, "-");
    lv_label_set_text(objects.label_back_level_value, "-");
    lv_label_set_text(objects.label_left_side_level_value, "-");
    lv_label_set_text(objects.label_right_side_level_value, "-");
    lv_label_set_text(objects.label_battery_soc_percent, "-");
    lv_label_set_text(objects.label_battery_voltage_value, "-");
    lv_label_set_text(objects.label_battery_time_to_go_hours, "-");
    lv_label_set_text(objects.label_solar_power_watts, "-");
    lv_label_set_text(objects.label_solar_charge_state, "-");
    lv_label_set_text(objects.label_battery_load_watts, "-");
    lv_label_set_text(objects.lbl_all_on_off, "All On");

    // Clock placeholders until Bearing sends CAN 0x06
    lv_label_set_text(objects.lbl_time_hour,    "--");
    lv_label_set_text(objects.lbl_time_minutes, "--");
    lv_label_set_text(objects.lbl_time_am_pm,   "--");
    lv_label_set_text(objects.lbl_date,         "-");

    // Thermostat defaults — desired setpoint is a local UI-only value
    // until an HVAC module is wired onto the CAN bus. Current interior
    // temperature starts blank until CAN 0x1F arrives.
    int32_t saved_desired = 72;
    nvs_get_i32(nvs_settings, "desiredTemp", &saved_desired);
    if (saved_desired < 35 || saved_desired > 100) saved_desired = 72;
    set_var_desired_temperature(saved_desired);
    lv_arc_set_value(objects.arc_thermostat, saved_desired);
    lv_label_set_text_fmt(objects.label_desired_temperature_value, "%d", (int)saved_desired);
    lv_label_set_text(objects.label_current_interior_temperature, "-");

    // Start CAN (switches GPIO19/20 from USB to CAN transceiver)
    can_init();

    ESP_LOGI(TAG, "Initialization complete");

    // Main loop
    extern enum ScreensEnum get_active_screen_id(void);
    while (1) {
        lv_timer_handler();
        tick_screen_by_id(get_active_screen_id());
        update_ui_from_can();
        handle_screen_timeout();
        ota_update_ui();
        discovery_update_ui();
        wifi_config_check_timeout();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
