#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui/vars.h"
#include "ui/screens.h"

// main.c helpers
extern int32_t fahrenheit_to_display(int32_t temp_f);
extern const char *temperature_unit_suffix(void);

static int32_t g_global_variable_integer = 0;
static float g_global_variable_float = 0.0f;
static double g_global_variable_double = 0.0;
static bool g_global_variable_boolean = false;
static char g_global_variable_string[64] = "";
static int32_t g_rotation_degrees = 0;
static int32_t g_satellite_count = 0;
static int32_t g_desired_temperature = 72;
static int32_t g_current_interior_temperature = 70;
static int32_t g_current_exterior_temperature = 65;
static float g_desired_fm_radio_station = 98.1f;
static bool g_user_settings_changed = false;
static char g_current_time_zone_string[32] = "EST";
static int32_t g_gateway_mac_bytes[6] = {0};
static int32_t g_screen_timeout_value = 60;
static int32_t g_selected_theme = 0;
static int32_t g_current_device_brightness_identifier = 0;
static int32_t g_temperature_unit = 0;  // 0=F, 1=C
static int32_t g_edit_btn_number = 0;
static char    g_edit_label_text[24] = "";
static int32_t g_edit_icon_codepoint = 0;
static int32_t g_assign_module_type = 0;
static int32_t g_assign_instance = 0;

int32_t get_var_global_variable_integer(void) { return g_global_variable_integer; }
void set_var_global_variable_integer(int32_t value) { g_global_variable_integer = value; }

float get_var_global_variable_float(void) { return g_global_variable_float; }
void set_var_global_variable_float(float value) { g_global_variable_float = value; }

double get_var_global_variable_double(void) { return g_global_variable_double; }
void set_var_global_variable_double(double value) { g_global_variable_double = value; }

bool get_var_global_variable_boolean(void) { return g_global_variable_boolean; }
void set_var_global_variable_boolean(bool value) { g_global_variable_boolean = value; }

const char *get_var_global_variable_string(void) { return g_global_variable_string; }
void set_var_global_variable_string(const char *value) {
    strncpy(g_global_variable_string, value, sizeof(g_global_variable_string) - 1);
    g_global_variable_string[sizeof(g_global_variable_string) - 1] = '\0';
}

int32_t get_var_rotation_degrees(void) { return g_rotation_degrees; }
void set_var_rotation_degrees(int32_t value) { g_rotation_degrees = value; }

int32_t get_var_satellite_count(void) { return g_satellite_count; }
void set_var_satellite_count(int32_t value) { g_satellite_count = value; }

int32_t get_var_desired_temperature(void) { return g_desired_temperature; }
void set_var_desired_temperature(int32_t value) { g_desired_temperature = value; }

int32_t get_var_current_interior_temperature(void) { return g_current_interior_temperature; }
void set_var_current_interior_temperature(int32_t value)
{
    g_current_interior_temperature = value;
    int32_t disp = fahrenheit_to_display(value);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)disp);
    // Home thermostat — small current temperature readout below the setpoint
    if (objects.label_current_interior_temperature) {
        lv_label_set_text(objects.label_current_interior_temperature, buf);
    }
    // PageAirQuality — big temperature tile
    if (objects.label_air_quality_temperature_value) {
        lv_label_set_text(objects.label_air_quality_temperature_value, buf);
    }
}

int32_t get_var_current_exterior_temperature(void) { return g_current_exterior_temperature; }
void set_var_current_exterior_temperature(int32_t value) { g_current_exterior_temperature = value; }

float get_var_desired_fm_radio_station(void) { return g_desired_fm_radio_station; }
void set_var_desired_fm_radio_station(float value) { g_desired_fm_radio_station = value; }

bool get_var_user_settings_changed(void) { return g_user_settings_changed; }
void set_var_user_settings_changed(bool value) { g_user_settings_changed = value; }

const char *get_var_current_time_zone_string(void) { return g_current_time_zone_string; }
void set_var_current_time_zone_string(const char *value) {
    strncpy(g_current_time_zone_string, value, sizeof(g_current_time_zone_string) - 1);
    g_current_time_zone_string[sizeof(g_current_time_zone_string) - 1] = '\0';
}

