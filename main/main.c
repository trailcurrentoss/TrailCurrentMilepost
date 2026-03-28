#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include "driver/twai.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "lvgl.h"

#include "ui/ui.h"
#include "include/ota.h"
#include "include/discovery.h"
#include "include/wifi_config.h"
#include "ui/vars.h"
#include "ui/styles.h"

static const char *TAG = "milepost";

// Display resolution
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 480

// ============================================================================
// CH422G IO expander
// Uses different I2C addresses per function (no register addressing).
//   0x24 = system parameter (write IO_OE=1 to enable push-pull EXIO outputs)
//   0x38 = IO0-7 output data
// Pin mapping within the 0x38 output byte:
//   Bit 1 = EXIO1 = Touch RST
//   Bit 2 = EXIO2 = Backlight enable
//   Bit 4 = EXIO4 = SD_CS
//   Bit 5 = EXIO5 = CAN_SEL (high=CAN, low=USB)
// ============================================================================
#define CH422G_SYS_ADDR   0x24
#define CH422G_OUT_ADDR   0x38

#define CH422G_EXIO1_BIT  (1 << 1)   // Touch RST
#define CH422G_EXIO2_BIT  (1 << 2)   // Backlight enable
#define CH422G_EXIO5_BIT  (1 << 5)   // CAN_SEL

#define I2C_PORT     I2C_NUM_0
#define I2C_SDA_PIN  8
#define I2C_SCL_PIN  9
#define I2C_FREQ_HZ  400000

static uint8_t ch422g_out = 0;

static esp_err_t ch422g_write(uint8_t addr, uint8_t val)
{
    uint8_t buf = val;
    return i2c_master_write_to_device(I2C_PORT, addr, &buf, 1, pdMS_TO_TICKS(100));
}

static void ch422g_set_bit(uint8_t bit, bool high)
{
    if (high) ch422g_out |= bit;
    else      ch422g_out &= ~bit;
    ch422g_write(CH422G_OUT_ADDR, ch422g_out);
}

static void ch422g_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

    // Enable push-pull output mode for EXIO pins (IO_OE = bit 0)
    esp_err_t err = ch422g_write(CH422G_SYS_ADDR, 0x01);
    ESP_LOGI(TAG, "CH422G init %s", err == ESP_OK ? "OK" : "FAILED");

    // Backlight on, CAN_SEL=0 (USB mode), touch RST low
    ch422g_out = CH422G_EXIO2_BIT;
    ch422g_write(CH422G_OUT_ADDR, ch422g_out);
}

// ============================================================================
// Backlight (digital on/off via CH422G EXIO2, no PWM)
// ============================================================================
static uint8_t current_brightness = 255;
static bool screen_timed_out = false;
static lv_obj_t *dimming_overlay = NULL;

// Map brightness (0-255) to overlay opacity.
// 255 = full bright (overlay invisible), 0 = screen off (backlight off).
// Values 1-255 produce a software dim via a black overlay.
static void apply_brightness(uint8_t brightness)
{
    if (brightness == 0) {
        // Fully off — kill backlight to save power
        ch422g_set_bit(CH422G_EXIO2_BIT, false);
        return;
    }
    // Ensure backlight is on
    ch422g_set_bit(CH422G_EXIO2_BIT, true);

    if (dimming_overlay) {
        // Invert: brightness 255 = fully transparent, brightness 1 = nearly opaque
        lv_opa_t opa = (lv_opa_t)(255 - brightness);
        lv_obj_set_style_bg_opa(dimming_overlay, opa, 0);
    }
}

void set_backlight(uint8_t brightness)
{
    current_brightness = brightness;
    if (!screen_timed_out) {
        apply_brightness(brightness);
    }
}

uint8_t get_backlight(void)
{
    return current_brightness;
}

