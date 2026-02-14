#include <Arduino.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Preferences.h>

#include "ui/ui.h"
#include "ui/vars.h"

// Display resolution
static const uint16_t SCREEN_WIDTH = 800;
static const uint16_t SCREEN_HEIGHT = 480;

// Backlight pin and PWM config
#define TFT_BL 2
#define BL_LEDC_FREQ    5000
#define BL_LEDC_BITS    8

// NVM storage
Preferences preferences;

// Backlight state
static uint8_t current_brightness = 64;

// Screen timeout state
static unsigned long last_touch_ms = 0;
static bool screen_timed_out = false;

// Touch configuration
#define TOUCH_SDA 19
#define TOUCH_SCL 20
#define TOUCH_RST 38
// GT911 registers
#define GT911_POINT_INFO 0x814E
#define GT911_POINT1     0x8150
#define GT911_CONFIG_START 0x8047

// ============================================================================
// Arduino_GFX display configuration for ESP32-8048S070
// ============================================================================
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    41 /* DE */, 40 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
    14 /* R0 */, 21 /* R1 */, 47 /* R2 */, 48 /* R3 */, 45 /* R4 */,
    9 /* G0 */, 46 /* G1 */, 3 /* G2 */, 8 /* G3 */, 16 /* G4 */, 1 /* G5 */,
    15 /* B0 */, 7 /* B1 */, 6 /* B2 */, 5 /* B3 */, 4 /* B4 */,
    0 /* hsync_polarity */, 210 /* hsync_front_porch */, 30 /* hsync_pulse_width */, 16 /* hsync_back_porch */,
    0 /* vsync_polarity */, 22 /* vsync_front_porch */, 13 /* vsync_pulse_width */, 10 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 16632000 /* prefer_speed ~30fps */,
    false /* useBigEndian */, 0 /* de_idle_high */, 0 /* pclk_idle_high */,
    SCREEN_WIDTH * 10 /* bounce_buffer_size_px */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_WIDTH, SCREEN_HEIGHT, rgbpanel, 0 /* rotation */, false /* auto_flush */);

// ============================================================================
// LVGL draw buffer (direct mode — renders into DMA framebuffer, no copy)
// ============================================================================
static lv_disp_draw_buf_t draw_buf;

// ============================================================================
// GT911 touch driver (direct I2C, no INT pin needed)
// ============================================================================
static bool gt911_ready = false;
static uint8_t gt911_addr = 0x5D;

static bool gt911_write_reg(uint16_t reg, const uint8_t *data, uint8_t len) {
    Wire.beginTransmission(gt911_addr);
    Wire.write(highByte(reg));
    Wire.write(lowByte(reg));
    for (uint8_t i = 0; i < len; i++) Wire.write(data[i]);
    return Wire.endTransmission() == 0;
}

static bool gt911_read_reg(uint16_t reg, uint8_t *data, uint8_t len) {
    Wire.beginTransmission(gt911_addr);
    Wire.write(highByte(reg));
    Wire.write(lowByte(reg));
    if (Wire.endTransmission() != 0) return false;
    Wire.requestFrom((uint8_t)gt911_addr, len);
    for (uint8_t i = 0; i < len; i++) {
        if (!Wire.available()) return false;
        data[i] = Wire.read();
    }
    return true;
}

static void gt911_init() {
    // Hardware reset the GT911 before I2C init
    // Without INT pin control, GT911 defaults to address 0x5D
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(100);

    // Initialize I2C
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(400000);
    delay(50);

    // Try address 0x5D first (default when INT pin is floating)
    uint8_t cfg;
    gt911_addr = 0x5D;
    if (gt911_read_reg(GT911_CONFIG_START, &cfg, 1)) {
        Serial.printf("GT911 found at 0x%02X, config version: 0x%02X\n", gt911_addr, cfg);
        gt911_ready = true;
        return;
    }
    // Try address 0x14
    gt911_addr = 0x14;
    if (gt911_read_reg(GT911_CONFIG_START, &cfg, 1)) {
        Serial.printf("GT911 found at 0x%02X, config version: 0x%02X\n", gt911_addr, cfg);
        gt911_ready = true;
        return;
    }
    Serial.println("GT911 not found at 0x5D or 0x14, touch disabled");
    gt911_ready = false;
}

static bool gt911_read_touch(uint16_t *x, uint16_t *y) {
    if (!gt911_ready) return false;

    uint8_t point_info;
    if (!gt911_read_reg(GT911_POINT_INFO, &point_info, 1)) return false;

    uint8_t touch_count = point_info & 0x0F;
    bool buffer_ready = (point_info & 0x80) != 0;

    if (buffer_ready) {
        // Clear the buffer status
        uint8_t zero = 0;
        gt911_write_reg(GT911_POINT_INFO, &zero, 1);

        if (touch_count > 0) {
            uint8_t point_data[4];
            if (!gt911_read_reg(GT911_POINT1, point_data, 4)) return false;
            *x = (uint16_t)point_data[0] | ((uint16_t)point_data[1] << 8);
            *y = (uint16_t)point_data[2] | ((uint16_t)point_data[3] << 8);
            return true;
        }
    }
    return false;
}