int32_t get_var_gateway_mac_address_byte1(void) { return g_gateway_mac_bytes[0]; }
void set_var_gateway_mac_address_byte1(int32_t value) { g_gateway_mac_bytes[0] = value; }
int32_t get_var_gateway_mac_address_byte2(void) { return g_gateway_mac_bytes[1]; }
void set_var_gateway_mac_address_byte2(int32_t value) { g_gateway_mac_bytes[1] = value; }
int32_t get_var_gateway_mac_address_byte3(void) { return g_gateway_mac_bytes[2]; }
void set_var_gateway_mac_address_byte3(int32_t value) { g_gateway_mac_bytes[2] = value; }
int32_t get_var_gateway_mac_address_byte4(void) { return g_gateway_mac_bytes[3]; }
void set_var_gateway_mac_address_byte4(int32_t value) { g_gateway_mac_bytes[3] = value; }
int32_t get_var_gateway_mac_address_byte5(void) { return g_gateway_mac_bytes[4]; }
void set_var_gateway_mac_address_byte5(int32_t value) { g_gateway_mac_bytes[4] = value; }
int32_t get_var_gateway_mac_address_byte6(void) { return g_gateway_mac_bytes[5]; }
void set_var_gateway_mac_address_byte6(int32_t value) { g_gateway_mac_bytes[5] = value; }

int32_t get_var_screen_timeout_value(void) { return g_screen_timeout_value; }
void set_var_screen_timeout_value(int32_t value) { g_screen_timeout_value = value; }

int32_t get_var_selected_theme(void) { return g_selected_theme; }
void set_var_selected_theme(int32_t value) { g_selected_theme = value; }

int32_t get_var_current_device_brightness_identifier(void) { return g_current_device_brightness_identifier; }
void set_var_current_device_brightness_identifier(int32_t value) { g_current_device_brightness_identifier = value; }

int32_t get_var_temperature_unit(void) { return g_temperature_unit; }
void set_var_temperature_unit(int32_t value)
{
    if (value != 0 && value != 1) value = 0;
    g_temperature_unit = value;

    // Toggle checked state on the F/C buttons if they exist
    if (objects.btn_temp_fahrenheit) {
        if (value == 0) lv_obj_add_state(objects.btn_temp_fahrenheit, LV_STATE_CHECKED);
        else            lv_obj_clear_state(objects.btn_temp_fahrenheit, LV_STATE_CHECKED);
    }
    if (objects.btn_temp_celsius) {
        if (value == 1) lv_obj_add_state(objects.btn_temp_celsius, LV_STATE_CHECKED);
        else            lv_obj_clear_state(objects.btn_temp_celsius, LV_STATE_CHECKED);
    }

    // Update the air quality unit indicator label
    if (objects.label_temp_value_indicator) {
        lv_label_set_text(objects.label_temp_value_indicator, temperature_unit_suffix());
    }

    // Re-render the current temperature in the new unit
    set_var_current_interior_temperature(g_current_interior_temperature);
}

int32_t get_var_edit_btn_number(void) { return g_edit_btn_number; }
void set_var_edit_btn_number(int32_t value) { g_edit_btn_number = value; }

const char *get_var_edit_label_text(void) { return g_edit_label_text; }
void set_var_edit_label_text(const char *value) {
    strncpy(g_edit_label_text, value, sizeof(g_edit_label_text) - 1);
    g_edit_label_text[sizeof(g_edit_label_text) - 1] = '\0';
}

int32_t get_var_edit_icon_codepoint(void) { return g_edit_icon_codepoint; }
void set_var_edit_icon_codepoint(int32_t value) { g_edit_icon_codepoint = value; }

int32_t get_var_assign_module_type(void) { return g_assign_module_type; }
void set_var_assign_module_type(int32_t value) { g_assign_module_type = value; }

int32_t get_var_assign_instance(void) { return g_assign_instance; }
void set_var_assign_instance(int32_t value) { g_assign_instance = value; }