static void create_dimming_overlay(void)
{
    dimming_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(dimming_overlay);
    lv_obj_set_size(dimming_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(dimming_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dimming_overlay, LV_OPA_TRANSP, 0);
    // Let touch events pass through to widgets underneath
    lv_obj_add_flag(dimming_overlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(dimming_overlay, LV_OBJ_FLAG_CLICKABLE);
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

static void lcd_init(void)
{
    vsync_sem = xSemaphoreCreateBinary();

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 21000000,
            .h_res = SCREEN_WIDTH,
            .v_res = SCREEN_HEIGHT,
            .hsync_pulse_width = 30,
            .hsync_back_porch = 16,
            .hsync_front_porch = 210,
            .vsync_pulse_width = 13,
            .vsync_back_porch = 10,
            .vsync_front_porch = 22,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = SCREEN_WIDTH * 20,
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

static void touch_init(void)
{
    // Pulse touch RST via CH422G EXIO1
    ch422g_set_bit(CH422G_EXIO1_BIT, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    ch422g_set_bit(CH422G_EXIO1_BIT, true);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_PORT, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = SCREEN_WIDTH,
        .y_max = SCREEN_HEIGHT,
        .rst_gpio_num = -1,   // RST handled via CH422G above
        .int_gpio_num = 4,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle));
    ESP_LOGI(TAG, "GT911 touch initialized");
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
#define CAN_ID_GPS_SAT_SPEED      0x07
#define CAN_ID_GPS_ALTITUDE  0x08
#define CAN_ID_TOGGLE        0x18
#define CAN_ID_STATUS        0x1B
#define CAN_ID_TEMPERATURE   0x1F
#define CAN_ID_BATT_SHUNT1   0x23
#define CAN_ID_BATT_SHUNT2   0x24
#define CAN_ID_SOLAR_MPPT1   0x2C

// Device state from PDM (updated from CAN RX task)
volatile uint8_t g_device_pwm[8] = {0};
volatile bool g_device_status_updated = false;

// Temperature & humidity (CAN ID 0x1F)
volatile uint8_t  g_interior_temp_f = 0;
volatile int8_t   g_interior_temp_c = 0;
volatile uint16_t g_humidity_raw = 0;
volatile bool g_temperature_updated = false;

// GPS (CAN IDs 0x07, 0x08)
volatile uint8_t  g_gps_num_sats = 0;
volatile uint8_t  g_gps_gnss_mode = 0;
volatile uint32_t g_gps_altitude_raw = 0;
volatile bool g_gps_sat_updated = false;
volatile bool g_gps_alt_updated = false;

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


static void can_rx_task(void *arg)
{
    twai_message_t msg;
    while (1) {
        if (twai_receive(&msg, pdMS_TO_TICKS(100)) != ESP_OK) continue;

        if (msg.identifier == CAN_ID_OTA_TRIGGER) {
            ota_handle_trigger(msg.data, msg.data_length_code);
            continue;
        }

        if (msg.identifier == CAN_ID_WIFI_CONFIG) {
            wifi_config_handle_can(msg.data, msg.data_length_code);
            continue;
        }

        if (msg.identifier == CAN_ID_DISCOVERY_TRIGGER) {
            discovery_handle_trigger();
            continue;
        }

        if (msg.identifier == CAN_ID_STATUS && msg.data_length_code == 8) {
            for (int i = 0; i < 8; i++) g_device_pwm[i] = msg.data[i];
            g_device_status_updated = true;
        } else if (msg.identifier == CAN_ID_TEMPERATURE && msg.data_length_code >= 4) {
            g_interior_temp_c = (int8_t)msg.data[0];
            g_interior_temp_f = msg.data[1];
            g_humidity_raw = ((uint16_t)msg.data[2] << 8) | msg.data[3];
            g_temperature_updated = true;
        } else if (msg.identifier == CAN_ID_GPS_SAT_SPEED && msg.data_length_code >= 6) {
            g_gps_num_sats = msg.data[0];
            g_gps_gnss_mode = msg.data[5];
            g_gps_sat_updated = true;
        } else if (msg.identifier == CAN_ID_GPS_ALTITUDE && msg.data_length_code >= 4) {
            g_gps_altitude_raw = ((uint32_t)msg.data[0] << 24) |
                                 ((uint32_t)msg.data[1] << 16) |
                                 ((uint32_t)msg.data[2] << 8) |
                                 msg.data[3];
            g_gps_alt_updated = true;
        } else if (msg.identifier == CAN_ID_BATT_SHUNT1 && msg.data_length_code >= 7) {
            g_batt_voltage_whole = msg.data[0];
            g_batt_voltage_dec   = msg.data[1];
            g_batt_soc_whole     = msg.data[5];
            g_batt_soc_dec       = msg.data[6];
            g_batt_shunt1_updated = true;
        } else if (msg.identifier == CAN_ID_BATT_SHUNT2 && msg.data_length_code >= 5) {
            g_is_wattage_negative = msg.data[0];
            g_shunt_wattage = ((uint16_t)msg.data[1] << 8) | msg.data[2];
            g_time_to_go_min = ((uint16_t)msg.data[3] << 8) | msg.data[4];
            g_batt_shunt2_updated = true;
        } else if (msg.identifier == CAN_ID_SOLAR_MPPT1 && msg.data_length_code >= 7) {
            g_solar_wattage = ((uint16_t)msg.data[2] << 8) | msg.data[3];
            g_solar_charge_status = msg.data[6];
            g_solar_mppt1_updated = true;
        }
    }
}

static void can_init(void)
{
    // Switch GPIO19/20 from USB to CAN transceiver
    ESP_LOGI(TAG, "Switching to CAN mode");
    ch422g_set_bit(CH422G_EXIO5_BIT, true);
    vTaskDelay(pdMS_TO_TICKS(5));

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NO_ACK);
    g_config.rx_queue_len = 32;
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK &&
        twai_start() == ESP_OK) {
        ESP_LOGI(TAG, "TWAI started on TX=%d RX=%d at 500kbps", CAN_TX_PIN, CAN_RX_PIN);
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
    static bool prev_on[8] = {false};

    lv_obj_t *indicators[8] = {
        objects.lbl_device01_status_ind,
        objects.lbl_device02_status_ind,
        objects.lbl_device03_status_ind,
        objects.lbl_device04_status_ind,
        objects.lbl_device05_status_ind,
        objects.lbl_device06_status_ind,
        objects.lbl_device07_status_ind,
        objects.lbl_device08_status_ind,
    };

    for (int i = 0; i < 8; i++) {
        bool is_on = g_device_pwm[i] > 0;
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
// Screen timeout
// ============================================================================
static int64_t last_touch_us = 0;

static void handle_screen_timeout(void)
{
    int32_t timeout_min = get_var_screen_timeout_value();
    if (timeout_min > 0) {
        uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
        if (inactive_ms < 1000) {
            last_touch_us = esp_timer_get_time();
            if (screen_timed_out) {
                screen_timed_out = false;
                apply_brightness(current_brightness);
            }
        }
        int64_t elapsed_us = esp_timer_get_time() - last_touch_us;
        if (!screen_timed_out && elapsed_us >= (int64_t)timeout_min * 60000000LL) {
            screen_timed_out = true;
            apply_brightness(0);
        }
    } else if (screen_timed_out) {
        screen_timed_out = false;
        apply_brightness(current_brightness);
    }
}

// ============================================================================
// UI data update (called each loop iteration)
// ============================================================================
static void update_ui_from_can(void)
{
    if (g_device_status_updated) {
        g_device_status_updated = false;
        update_device_status_indicators(false);
        bool any_on = false;
        for (int i = 0; i < 8; i++) {
            if (g_device_pwm[i] > 0) { any_on = true; break; }
        }
        lv_label_set_text(objects.lbl_all_on_off, any_on ? "All Off" : "All On");
    }

    if (g_temperature_updated) {
        g_temperature_updated = false;
        int32_t temp_f = (int32_t)g_interior_temp_f;
        set_var_current_interior_temperature(temp_f);
        lv_label_set_text_fmt(objects.label_current_interior_temperature, "%d", (int)temp_f);
        lv_label_set_text_fmt(objects.label_temp_fahrenheit_value, "%d", (int)temp_f);
        int arc_temp = (temp_f < 0) ? 0 : ((temp_f > 130) ? 130 : (int)temp_f);
        lv_arc_set_value(objects.arc_temperature, arc_temp);
        int celsius = (int)g_interior_temp_c;
        lv_label_set_text_fmt(objects.label_temp_celsius_value, "%d \u00b0C", celsius);
        int hum_whole = g_humidity_raw / 100;
        int hum_frac  = (g_humidity_raw % 100) / 10;
        lv_label_set_text_fmt(objects.label_humidity_value, "%d.%d", hum_whole, hum_frac);
        int arc_hum = (hum_whole > 100) ? 100 : hum_whole;
        lv_arc_set_value(objects.arc_humidity, arc_hum);
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

    if (g_batt_shunt1_updated) {
        g_batt_shunt1_updated = false;
        lv_label_set_text_fmt(objects.label_battery_voltage, "%d.%02d",
            (int)g_batt_voltage_whole, (int)g_batt_voltage_dec);
        int soc = (int)g_batt_soc_whole;
        lv_label_set_text_fmt(objects.label_power_battery_percentage, "%d", soc);
        lv_bar_set_value(objects.bar_battery_soc, soc, LV_ANIM_OFF);
    }

    if (g_batt_shunt2_updated) {
        g_batt_shunt2_updated = false;
        int watts = (int)g_shunt_wattage;
        if (g_is_wattage_negative == 0xFF) watts = -watts;
        lv_label_set_text_fmt(objects.label_shunt_current_watts_used, "%d", watts);
        uint16_t ttg = g_time_to_go_min;
        if (ttg == 0xFFFF || ttg == 0) {
            lv_label_set_text(objects.label_power_remaining_time_to_go_value, "-");
            lv_label_set_text(objects.label_time_to_go_measurement_type, "");
        } else {
            int hours = ttg / 60;
            int mins = ttg % 60;
            lv_label_set_text_fmt(objects.label_power_remaining_time_to_go_value, "%d:%02d", hours, mins);
            lv_label_set_text(objects.label_time_to_go_measurement_type, "Hrs");
        }
        int arc_val = (ttg > 2000) ? 2000 : (int)ttg;
        lv_arc_set_value(objects.power_arc_remaining_hours, arc_val);
    }

    if (g_solar_mppt1_updated) {
        g_solar_mppt1_updated = false;
        lv_label_set_text_fmt(objects.label_solar_wattage, "%d", (int)g_solar_wattage);
        const char *charge_str;
        switch (g_solar_charge_status) {
            case 0: charge_str = "Off";        break;
            case 2: charge_str = "Fault";      break;
            case 3: charge_str = "Bulk";       break;
            case 4: charge_str = "Absorption"; break;
            case 5: charge_str = "Float";      break;
            default: charge_str = "Unknown";   break;
        }
        lv_label_set_text(objects.label_curent_charge_mode, charge_str);
    }

    // Save settings to NVS when changed
    if (get_var_user_settings_changed()) {
        nvs_set_i32(nvs_settings, "brightness", current_brightness);
        nvs_set_i32(nvs_settings, "timeout", get_var_screen_timeout_value());
        nvs_set_i32(nvs_settings, "theme", get_var_selected_theme());
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

    // Hardware init
    lcd_init();
    ch422g_init();
    touch_init();
    lvgl_init();

    // Confirm firmware is good (OTA rollback protection)
    esp_ota_mark_app_valid_cancel_rollback();

    // EEZ Studio UI
    ui_init();

    // Dimming overlay (must be created after ui_init so lv_layer_top exists)
    create_dimming_overlay();

    // Load saved settings
    int32_t saved_brightness = 255, saved_timeout = 5, saved_theme = 0;
    nvs_get_i32(nvs_settings, "brightness", &saved_brightness);
    nvs_get_i32(nvs_settings, "timeout", &saved_timeout);
    nvs_get_i32(nvs_settings, "theme", &saved_theme);

    current_brightness = (uint8_t)saved_brightness;
    set_backlight(current_brightness);
    int slider_pct = (current_brightness * 100) / 255;
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

    // Default all CAN-sourced labels to "-"
    lv_label_set_text(objects.label_current_interior_temperature, "-");
    lv_label_set_text(objects.label_current_exterior_temperature, "-");
    lv_label_set_text(objects.label_temp_fahrenheit_value, "-");
    lv_label_set_text(objects.label_temp_celsius_value, "- \u00b0C");
    lv_label_set_text(objects.label_humidity_value, "-");
    lv_arc_set_value(objects.arc_temperature, 0);
    lv_arc_set_value(objects.arc_humidity, 0);
    lv_label_set_text(objects.label_elevation_value, "-");
    lv_label_set_text(objects.label_number_of_sats_value, "-");
    lv_label_set_text(objects.label_gps_mode_value, "-");
    lv_label_set_text(objects.label_front_level_value, "-");
    lv_label_set_text(objects.label_back_level_value, "-");
    lv_label_set_text(objects.label_left_side_level_value, "-");
    lv_label_set_text(objects.label_right_side_level_value, "-");
    lv_label_set_text(objects.label_power_battery_percentage, "-");
    lv_label_set_text(objects.label_battery_voltage, "-");
    lv_label_set_text(objects.label_power_remaining_time_to_go_value, "-");
    lv_label_set_text(objects.label_solar_wattage, "-");
    lv_label_set_text(objects.label_curent_charge_mode, "-");
    lv_label_set_text(objects.label_shunt_current_watts_used, "-");
    lv_label_set_text(objects.lbl_all_on_off, "All On");

    last_touch_us = esp_timer_get_time();

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
