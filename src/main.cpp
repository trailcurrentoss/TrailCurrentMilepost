#include <Arduino.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Preferences.h>
#include <debug.h>
#include <TwaiTaskBased.h>

#include "ui/ui.h"
#include "ui/vars.h"
#include "ui/styles.h"

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

// CAN bus configuration
#define CAN_TX 17
#define CAN_RX 18
#define CAN_BAUDRATE 500000
#define CAN_ID_GPS_SAT_SPEED  0x07
#define CAN_ID_GPS_ALTITUDE   0x08
#define CAN_ID_TOGGLE         0x18
#define CAN_ID_STATUS         0x1B
#define CAN_ID_TEMPERATURE    0x1F
#define CAN_ID_BATT_SHUNT1    0x23
#define CAN_ID_BATT_SHUNT2    0x24
#define CAN_ID_SOLAR_MPPT1    0x2C

// Device state received from PDM (updated from CAN RX task)
volatile uint8_t g_device_pwm[8] = {0};
volatile bool g_device_status_updated = false;

// Temperature & humidity received from TempSensor (CAN ID 0x1F)
volatile uint8_t  g_interior_temp_f = 0;   // byte 1: Fahrenheit
volatile int8_t   g_interior_temp_c = 0;   // byte 0: Celsius
volatile uint16_t g_humidity_raw = 0;       // bytes 2-3: humidity * 100
volatile bool g_temperature_updated = false;

// GPS data received from GpsModule (CAN IDs 0x07, 0x08)
volatile uint8_t  g_gps_num_sats = 0;
volatile uint8_t  g_gps_gnss_mode = 0;   // 0=No Fix, 1=2D, 2=3D
volatile uint32_t g_gps_altitude_raw = 0; // raw value, scale 0.01 = meters
volatile bool g_gps_sat_updated = false;
volatile bool g_gps_alt_updated = false;

// Battery shunt data (CAN IDs 0x23, 0x24)
volatile uint8_t  g_batt_voltage_whole = 0;
volatile uint8_t  g_batt_voltage_dec = 0;
volatile uint8_t  g_batt_soc_whole = 0;
volatile uint8_t  g_batt_soc_dec = 0;
volatile bool     g_batt_shunt1_updated = false;

volatile uint8_t  g_is_wattage_negative = 0;
volatile uint16_t g_shunt_wattage = 0;
volatile uint16_t g_time_to_go_min = 0;
volatile bool     g_batt_shunt2_updated = false;

// Solar MPPT data (CAN ID 0x2C)
volatile uint16_t g_solar_wattage = 0;
volatile uint8_t  g_solar_charge_status = 0;
volatile bool     g_solar_mppt1_updated = false;

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
        debugf("GT911 found at 0x%02X, config version: 0x%02X\n", gt911_addr, cfg);
        gt911_ready = true;
        return;
    }
    // Try address 0x14
    gt911_addr = 0x14;
    if (gt911_read_reg(GT911_CONFIG_START, &cfg, 1)) {
        debugf("GT911 found at 0x%02X, config version: 0x%02X\n", gt911_addr, cfg);
        gt911_ready = true;
        return;
    }
    debugln("GT911 not found at 0x5D or 0x14, touch disabled");
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
// CAN bus receive callback (runs on Core 0 TWAI RX task)
// ============================================================================
static void can_rx_callback(const twai_message_t &msg) {
    if (msg.identifier == CAN_ID_STATUS && msg.data_length_code == 8) {
        for (int i = 0; i < 8; i++) {
            g_device_pwm[i] = msg.data[i];
        }
        g_device_status_updated = true;
    } else if (msg.identifier == CAN_ID_TEMPERATURE && msg.data_length_code >= 4) {
        g_interior_temp_c = (int8_t)msg.data[0];
        g_interior_temp_f = msg.data[1];
        g_humidity_raw = ((uint16_t)msg.data[2] << 8) | (uint16_t)msg.data[3];
        g_temperature_updated = true;
    } else if (msg.identifier == CAN_ID_GPS_SAT_SPEED && msg.data_length_code >= 6) {
        // Byte 0 = NumSatellitesUsed, Byte 5 = GnssMode (constellation combo 1-7)
        g_gps_num_sats = msg.data[0];
        g_gps_gnss_mode = msg.data[5];
        g_gps_sat_updated = true;
    } else if (msg.identifier == CAN_ID_GPS_ALTITUDE && msg.data_length_code >= 4) {
        // 32-bit big-endian altitude, scale 0.01 = meters
        g_gps_altitude_raw = ((uint32_t)msg.data[0] << 24) |
                             ((uint32_t)msg.data[1] << 16) |
                             ((uint32_t)msg.data[2] << 8)  |
                             ((uint32_t)msg.data[3]);
        g_gps_alt_updated = true;
    } else if (msg.identifier == CAN_ID_BATT_SHUNT1 && msg.data_length_code >= 7) {
        // BatteryShuntData1: voltage(2), current sign+value(3), SOC(2)
        g_batt_voltage_whole = msg.data[0];
        g_batt_voltage_dec   = msg.data[1];
        g_batt_soc_whole     = msg.data[5];
        g_batt_soc_dec       = msg.data[6];
        g_batt_shunt1_updated = true;
    } else if (msg.identifier == CAN_ID_BATT_SHUNT2 && msg.data_length_code >= 5) {
        // BatteryShuntData2: wattage sign(1), wattage(2), time-to-go(2)
        g_is_wattage_negative = msg.data[0];
        g_shunt_wattage = ((uint16_t)msg.data[1] << 8) | (uint16_t)msg.data[2];
        g_time_to_go_min = ((uint16_t)msg.data[3] << 8) | (uint16_t)msg.data[4];
        g_batt_shunt2_updated = true;
    } else if (msg.identifier == CAN_ID_SOLAR_MPPT1 && msg.data_length_code >= 7) {
        // SolarMpptData1: panelV(2), solarW(2), battV(2), chargeStatus(1)
        g_solar_wattage = ((uint16_t)msg.data[2] << 8) | (uint16_t)msg.data[3];
        g_solar_charge_status = msg.data[6];
        g_solar_mppt1_updated = true;
    }
}