// ============================================================================
// Backlight control
// ============================================================================
void set_backlight(uint8_t brightness) {
    current_brightness = brightness;
    if (!screen_timed_out) {
        ledcWrite(TFT_BL, brightness);
    }
}

uint8_t get_backlight() {
    return current_brightness;
}

// ============================================================================
// LVGL display flush callback (direct mode — zero copy)
// ============================================================================
static void lvgl_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                            lv_color_t *color_p) {
    // LVGL renders multiple dirty regions per frame in direct mode.
    // Only flush CPU cache after the last region so the entire frame
    // appears atomically (bounce buffer ISR won't see partial updates).
    if (lv_disp_flush_is_last(disp_drv)) {
        gfx->flush();
    }
    lv_disp_flush_ready(disp_drv);
}

// ============================================================================
// LVGL touch read callback
// ============================================================================
static void lvgl_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    uint16_t touch_x, touch_y;
    if (gt911_read_touch(&touch_x, &touch_y)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touch_x;
        data->point.y = touch_y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ============================================================================
// LVGL log callback
// ============================================================================
#if LV_USE_LOG
static void lvgl_log_cb(const char *buf) {
    Serial.printf("[LVGL] %s\n", buf);
    Serial.flush();
}
#endif

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("ESP32-8048S070 starting...");

    // Initialize display
    gfx->begin();

    // Backlight via PWM
    ledcAttach(TFT_BL, BL_LEDC_FREQ, BL_LEDC_BITS);
    ledcWrite(TFT_BL, current_brightness);

    // Initialize touch (GT911 via I2C, polling mode)
    gt911_init();

    // Initialize LVGL
    lv_init();

#if LV_USE_LOG
    lv_log_register_print_cb(lvgl_log_cb);
#endif

    // Direct mode: LVGL renders into the RGB panel's DMA framebuffer.
    // Bounce buffers ensure DMA reads from SRAM, not PSRAM, so no contention.
    uint16_t *fb = gfx->getFramebuffer();
    if (!fb) {
        Serial.println("Failed to get RGB framebuffer!");
        return;
    }
    uint32_t buf_size = SCREEN_WIDTH * SCREEN_HEIGHT;
    lv_disp_draw_buf_init(&draw_buf, (lv_color_t *)fb, NULL, buf_size);

    // Register display driver in direct mode (zero copy)
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res     = SCREEN_WIDTH;
    disp_drv.ver_res     = SCREEN_HEIGHT;
    disp_drv.flush_cb    = lvgl_disp_flush;
    disp_drv.draw_buf    = &draw_buf;
    disp_drv.direct_mode = 1;
    lv_disp_drv_register(&disp_drv);

    // Register touch input driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Initialize the EEZ Studio UI
    ui_init();

    // Load saved settings from NVM
    preferences.begin("settings", false);
    current_brightness = (uint8_t)preferences.getInt("brightness", 64);
    int32_t saved_timeout = preferences.getInt("timeout", 5);
    int32_t saved_theme = preferences.getInt("theme", 0);

    // Apply loaded brightness
    ledcWrite(TFT_BL, current_brightness);
    int slider_pct = map(current_brightness, 0, 255, 0, 100);
    lv_slider_set_value(objects.slider_screen_brightness, slider_pct, LV_ANIM_OFF);

    // Apply loaded timeout
    set_var_screen_timeout_value(saved_timeout);
    lv_label_set_text_fmt(objects.label_screen_timeout_value, "%d", (int)saved_timeout);

    // Apply loaded theme
    if (saved_theme != 0) {
        change_color_theme((uint32_t)saved_theme);
        lv_obj_clear_state(objects.btn_theme_light, LV_STATE_CHECKED);
        lv_obj_add_state(objects.btn_theme_dark, LV_STATE_CHECKED);
    }
    set_var_selected_theme(saved_theme);

    last_touch_ms = millis();

    // Override the fade animation from ui_init with an instant load
    lv_disp_load_scr(objects.page_home);

    Serial.println("UI initialized successfully");
}

// ============================================================================
// Main loop
// ============================================================================
void loop() {
    lv_timer_handler();
    extern enum ScreensEnum get_active_screen_id();
    tick_screen_by_id(get_active_screen_id());

    // Save settings to NVM when changed
    if (get_var_user_settings_changed()) {
        preferences.putInt("brightness", current_brightness);
        preferences.putInt("timeout", get_var_screen_timeout_value());
        preferences.putInt("theme", get_var_selected_theme());
        set_var_user_settings_changed(false);
    }

    // Screen timeout logic
    int32_t timeout_min = get_var_screen_timeout_value();
    if (timeout_min > 0) {
        // Check for touch activity via LVGL inactivity tracker
        uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
        if (inactive_ms < 1000) {
            last_touch_ms = millis();
            if (screen_timed_out) {
                screen_timed_out = false;
                ledcWrite(TFT_BL, current_brightness);
            }
        }

        unsigned long elapsed_ms = millis() - last_touch_ms;
        if (!screen_timed_out && elapsed_ms >= (unsigned long)timeout_min * 60000UL) {
            screen_timed_out = true;
            ledcWrite(TFT_BL, 0);
        }
    } else if (screen_timed_out) {
        // Timeout was disabled, wake up
        screen_timed_out = false;
        ledcWrite(TFT_BL, current_brightness);
    }
}
