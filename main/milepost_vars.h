#ifndef MILEPOST_VARS_H
#define MILEPOST_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Milepost variable setter API — the single entry point through which
 * CAN-frame parsers in main.c feed data into the UI.
 *
 * All setters own their widget updates internally and acquire the LVGL
 * display lock where they touch objects.foo. Callers must NOT hold the
 * lock; the setters serialize themselves via lvgl_port_lock() in the
 * esp_lvgl_port.h shim.
 *
 * Rolling 60-min ring buffers: solar/soc/volts/load (Power) and
 * temp/hum/eco2/tvoc/co (Air) each keep the last 24 samples in a ring
 * accessible via metric_history_get() so the sparkline canvases can
 * redraw on demand.
 */

/* Ring-buffer capacity. 24 samples × 2.5 min per sample = 60-minute
 * rolling window. Milepost's CAN sources broadcast every 1–2 s; we
 * downsample per-bucket using a last-write-wins strategy keyed on
 * esp_timer time (150 s buckets). Charts step forward by exactly one
 * bar every 2.5 min — see metric_push() in vars.c. */
#define METRIC_HISTORY_LEN     24
#define METRIC_BUCKET_US       (150LL * 1000000LL)  /* 2.5 minutes */

typedef enum {
    METRIC_SOLAR = 0,   /* W */
    METRIC_SOC,         /* % */
    METRIC_VOLTS,       /* V (×100 → int16 in ring, /100 on read) */
    METRIC_LOAD,        /* W */
    METRIC_TEMP,        /* °F stored, converts on display */
    METRIC_HUM,         /* % */
    METRIC_ECO2,        /* ppm */
    METRIC_TVOC,        /* ppb */
    METRIC_CO,          /* ppm (×10 → int16 in ring) */
    METRIC_COUNT
} metric_id_t;

/* Fill `out` (size METRIC_HISTORY_LEN) with the last N samples in
 * chronological order (oldest → newest). Missing samples come back as
 * METRIC_MISSING. Returns the count of populated samples. */
#define METRIC_MISSING INT16_MIN
int  metric_history_get(metric_id_t id, int16_t *out);

/* Latest sample. Returns METRIC_MISSING if never seen. */
int16_t metric_last(metric_id_t id);

/* Temperature unit: 0=Fahrenheit, 1=Celsius */
int32_t get_var_temperature_unit(void);
void    set_var_temperature_unit(int32_t value);

/* Device relay/light status (0=off, >0=on). Driven from Torrent/Switchback
 * status frames (CAN 0x1B-0x1D / 0x28-0x2A) via update_ui_from_can(). */
void set_var_device01_status(int32_t value);
void set_var_device02_status(int32_t value);
void set_var_device03_status(int32_t value);
void set_var_device04_status(int32_t value);
void set_var_device05_status(int32_t value);
void set_var_device06_status(int32_t value);
void set_var_device07_status(int32_t value);
void set_var_device08_status(int32_t value);
int32_t get_var_device_brightness(int32_t device_id);

/* Currently-adjusted device — kept for echo-suppression symmetry with
 * dimmable Torrent channels; not used on the current UI. */
int32_t get_var_current_device_brightness_identifier(void);
void    set_var_current_device_brightness_identifier(int32_t value);

/* Battery / energy — feeds the Power screen sparklines. Fed from CAN
 * 0x23 (Bearing shunt) and CAN 0x2C (Solstice MPPT). */
void set_var_battery_soc(int32_t percent);
void set_var_battery_voltage(float volts);
void set_var_solar_watts(int32_t watts);
void set_var_solar_status(const char *status);
void set_var_consumption_watts(int32_t watts);
void set_var_time_remaining(int32_t minutes);

/* Device-battery setters — no-ops on Milepost (hardwired, no lipo). Kept
 * so shared vars.c cross-links resolve without #ifdef guards. */
void set_var_internal_battery_soc(int32_t percent);
void set_var_internal_battery_voltage(float volts);
void set_var_internal_charging(bool charging);

/* GPS / GNSS — Trailer screen right panel. Fed from Bearing CAN
 * 0x07 / 0x08 / 0x09. */
void set_var_latitude(float lat);
void set_var_longitude(float lon);
void set_var_altitude(float feet);
void set_var_speed(float knots);
void set_var_course(float degrees);
void set_var_gnss_mode(const char *mode);
void set_var_satellite_count(int32_t value);
void set_var_gps_time(int year, int month, int day, int hour, int minute, int second);