// ============================================================================
// Update device button status indicators from CAN state.
// Uses the EEZ Studio on/off class styles so theme changes propagate
// automatically.  The generated change_color_theme() sets an inline
// text_color on every indicator (DEFAULT state, "off" colour).  We must
// remove that inline property so the class style wins.
// force=true re-applies all indicators (call after theme change).
// ============================================================================
void update_device_status_indicators(bool force) {
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
            // Remove inline text_color so the class style takes effect
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
// LVGL log callback
// ============================================================================
#if LV_USE_LOG
static void lvgl_log_cb(const char *buf) {
    debugf("[LVGL] %s\n", buf);
    Serial.flush();
}
#endif

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    debugln("ESP32-8048S070 starting...");

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
        debugln("Failed to get RGB framebuffer!");
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

    // Default all CAN-sourced data labels to "-" until real data arrives
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

    // Initialize CAN bus (TWAI) for PDM communication
    TwaiTaskBased::onReceive(can_rx_callback);
    if (TwaiTaskBased::begin((gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, CAN_BAUDRATE)) {
        debugln("TWAI initialized on TX=17, RX=18 at 500kbps");
    } else {
        debugln("TWAI initialization failed!");
    }

    debugln("UI initialized successfully");
}

// ============================================================================
// Main loop
// ============================================================================
void loop() {
    lv_timer_handler();
    extern enum ScreensEnum get_active_screen_id();
    tick_screen_by_id(get_active_screen_id());

    // Update device button indicators from CAN status
    if (g_device_status_updated) {
        g_device_status_updated = false;
        update_device_status_indicators(false);

        // Update All On/Off button label based on whether any device is on
        bool any_on = false;
        for (int i = 0; i < 8; i++) {
            if (g_device_pwm[i] > 0) { any_on = true; break; }
        }
        lv_label_set_text(objects.lbl_all_on_off, any_on ? "All Off" : "All On");
    }

    // Update temperature and humidity from CAN
    if (g_temperature_updated) {
        g_temperature_updated = false;

        // Fahrenheit — home page thermostat + air quality panel
        int32_t temp_f = (int32_t)g_interior_temp_f;
        set_var_current_interior_temperature(temp_f);
        lv_label_set_text_fmt(objects.label_current_interior_temperature,
            "%d", (int)temp_f);
        lv_label_set_text_fmt(objects.label_temp_fahrenheit_value, "%d", (int)temp_f);
        int arc_temp = (temp_f < 0) ? 0 : ((temp_f > 130) ? 130 : (int)temp_f);
        lv_arc_set_value(objects.arc_temperature, arc_temp);

        // Celsius — air quality panel secondary readout
        int celsius = (int)g_interior_temp_c;
        lv_label_set_text_fmt(objects.label_temp_celsius_value, "%d \u00b0C", celsius);

        // Humidity — air quality panel arc + value
        int hum_whole = g_humidity_raw / 100;
        int hum_frac  = (g_humidity_raw % 100) / 10;
        lv_label_set_text_fmt(objects.label_humidity_value, "%d.%d", hum_whole, hum_frac);
        int arc_hum = (hum_whole > 100) ? 100 : hum_whole;
        lv_arc_set_value(objects.arc_humidity, arc_hum);
    }

    // Update GPS satellite count and GNSS mode from CAN
    if (g_gps_sat_updated) {
        g_gps_sat_updated = false;
        lv_label_set_text_fmt(objects.label_number_of_sats_value, "%d", (int)g_gps_num_sats);
        set_var_satellite_count((int32_t)g_gps_num_sats);

        // DFRobot GNSS mode = active constellation combination (matches PWA strings)
        const char *mode_str;
        switch (g_gps_gnss_mode) {
            case 1: mode_str = "Gps";                       break;
            case 2: mode_str = "Beidou";                    break;
            case 3: mode_str = "Gps + Beidou";              break;
            case 4: mode_str = "Glonass";                   break;
            case 5: mode_str = "Gps + Glonass";             break;
            case 6: mode_str = "Beidou + Glonass";          break;
            case 7: mode_str = "Gps + Beidou + Glonass";    break;
            default: mode_str = "No Fix";                   break;
        }
        lv_label_set_text(objects.label_gps_mode_value, mode_str);
    }

    // Update GPS elevation from CAN (raw * 0.01 = meters, convert to feet)
    if (g_gps_alt_updated) {
        g_gps_alt_updated = false;
        double alt_m = (double)g_gps_altitude_raw * 0.01;
        int alt_ft = (int)(alt_m * 3.28084);
        lv_label_set_text_fmt(objects.label_elevation_value, "%d", alt_ft);
    }

    // Update battery voltage and SOC from CAN (BatteryShuntData1)
    if (g_batt_shunt1_updated) {
        g_batt_shunt1_updated = false;
        lv_label_set_text_fmt(objects.label_battery_voltage, "%d.%02d",
            (int)g_batt_voltage_whole, (int)g_batt_voltage_dec);
        int soc = (int)g_batt_soc_whole;
        lv_label_set_text_fmt(objects.label_power_battery_percentage, "%d", soc);
        lv_bar_set_value(objects.bar_battery_soc, soc, LV_ANIM_OFF);
    }

    // Update wattage consumption and time remaining from CAN (BatteryShuntData2)
    if (g_batt_shunt2_updated) {
        g_batt_shunt2_updated = false;

        // Wattage (with sign)
        int watts = (int)g_shunt_wattage;
        if (g_is_wattage_negative == 0xFF) watts = -watts;
        lv_label_set_text_fmt(objects.label_shunt_current_watts_used, "%d", watts);

        // Time to go (minutes → hours for display)
        uint16_t ttg = g_time_to_go_min;
        if (ttg == 0xFFFF || ttg == 0) {
            lv_label_set_text(objects.label_power_remaining_time_to_go_value, "-");
            lv_label_set_text(objects.label_time_to_go_measurement_type, "");
        } else {
            int hours = ttg / 60;
            int mins = ttg % 60;
            lv_label_set_text_fmt(objects.label_power_remaining_time_to_go_value,
                "%d:%02d", hours, mins);
            lv_label_set_text(objects.label_time_to_go_measurement_type, "Hrs");
        }
        // Update the arc (range 0-2000, value in minutes, cap at 2000)
        int arc_val = (ttg > 2000) ? 2000 : (int)ttg;
        lv_arc_set_value(objects.power_arc_remaining_hours, arc_val);
    }

    // Update solar wattage and charge mode from CAN (SolarMpptData1)
    if (g_solar_mppt1_updated) {
        g_solar_mppt1_updated = false;
        lv_label_set_text_fmt(objects.label_solar_wattage, "%d", (int)g_solar_wattage);

        // Victron MPPT charge status: 0=Off, 2=Fault, 3=Bulk, 4=Absorption, 5=Float
        const char *charge_str;
        switch (g_solar_charge_status) {
            case 0: charge_str = "Off";         break;
            case 2: charge_str = "Fault";       break;
            case 3: charge_str = "Bulk";        break;
            case 4: charge_str = "Absorption";  break;
            case 5: charge_str = "Float";       break;
            default: charge_str = "Unknown";    break;
        }
        lv_label_set_text(objects.label_curent_charge_mode, charge_str);
    }

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
