#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ui/actions.h"
#include "ui/screens.h"
#include "ui/vars.h"
#include "ui/ui.h"
#include "button_config.h"

static const char *TAG = "actions";

static enum ScreensEnum g_active_screen = SCREEN_ID_PAGE_HOME;

enum ScreensEnum get_active_screen_id(void)
{
    return g_active_screen;
}

// Defined in main.c
extern void set_backlight(uint8_t brightness);
extern bool can_send(uint32_t id, const uint8_t *data, uint8_t len);
extern void update_device_status_indicators(bool force);

extern int64_t screen_wake_age_us(void);

void action_change_screen_brightness(lv_event_t *e)
{
    // Wake-tap grace period: ignore slider events within 500 ms of a
    // wake so a touch that was meant only to bring the screen back
    // can't be interpreted as a brightness change.
    if (screen_wake_age_us() < 500000) {
        ESP_LOGI(TAG, "ignoring brightness event within wake grace period");
        return;
    }
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
            objects.home_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.home_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.home_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.home_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.home_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.home_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
        {
            objects.trailer_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.trailer_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.trailer_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.trailer_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.trailer_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.trailer_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
        {
            objects.power_management_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.power_management_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.power_management_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.power_management_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.power_management_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.power_management_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
        {
            objects.air_quality_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.air_quality_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.air_quality_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.air_quality_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.air_quality_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.air_quality_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
        {
            objects.water_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.water_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.water_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.water_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.water_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.water_page_bottom_nav_bar__bottom_nav_bar_button_settings,
        },
        {
            objects.settings_page_bottom_nav_bar__bottom_nav_bar_button_home,
            objects.settings_page_bottom_nav_bar__bottom_nav_bar_button_trailer,
            objects.settings_page_bottom_nav_bar__bottom_nav_bar_button_power,
            objects.settings_page_bottom_nav_bar__bottom_nav_bar_button_air_quality,
            objects.settings_page_bottom_nav_bar__bottom_nav_bar_button_water,
            objects.settings_page_bottom_nav_bar__bottom_nav_bar_button_settings,
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

    int32_t btn = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (btn < 1 || btn > NUM_BUTTONS) return;

    const btn_config_t *cfg = &g_buttons[btn - 1];
    if (cfg->instance > 2 || cfg->channel > 7) return;

    uint8_t data[1] = { cfg->channel };
    uint32_t can_id = 0;
    switch ((module_type_t)cfg->module_type) {
        case MOD_TORRENT:
            can_id = TORRENT_TOGGLE_ID[cfg->instance];
            break;
        case MOD_SWITCHBACK:
            can_id = SWITCHBACK_TOGGLE_ID[cfg->instance];
            break;
        case MOD_NONE:
        default:
            ESP_LOGI(TAG, "button %ld unmapped, ignoring press", (long)btn);
            return;
    }

    if (can_send(can_id, data, 1)) {
        ESP_LOGI(TAG, "CAN TX 0x%02lX [%02X] (btn %ld -> %s%u ch%u)",
                 (unsigned long)can_id, data[0], (long)btn,
                 cfg->module_type == MOD_TORRENT ? "Torrent" : "Switchback",
                 cfg->instance, cfg->channel);
    } else {
        ESP_LOGW(TAG, "CAN TX 0x%02lX failed", (unsigned long)can_id);
    }
}

void action_change_desired_temperature(lv_event_t *e)
{
    int32_t val = lv_arc_get_value(objects.arc_thermostat);
    if (val < 35)  val = 35;
    if (val > 100) val = 100;
    if (val == get_var_desired_temperature()) return;

    set_var_desired_temperature(val);
    lv_label_set_text_fmt(objects.label_desired_temperature_value, "%d", (int)val);
    set_var_user_settings_changed(true);
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

// Defined in main.c
extern void    clock_set_timezone_index(int32_t idx);
extern int32_t clock_get_timezone_count(void);

void action_timezone_change(lv_event_t *e)
{
    uint16_t idx = lv_dropdown_get_selected(objects.drop_down_selected_time_zone);
    if ((int32_t)idx >= clock_get_timezone_count()) return;
    clock_set_timezone_index((int32_t)idx);
    set_var_user_settings_changed(true);
    ESP_LOGI(TAG, "timezone -> index %u", (unsigned)idx);
}

void action_temperature_unit_change(lv_event_t *e)
{
    int32_t unit = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (unit != 0 && unit != 1) return;
    set_var_temperature_unit(unit);
    set_var_user_settings_changed(true);
    ESP_LOGI(TAG, "temperature unit -> %s", unit == 1 ? "C" : "F");
}
void action_commit_mac_address_changes(lv_event_t *e) { }
void action_set_device_brightness_level(lv_event_t *e) { }
void action_show_device_brightness_dialog(lv_event_t *e) { }
void action_close_dialog(lv_event_t *e) { }

// ===========================================================================
// Button configuration UI — PageButtonEdit + PageDeviceAssign
// ===========================================================================

// Blinking cursor for LblButtonEditText. Since the label isn't a real
// textarea and gives no cursor on its own, we synthesize one by toggling
// a '|' on the end of the display text every 500 ms while PageButtonEdit
// is active. Keystrokes reset the phase to visible for instant feedback.
static bool         s_cursor_visible  = true;

static void update_edit_text_display(void)
{
    const char *txt = get_var_edit_label_text();
    if (!txt) txt = "";
    char buf[BTN_LABEL_MAX + 2];
    size_t n = strnlen(txt, BTN_LABEL_MAX - 1);
    memcpy(buf, txt, n);
    if (s_cursor_visible) {
        buf[n++] = '|';
    }
    buf[n] = '\0';
    if (objects.lbl_button_edit_text) {
        lv_label_set_text(objects.lbl_button_edit_text, buf);
    }
}

static void cursor_blink_cb(lv_timer_t *t)
{
    (void)t;
    if (g_active_screen != SCREEN_ID_PAGE_BUTTON_EDIT) return;
    s_cursor_visible = !s_cursor_visible;
    update_edit_text_display();
}

static void start_cursor_blink(void)
{
    static lv_timer_t *s_cursor_timer = NULL;
    if (s_cursor_timer == NULL) {
        s_cursor_timer = lv_timer_create(cursor_blink_cb, 500, NULL);
    }
    s_cursor_visible = true;
    update_edit_text_display();
}

static lv_obj_t *channel_dropdown(int ch)
{
    switch (ch) {
        case 0: return objects.dd_channel0_button;
        case 1: return objects.dd_channel1_button;
        case 2: return objects.dd_channel2_button;
        case 3: return objects.dd_channel3_button;
        case 4: return objects.dd_channel4_button;
        case 5: return objects.dd_channel5_button;
        case 6: return objects.dd_channel6_button;
        case 7: return objects.dd_channel7_button;
        default: return NULL;
    }
}

static lv_obj_t *instance_button(int inst)
{
    switch (inst) {
        case 0: return objects.btn_device_instance0;
        case 1: return objects.btn_device_instance1;
        case 2: return objects.btn_device_instance2;
        default: return NULL;
    }
}

static void refresh_channel_dropdowns(void)
{
    module_type_t mod = (module_type_t)get_var_assign_module_type();
    uint8_t inst = (uint8_t)get_var_assign_instance();
    for (int ch = 0; ch < 8; ch++) {
        lv_obj_t *dd = channel_dropdown(ch);
        if (!dd) continue;
        uint8_t btn = button_config_find(mod, inst, (uint8_t)ch);
        lv_dropdown_set_selected(dd, btn);  // 0 = None, 1..8 = Button N
    }
}

static void highlight_active_instance(void)
{
    int active = get_var_assign_instance();
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = instance_button(i);
        if (!b) continue;
        if (i == active) lv_obj_add_state(b, LV_STATE_CHECKED);
        else              lv_obj_clear_state(b, LV_STATE_CHECKED);
    }
}

// ---------------------------------------------------------------------------
// Icon slot highlight: mark whichever BtnIconSlot##'s codepoint matches the
// currently-selected EditIconCodepoint by adding LV_STATE_CHECKED. The
// StyleButtonDefault CHECKED state paints an accent-color bottom border so
// the active slot is visually distinct in both light and dark themes.
// ---------------------------------------------------------------------------
static lv_obj_t *icon_slot(int i)
{
    switch (i) {
        case  0: return objects.btn_icon_slot00;
        case  1: return objects.btn_icon_slot01;
        case  2: return objects.btn_icon_slot02;
        case  3: return objects.btn_icon_slot03;
        case  4: return objects.btn_icon_slot04;
        case  5: return objects.btn_icon_slot05;
        case  6: return objects.btn_icon_slot06;
        case  7: return objects.btn_icon_slot07;
        case  8: return objects.btn_icon_slot08;
        case  9: return objects.btn_icon_slot09;
        case 10: return objects.btn_icon_slot10;
        case 11: return objects.btn_icon_slot11;
        case 12: return objects.btn_icon_slot12;
        case 13: return objects.btn_icon_slot13;
        case 14: return objects.btn_icon_slot14;
        case 15: return objects.btn_icon_slot15;
        case 16: return objects.btn_icon_slot16;
        case 17: return objects.btn_icon_slot17;
        case 18: return objects.btn_icon_slot18;
        case 19: return objects.btn_icon_slot19;
        case 20: return objects.btn_icon_slot20;
        case 21: return objects.btn_icon_slot21;
        case 22: return objects.btn_icon_slot22;
        case 23: return objects.btn_icon_slot23;
        case 24: return objects.btn_icon_slot24;
        case 25: return objects.btn_icon_slot25;
        case 26: return objects.btn_icon_slot26;
        case 27: return objects.btn_icon_slot27;
        case 28: return objects.btn_icon_slot28;
        case 29: return objects.btn_icon_slot29;
        case 30: return objects.btn_icon_slot30;
        case 31: return objects.btn_icon_slot31;
        case 32: return objects.btn_icon_slot32;
        case 33: return objects.btn_icon_slot33;
        case 34: return objects.btn_icon_slot34;
        case 35: return objects.btn_icon_slot35;
        case 36: return objects.btn_icon_slot36;
        case 37: return objects.btn_icon_slot37;
        case 38: return objects.btn_icon_slot38;
        case 39: return objects.btn_icon_slot39;
        case 40: return objects.btn_icon_slot40;
        case 41: return objects.btn_icon_slot41;
        case 42: return objects.btn_icon_slot42;
        case 43: return objects.btn_icon_slot43;
        case 44: return objects.btn_icon_slot44;
        case 45: return objects.btn_icon_slot45;
        case 46: return objects.btn_icon_slot46;
        case 47: return objects.btn_icon_slot47;
        case 48: return objects.btn_icon_slot48;
        case 49: return objects.btn_icon_slot49;
        case 50: return objects.btn_icon_slot50;
        case 51: return objects.btn_icon_slot51;
        case 52: return objects.btn_icon_slot52;
        case 53: return objects.btn_icon_slot53;
        case 54: return objects.btn_icon_slot54;
        case 55: return objects.btn_icon_slot55;
        case 56: return objects.btn_icon_slot56;
        case 57: return objects.btn_icon_slot57;
        case 58: return objects.btn_icon_slot58;
        case 59: return objects.btn_icon_slot59;
        case 60: return objects.btn_icon_slot60;
        case 61: return objects.btn_icon_slot61;
        case 62: return objects.btn_icon_slot62;
        case 63: return objects.btn_icon_slot63;
        case 64: return objects.btn_icon_slot64;
        case 65: return objects.btn_icon_slot65;
        case 66: return objects.btn_icon_slot66;
        case 67: return objects.btn_icon_slot67;
        case 68: return objects.btn_icon_slot68;
        case 69: return objects.btn_icon_slot69;
        case 70: return objects.btn_icon_slot70;
        case 71: return objects.btn_icon_slot71;
        case 72: return objects.btn_icon_slot72;
        case 73: return objects.btn_icon_slot73;
        case 74: return objects.btn_icon_slot74;
        case 75: return objects.btn_icon_slot75;
        case 76: return objects.btn_icon_slot76;
        case 77: return objects.btn_icon_slot77;
        case 78: return objects.btn_icon_slot78;
        case 79: return objects.btn_icon_slot79;
        case 80: return objects.btn_icon_slot80;
        case 81: return objects.btn_icon_slot81;
        default: return NULL;
    }
}

static void highlight_selected_icon(uint16_t selected_cp)
{
    for (int i = 0; i < NUM_CURATED_ICONS; i++) {
        lv_obj_t *slot = icon_slot(i);
        if (!slot) continue;
        if (CURATED_ICONS[i] == selected_cp) {
            lv_obj_add_state(slot, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(slot, LV_STATE_CHECKED);
        }
    }
}

void action_navigate_to_button_edit(lv_event_t *e)
{
    int btn = (int)(intptr_t)lv_event_get_user_data(e);
    if (btn < 1 || btn > NUM_BUTTONS) return;

    const btn_config_t *cfg = &g_buttons[btn - 1];
    set_var_edit_btn_number(btn);
    set_var_edit_label_text(cfg->label);
    set_var_edit_icon_codepoint((int32_t)cfg->icon_codepoint);

    lv_label_set_text_fmt(objects.lbl_button_edit_header, "Button %d", btn);
    highlight_selected_icon(cfg->icon_codepoint);

    g_active_screen = SCREEN_ID_PAGE_BUTTON_EDIT;
    lv_disp_load_scr(objects.page_button_edit);
    start_cursor_blink();  // paints text with cursor suffix
}

void action_navigate_to_device_assign(lv_event_t *e)
{
    int mod = (int)(intptr_t)lv_event_get_user_data(e);
    if (mod != MOD_TORRENT && mod != MOD_SWITCHBACK) return;

    set_var_assign_module_type(mod);
    set_var_assign_instance(0);

    const char *hdr = (mod == MOD_TORRENT)
        ? "Assign Torrent Channels"
        : "Assign Switchback Relays";
    lv_label_set_text(objects.lbl_device_assign_header, hdr);

    refresh_channel_dropdowns();
    highlight_active_instance();

    g_active_screen = SCREEN_ID_PAGE_DEVICE_ASSIGN;
    lv_disp_load_scr(objects.page_device_assign);
}

void action_select_button_icon(lv_event_t *e)
{
    uint32_t cp = (uint32_t)(intptr_t)lv_event_get_user_data(e);
    if (cp == 0) return;
    set_var_edit_icon_codepoint((int32_t)cp);
    highlight_selected_icon((uint16_t)cp);
    ESP_LOGI(TAG, "icon selected: U+%04lX", (unsigned long)cp);
}

void action_save_button_appearance(lv_event_t *e)
{
    int btn = get_var_edit_btn_number();
    const char *lbl = get_var_edit_label_text();
    uint16_t icon = (uint16_t)get_var_edit_icon_codepoint();

    if (btn >= 1 && btn <= NUM_BUTTONS) {
        button_config_set_appearance((uint8_t)btn,
                                     (lbl && lbl[0]) ? lbl : NULL,
                                     icon);
        button_config_apply_to_ui();
        ESP_LOGI(TAG, "saved appearance for btn %d: \"%s\" icon=U+%04X",
                 btn, lbl ? lbl : "", icon);
    }

    g_active_screen = SCREEN_ID_PAGE_SETTINGS;
    lv_disp_load_scr(objects.page_settings);
}

void action_assign_channel(lv_event_t *e)
{
    int ch = (int)(intptr_t)lv_event_get_user_data(e);
    if (ch < 0 || ch >= 8) return;

    lv_obj_t *dd = channel_dropdown(ch);
    if (!dd) return;
    uint16_t sel = lv_dropdown_get_selected(dd);  // 0=None, 1..8=Button N

    module_type_t mod = (module_type_t)get_var_assign_module_type();
    uint8_t inst = (uint8_t)get_var_assign_instance();

    button_config_assign(mod, inst, (uint8_t)ch, (uint8_t)sel);
    button_config_apply_to_ui();
    refresh_channel_dropdowns();

    ESP_LOGI(TAG, "assign %s%u ch%d -> %s",
             mod == MOD_TORRENT ? "Torrent" : "Switchback",
             inst, ch, sel ? "Button" : "none");
}

void action_select_device_instance(lv_event_t *e)
{
    int inst = (int)(intptr_t)lv_event_get_user_data(e);
    if (inst < 0 || inst > 2) return;
    set_var_assign_instance(inst);
    refresh_channel_dropdowns();
    highlight_active_instance();
}

// ---------------------------------------------------------------------------
// Keyboard on PageButtonEdit: since the EEZ project has no LVGLTextareaWidget,
// we intercept keypresses directly from the keyboard and maintain the
// EditLabelText buffer ourselves, driving the LblButtonEditText display label.
// Attached once from ui_bind_button_edit_keyboard() after ui_init().
// ---------------------------------------------------------------------------
static void kb_button_edit_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);

    if (code == LV_EVENT_READY) {
        action_save_button_appearance(NULL);
        return;
    }
    if (code == LV_EVENT_CANCEL) {
        g_active_screen = SCREEN_ID_PAGE_SETTINGS;
        lv_disp_load_scr(objects.page_settings);
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED) return;

    uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
    if (btn_id == LV_BTNMATRIX_BTN_NONE) return;
    const char *txt = lv_btnmatrix_get_btn_text(kb, btn_id);
    if (!txt) return;

    // Mode-switch / control keys — let the default keyboard handler process
    if (strcmp(txt, "abc") == 0 || strcmp(txt, "ABC") == 0 ||
        strcmp(txt, "1#") == 0  || strcmp(txt, "Abc") == 0) {
        return;
    }

    char buf[BTN_LABEL_MAX];
    const char *cur = get_var_edit_label_text();
    strncpy(buf, cur ? cur : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    size_t len = strlen(buf);

    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        if (len > 0) buf[len - 1] = '\0';
    } else if (strcmp(txt, LV_SYMBOL_OK) == 0 ||
               strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
        action_save_button_appearance(NULL);
        return;
    } else if (strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
        // Ignore — user should use Save/Cancel buttons
        return;
    } else {
        size_t tl = strlen(txt);
        if (len + tl < sizeof(buf)) {
            memcpy(buf + len, txt, tl);
            buf[len + tl] = '\0';
        }
    }

    set_var_edit_label_text(buf);
    // Snap cursor back to visible on every keystroke so the user sees
    // immediate visual response even mid-blink.
    s_cursor_visible = true;
    update_edit_text_display();
}

// Called from main.c once after ui_init()
void ui_bind_button_edit_keyboard(void)
{
    if (objects.kb_button_edit) {
        lv_obj_add_event_cb(objects.kb_button_edit, kb_button_edit_event_cb,
                            LV_EVENT_ALL, NULL);
    }
}

void action_all_on_off(lv_event_t *e)
{
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;

    static int64_t last_send_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_send_us < 300000) return;
    last_send_us = now;

    bool any_on = false;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (g_buttons[i].module_type != MOD_NONE && g_button_state[i] > 0) {
            any_on = true;
            break;
        }
    }
    bool want_off = any_on;

    int sent = 0;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        const btn_config_t *cfg = &g_buttons[i];
        if (cfg->module_type == MOD_NONE) continue;
        if (cfg->instance > 2 || cfg->channel > 7) continue;

        bool is_on = g_button_state[i] > 0;
        if (want_off == is_on) {
            uint32_t can_id = (cfg->module_type == MOD_TORRENT)
                ? TORRENT_TOGGLE_ID[cfg->instance]
                : SWITCHBACK_TOGGLE_ID[cfg->instance];
            uint8_t data[1] = { cfg->channel };
            if (can_send(can_id, data, 1)) {
                sent++;
            }
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }
    ESP_LOGI(TAG, "All %s: sent %d toggle frames", want_off ? "Off" : "On", sent);
}
