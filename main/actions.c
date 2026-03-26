#include <string.h>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ui/actions.h"
#include "ui/screens.h"
#include "ui/vars.h"
#include "ui/ui.h"

static const char *TAG = "actions";

static enum ScreensEnum g_active_screen = SCREEN_ID_PAGE_HOME;

enum ScreensEnum get_active_screen_id(void)
{
    return g_active_screen;
}

// Defined in main.c
extern void set_backlight(uint8_t brightness);
extern bool can_send(uint32_t id, const uint8_t *data, uint8_t len);
extern volatile uint8_t g_device_pwm[8];
extern void update_device_status_indicators(bool force);

void action_change_screen_brightness(lv_event_t *e)
{
    int32_t slider_val = lv_slider_get_value(objects.slider_screen_brightness);
    uint8_t pwm = (uint8_t)((slider_val * 255) / 100);
    set_backlight(pwm);
    set_var_user_settings_changed(true);
}

void action_rotate_screen(lv_event_t *e)
{
    // TODO: Implement screen rotation
}

static void update_nav_bar_active_state(int active_idx)
{
    lv_obj_t *nav[6][6] = {
        {
            objects.widget_home_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.widget_home_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.widget_home_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.widget_home_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.widget_home_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.widget_home_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
        {
            objects.widget_trailer_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.widget_trailer_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.widget_trailer_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.widget_trailer_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.widget_trailer_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.widget_trailer_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
        {
            objects.widget_power_management_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.widget_power_management_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.widget_power_management_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.widget_power_management_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.widget_power_management_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.widget_power_management_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
        {
            objects.widget_air_quality_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.widget_air_quality_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.widget_air_quality_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.widget_air_quality_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.widget_air_quality_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.widget_air_quality_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
        {
            objects.widget_water_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.widget_water_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.widget_water_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.widget_water_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.widget_water_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.widget_water_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
        {
            objects.widget_settings_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.widget_settings_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.widget_settings_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.widget_settings_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.widget_settings_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.widget_settings_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
    };

    for (int page = 0; page < 6; page++) {
        for (int btn = 0; btn < 6; btn++) {
            if (nav[page][btn]) {
                if (btn == active_idx)
                    lv_obj_add_state(nav[page][btn], LV_STATE_CHECKED);
                else
                    lv_obj_clear_state(nav[page][btn], LV_STATE_CHECKED);
            }
        }
    }
}

void action_change_screen(lv_event_t *e)
{
    int32_t idx = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < 6) {
        g_active_screen = (enum ScreensEnum)(idx + 1);
        lv_obj_t *screen = ((lv_obj_t **)&objects)[idx];
        lv_disp_load_scr(screen);
        update_nav_bar_active_state(idx);
    }
}

void action_send_device_command(lv_event_t *e)
{
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;

    static int64_t last_send_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_send_us < 300000) return;
    last_send_us = now;

    int32_t device_num = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (device_num < 1 || device_num > 8) return;

    uint8_t data[1] = { (uint8_t)(device_num - 1) };
    if (can_send(0x18, data, 1)) {
        ESP_LOGI(TAG, "CAN TX: toggle channel %d", device_num - 1);
    } else {
        ESP_LOGW(TAG, "CAN TX failed: channel %d", device_num - 1);
    }
}

void action_change_desired_temperature(lv_event_t *e)
{
    int32_t value = lv_arc_get_value(objects.arc_thermostat);
    set_var_desired_temperature(value);
    lv_label_set_text_fmt(objects.label_desired_temperature_value, "%d", (int)value);
}

void action_change_fm_radio_station(lv_event_t *e) { }
void action_go_to_preset(lv_event_t *e) { }
void action_settings_selection_change(lv_event_t *e) { }

void action_change_theme(lv_event_t *e)
{
    int32_t theme_index = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (theme_index < 0 || theme_index > 1) return;

    change_color_theme((uint32_t)theme_index);

    if (theme_index == 0) {
        lv_obj_add_state(objects.btn_theme_light, LV_STATE_CHECKED);
        lv_obj_clear_state(objects.btn_theme_dark, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(objects.btn_theme_light, LV_STATE_CHECKED);
        lv_obj_add_state(objects.btn_theme_dark, LV_STATE_CHECKED);
    }

    update_device_status_indicators(true);
    set_var_selected_theme(theme_index);
    set_var_user_settings_changed(true);
}

void action_timeout_changed(lv_event_t *e)
{
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;

    int32_t direction = (int32_t)(intptr_t)lv_event_get_user_data(e);
    int32_t value = get_var_screen_timeout_value();

    if (direction == 0) {
        value--;
        if (value < 0) value = 0;
    } else {
        value++;
        if (value > 30) value = 30;
    }

    set_var_screen_timeout_value(value);
    lv_label_set_text_fmt(objects.label_screen_timeout_value, "%d", (int)value);
    set_var_user_settings_changed(true);
}

void action_timezone_change(lv_event_t *e) { }
void action_commit_mac_address_changes(lv_event_t *e) { }
void action_set_device_brightness_level(lv_event_t *e) { }
void action_show_device_brightness_dialog(lv_event_t *e) { }
void action_close_dialog(lv_event_t *e) { }
void action_selected_wifi_changed(lv_event_t *e) { }
void action_show_wi_fi_keyaboard_entry(lv_event_t *e) { }
void action_hide_wifi_keyboard(lv_event_t *e) { }
void action_scan_wifi_networks(lv_event_t *e) { }
void action_wifi_network_selected(lv_event_t *e) { }
void action_connect_to_wifi(lv_event_t *e) { }

void action_all_on_off(lv_event_t *e)
{
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;

    static int64_t last_send_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_send_us < 300000) return;
    last_send_us = now;

    bool any_on = false;
    for (int i = 0; i < 8; i++) {
        if (g_device_pwm[i] > 0) { any_on = true; break; }
    }

    uint8_t data[2] = { 8, any_on ? 0 : 1 };
    if (can_send(0x18, data, 2)) {
        ESP_LOGI(TAG, "CAN TX: all %s", any_on ? "off" : "on");
    } else {
        ESP_LOGW(TAG, "CAN TX failed: all on/off");
    }
}
