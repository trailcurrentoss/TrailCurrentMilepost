/**
 * Stub implementations for EEZ Studio UI action callbacks.
 * These will be filled in with real application logic as features are implemented.
 */

#include <lvgl.h>
#include <debug.h>
#include <TwaiTaskBased.h>
#include "ui/actions.h"
#include "ui/screens.h"
#include "ui/vars.h"
#include "ui/ui.h"

static enum ScreensEnum g_active_screen = SCREEN_ID_PAGE_HOME;

enum ScreensEnum get_active_screen_id() {
    return g_active_screen;
}

// Defined in main.cpp
extern void set_backlight(uint8_t brightness);

void action_change_screen_brightness(lv_event_t *e) {
    int32_t slider_val = lv_slider_get_value(objects.slider_screen_brightness);
    uint8_t pwm = (uint8_t)((slider_val * 255) / 100);
    set_backlight(pwm);
    set_var_user_settings_changed(true);
}

void action_rotate_screen(lv_event_t *e) {
    // TODO: Implement screen rotation
}

static void update_nav_bar_active_state(int active_idx) {
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

void action_change_screen(lv_event_t *e) {
    int32_t idx = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < 6) {
        g_active_screen = (enum ScreensEnum)(idx + 1);
        lv_obj_t *screen = ((lv_obj_t **)&objects)[idx];
        lv_disp_load_scr(screen);
        update_nav_bar_active_state(idx);
    }
}

void action_send_device_command(lv_event_t *e) {
    // Ignore events bubbled up from child labels
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;

    // Debounce: ignore rapid repeated taps within 300ms
    static unsigned long last_send_ms = 0;
    unsigned long now = millis();
    if (now - last_send_ms < 300) return;
    last_send_ms = now;

    int32_t device_num = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (device_num < 1 || device_num > 8) return;

    uint8_t channel = (uint8_t)(device_num - 1);
    twai_message_t msg = {};
    msg.identifier = 0x18;
    msg.data_length_code = 1;
    msg.data[0] = channel;

    if (TwaiTaskBased::send(msg, 0)) {
        debugf("CAN TX: toggle channel %d\n", channel);
    } else {
        debugf("CAN TX failed: channel %d\n", channel);
    }
}

void action_change_desired_temperature(lv_event_t *e) {
    int32_t value = lv_arc_get_value(objects.arc_thermostat);
    set_var_desired_temperature(value);
    lv_label_set_text_fmt(objects.label_desired_temperature_value, "%d", (int)value);
}

void action_change_fm_radio_station(lv_event_t *e) {
    // TODO: Implement FM radio station change
}

void action_go_to_preset(lv_event_t *e) {
    // TODO: Implement radio preset navigation
}

void action_settings_selection_change(lv_event_t *e) {
    // TODO: Implement settings tab selection change
}

void action_change_theme(lv_event_t *e) {
    int32_t theme_index = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (theme_index < 0 || theme_index > 1) return;

    // Apply the color theme to all styles and objects
    change_color_theme((uint32_t)theme_index);

    // Update the checked state so the active button appears selected
    if (theme_index == 0) {
        lv_obj_add_state(objects.btn_theme_light, LV_STATE_CHECKED);
        lv_obj_clear_state(objects.btn_theme_dark, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(objects.btn_theme_light, LV_STATE_CHECKED);
        lv_obj_add_state(objects.btn_theme_dark, LV_STATE_CHECKED);
    }

    // Re-apply device indicator styles (change_color_theme resets inline colors)
    extern void update_device_status_indicators(bool force);
    update_device_status_indicators(true);

    // Persist the selection in the variable store
    set_var_selected_theme(theme_index);
    set_var_user_settings_changed(true);
}

void action_timeout_changed(lv_event_t *e) {
    // Skip bubbled events from child labels to avoid double-firing
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;

    int32_t direction = (int32_t)(intptr_t)lv_event_get_user_data(e);
    int32_t value = get_var_screen_timeout_value();

    if (direction == 0) {
        // Decrease
        value--;
        if (value < 0) value = 0;
    } else {
        // Increase
        value++;
        if (value > 30) value = 30;
    }

    set_var_screen_timeout_value(value);
    lv_label_set_text_fmt(objects.label_screen_timeout_value, "%d", (int)value);
    set_var_user_settings_changed(true);
}

void action_timezone_change(lv_event_t *e) {
    // TODO: Implement timezone change
}

void action_commit_mac_address_changes(lv_event_t *e) {
    // TODO: Implement MAC address commit
}

void action_set_device_brightness_level(lv_event_t *e) {
    // TODO: Implement device brightness level setting
}

void action_show_device_brightness_dialog(lv_event_t *e) {
    // TODO: Implement show brightness dialog
}

void action_close_dialog(lv_event_t *e) {
    // TODO: Implement close dialog
}

void action_selected_wifi_changed(lv_event_t *e) {
    // TODO: Implement WiFi selection change
}

void action_show_wi_fi_keyaboard_entry(lv_event_t *e) {
    // TODO: Implement show WiFi keyboard
}

void action_hide_wifi_keyboard(lv_event_t *e) {
    // TODO: Implement hide WiFi keyboard
}

void action_scan_wifi_networks(lv_event_t *e) {
    // TODO: Implement WiFi network scan
}

void action_wifi_network_selected(lv_event_t *e) {
    // TODO: Implement WiFi network selection
}

void action_connect_to_wifi(lv_event_t *e) {
    // TODO: Implement WiFi connection
}
