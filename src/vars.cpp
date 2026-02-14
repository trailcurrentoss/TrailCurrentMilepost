/**
 * Stub implementations for EEZ Studio UI variable getters/setters.
 * These hold default values and will be connected to real data sources
 * (CAN bus, sensors, settings) as features are implemented.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ui/vars.h"

// Storage for global variables
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
static int32_t g_wifi_scan_status = 0;
static int32_t g_wifi_network_count = 0;
static int32_t g_selected_wifi_network_index = 0;
static char g_selected_wifi_network_name[64] = "";
static int32_t g_wifi_connection_status = 0;

// --- Generic variables ---

int32_t get_var_global_variable_integer() { return g_global_variable_integer; }
void set_var_global_variable_integer(int32_t value) { g_global_variable_integer = value; }

float get_var_global_variable_float() { return g_global_variable_float; }
void set_var_global_variable_float(float value) { g_global_variable_float = value; }

double get_var_global_variable_double() { return g_global_variable_double; }
void set_var_global_variable_double(double value) { g_global_variable_double = value; }

bool get_var_global_variable_boolean() { return g_global_variable_boolean; }
void set_var_global_variable_boolean(bool value) { g_global_variable_boolean = value; }

const char *get_var_global_variable_string() { return g_global_variable_string; }
void set_var_global_variable_string(const char *value) {
    strncpy(g_global_variable_string, value, sizeof(g_global_variable_string) - 1);
    g_global_variable_string[sizeof(g_global_variable_string) - 1] = '\0';
}

// --- Sensor/display variables ---

int32_t get_var_rotation_degrees() { return g_rotation_degrees; }
void set_var_rotation_degrees(int32_t value) { g_rotation_degrees = value; }

int32_t get_var_satellite_count() { return g_satellite_count; }
void set_var_satellite_count(int32_t value) { g_satellite_count = value; }

int32_t get_var_desired_temperature() { return g_desired_temperature; }
void set_var_desired_temperature(int32_t value) { g_desired_temperature = value; }

int32_t get_var_current_interior_temperature() { return g_current_interior_temperature; }
void set_var_current_interior_temperature(int32_t value) { g_current_interior_temperature = value; }

int32_t get_var_current_exterior_temperature() { return g_current_exterior_temperature; }
void set_var_current_exterior_temperature(int32_t value) { g_current_exterior_temperature = value; }

float get_var_desired_fm_radio_station() { return g_desired_fm_radio_station; }
void set_var_desired_fm_radio_station(float value) { g_desired_fm_radio_station = value; }

bool get_var_user_settings_changed() { return g_user_settings_changed; }
void set_var_user_settings_changed(bool value) { g_user_settings_changed = value; }

const char *get_var_current_time_zone_string() { return g_current_time_zone_string; }
void set_var_current_time_zone_string(const char *value) {
    strncpy(g_current_time_zone_string, value, sizeof(g_current_time_zone_string) - 1);
    g_current_time_zone_string[sizeof(g_current_time_zone_string) - 1] = '\0';
}

// --- Gateway MAC address bytes ---

int32_t get_var_gateway_mac_address_byte1() { return g_gateway_mac_bytes[0]; }
void set_var_gateway_mac_address_byte1(int32_t value) { g_gateway_mac_bytes[0] = value; }

int32_t get_var_gateway_mac_address_byte2() { return g_gateway_mac_bytes[1]; }
void set_var_gateway_mac_address_byte2(int32_t value) { g_gateway_mac_bytes[1] = value; }

int32_t get_var_gateway_mac_address_byte3() { return g_gateway_mac_bytes[2]; }
void set_var_gateway_mac_address_byte3(int32_t value) { g_gateway_mac_bytes[2] = value; }

int32_t get_var_gateway_mac_address_byte4() { return g_gateway_mac_bytes[3]; }
void set_var_gateway_mac_address_byte4(int32_t value) { g_gateway_mac_bytes[3] = value; }

int32_t get_var_gateway_mac_address_byte5() { return g_gateway_mac_bytes[4]; }
void set_var_gateway_mac_address_byte5(int32_t value) { g_gateway_mac_bytes[4] = value; }

int32_t get_var_gateway_mac_address_byte6() { return g_gateway_mac_bytes[5]; }
void set_var_gateway_mac_address_byte6(int32_t value) { g_gateway_mac_bytes[5] = value; }

// --- Settings variables ---

int32_t get_var_screen_timeout_value() { return g_screen_timeout_value; }
void set_var_screen_timeout_value(int32_t value) { g_screen_timeout_value = value; }

int32_t get_var_selected_theme() { return g_selected_theme; }
void set_var_selected_theme(int32_t value) { g_selected_theme = value; }

int32_t get_var_current_device_brightness_identifier() { return g_current_device_brightness_identifier; }
void set_var_current_device_brightness_identifier(int32_t value) { g_current_device_brightness_identifier = value; }

// --- WiFi variables ---

int32_t get_var_wifi_scan_status() { return g_wifi_scan_status; }
void set_var_wifi_scan_status(int32_t value) { g_wifi_scan_status = value; }

int32_t get_var_wifi_network_count() { return g_wifi_network_count; }
void set_var_wifi_network_count(int32_t value) { g_wifi_network_count = value; }

int32_t get_var_selected_wifi_network_index() { return g_selected_wifi_network_index; }
void set_var_selected_wifi_network_index(int32_t value) { g_selected_wifi_network_index = value; }

const char *get_var_selected_wifi_network_name() { return g_selected_wifi_network_name; }
void set_var_selected_wifi_network_name(const char *value) {
    strncpy(g_selected_wifi_network_name, value, sizeof(g_selected_wifi_network_name) - 1);
    g_selected_wifi_network_name[sizeof(g_selected_wifi_network_name) - 1] = '\0';
}

int32_t get_var_wifi_connection_status() { return g_wifi_connection_status; }
void set_var_wifi_connection_status(int32_t value) { g_wifi_connection_status = value; }