/* Air quality — fed from Borealis CAN 0x1F (temp / humidity / TVOC / eCO2). */
void set_var_humidity(float percent);
void set_var_co2(int32_t ppm);
void set_var_tvoc(int32_t ppb);
void set_var_current_interior_temperature(int32_t value);
void set_var_current_exterior_temperature(int32_t value);
void set_var_co(int32_t ppm);
void set_var_co_flags(bool warn, bool alarm);

/* Water tank levels (0-100 %) — Reservoir CAN 0x3E. */
void set_var_water_levels(int32_t fresh, int32_t grey, int32_t black);

/* Trailer leveling — Plateau publishes tilt over CAN. Payload fields:
 *   front_back (pitch °), side_to_side (roll °),
 *   front_back_diff_mm, left_right_diff_mm.
 * Drives the Trailer screen's side/back leveling cards. */
void set_var_leveling(float pitch_deg, float roll_deg,
                      int32_t fb_diff_mm, int32_t lr_diff_mm);

/* WiFi RSSI → topbar chip. Painted from a 5-second LVGL timer. */
void set_var_wifi_rssi(int32_t rssi_dbm);

/* Per-subsystem staleness clears — call when a source hasn't broadcast
 * inside its expected window. Currently unwired on Milepost (all
 * subsystems come from live CAN); kept for parity with the setter API. */
void clear_var_energy(void);
void clear_var_airquality(void);
void clear_var_gps(void);
void clear_var_water(void);
void clear_var_leveling(void);

/* Rotation / MAC — main.c calls these once at boot. */
void set_var_rotation_degrees(int32_t value);
int32_t get_var_rotation_degrees(void);
void set_var_mcu_mac_address(const char *value);
const char *get_var_mcu_mac_address(void);

/* Settings state — persisted in NVS namespace "milepost". */
int32_t get_var_screen_brightness(void);
void    set_var_screen_brightness(int32_t value);
int32_t get_var_screen_timeout_minutes(void);
void    set_var_screen_timeout_minutes(int32_t value);

/* Load persisted user settings (theme / brightness / timeout / tz / temp
 * unit) from NVS and apply to the UI. Called once from app_main after
 * ui_init(). */
void restore_user_settings(void);

/* Map an IANA zone (as authored in the Settings timezone dropdown) to its
 * POSIX TZ string and install it via setenv+tzset, so localtime_r() in
 * update_clock_display() renders local wall time instead of UTC. Returns
 * true when the zone was known and applied. */
bool apply_timezone(const char *iana);

/* Clock label update — called by main loop at 1 Hz with display lock held. */
void update_clock_display(void);

/* Start the 500ms LVGL timer that blinks the Home page separator dots.
 * Call once, under the display lock, after ui_init(). */
void init_clock_blink(void);

/* Create an lv_chart child in every authored `<page>_<metric>_chart`
 * panel. Call once, under the display lock, after ui_init(). The chart
 * receives shift-mode updates whenever metric_push() fires (from the
 * CAN handler in main.c) so history keeps building even when the user
 * is on a different screen. */
void init_metric_charts(void);

/* Reset all widgets that carry author-time placeholders (currently: the
 * three water tank bars, which are authored at 50 % fill so the vertical
 * gradient is visible in EEZ Studio's canvas). Call once under the
 * display lock, after ui_init(). */
void reset_placeholders(void);

/* Start the LVGL timer that reads WiFi RSSI from the driver every 5s and
 * paints it to every topbar. Call once, under the display lock, after
 * ui_init(). Safe to call before WiFi is up — the poll reports 0 dBm
 * until the station associates. */
void init_wifi_rssi_poll(void);

/* Start the LVGL timer that watches lv_disp_get_inactive_time() and blanks
 * the backlight after get_var_screen_timeout_minutes() of inactivity, then
 * restores it to get_var_screen_brightness() as soon as the user touches
 * the screen again. Timeout value 0 = never blank. Call once, under the
 * display lock, after ui_init(). */
void init_screen_timeout(void);

/* Widen ext_click_area on the touch-critical top-level tappables (bottom
 * nav buttons + home page device buttons). Doesn't change the visible
 * widget geometry — only extends the invisible hit rectangle so finger
 * drift during release doesn't kill the CLICKED event on a 10" glass. */
void init_touch_target_hit_areas(void);

/* Enable click flag + acknowledge-alarms callback on the topbar bell +
 * badge across every page. Call once, under the display lock, after
 * ui_init(). Silences re-alerts for a snooze window; does not clear the
 * badge count (the badge follows the underlying alarm-active state). */
void init_notif_icon_ack_taps(void);

#ifdef __cplusplus
}
#endif

#endif /* MILEPOST_VARS_H */
