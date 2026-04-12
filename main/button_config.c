#include "button_config.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lvgl.h"
#include "ui/screens.h"

static const char *TAG = "btncfg";

#define NVS_NAMESPACE   "btnmap"
#define NVS_KEY_BLOB    "buttons"
#define BLOB_MAGIC      0x424E5443u   /* 'BTNC' */
#define BLOB_VERSION    1

const uint32_t TORRENT_TOGGLE_ID[3]    = { 0x18, 0x19, 0x1A };
const uint32_t TORRENT_STATUS_ID[3]    = { 0x1B, 0x1C, 0x1D };
const uint32_t SWITCHBACK_TOGGLE_ID[3] = { 0x25, 0x26, 0x27 };
const uint32_t SWITCHBACK_STATUS_ID[3] = { 0x28, 0x29, 0x2A };

// 80 curated FontAwesome codepoints. MUST match the order used by
// eez_patch2.py when generating the BtnIconSlot00..79 buttons, because
// slot N's click handler sends userData = CURATED_ICONS[N] and the
// highlight logic walks this array in parallel with the slot buttons.
const uint16_t CURATED_ICONS[NUM_CURATED_ICONS] = {
    /* Lights & electrical */
    0xf7b6, 0xf0eb, 0xf185, 0xf186, 0xf0e7, 0xf1e6, 0xf240,
    /* Climate / temperature */
    0xf2dc, 0xf06d, 0xf2e7, 0xf6fc, 0xf5e7, 0xf0c2,
    /* Water / plumbing */
    0xf7c0, 0xf7e4, 0xf5aa,
    /* Kitchen / food */
    0xf863, 0xf54f, 0xf0f4,
    /* Home / structure / doors */
    0xf015, 0xf1ad, 0xf52a, 0xf2f6, 0xf023, 0xf084,
    /* Outdoors / vehicles */
    0xf1bb, 0xf5fd, 0xf0d1, 0xf494, 0xf1b9, 0xf206, 0xf06c, 0xf0ac,
    /* Sun / shade / weather */
    0xf0e9,
    /* Media / entertainment */
    0xf083, 0xf008, 0xf001, 0xf025, 0xf04b, 0xf04a, 0xf04d, 0xf11b,
    /* People / communication */
    0xf095, 0xf0c0, 0xf007, 0xf183, 0xf0f3, 0xf075,
    /* Data / system (slimmed down — UI-only glyphs removed) */
    0xf013, 0xf1de,
    /* Productivity / information */
    0xf004, 0xf005, 0xf164, 0xf017, 0xf133, 0xf073, 0xf002, 0xf05a,
    0xf09d, 0xf0b1, 0xf0c6, 0xf02d,
    /* Physical space / rooms */
    0xf236, 0xf2cd, 0xf553, 0xf794, 0xf290,
    /* Safety / health */
    0xf0f1, 0xf21b, 0xf06e, 0xf132,
    /* Navigation / location */
    0xf3c5, 0xf041, 0xf072, 0xf135,
    /* Misc */
    0xf0ee, 0xf06b, 0xf1f8, 0xf065, 0xf1eb,
};

btn_config_t     g_buttons[NUM_BUTTONS];
volatile uint8_t g_button_state[NUM_BUTTONS] = {0};

typedef struct {
    uint32_t     magic;
    uint16_t     version;
    uint16_t     reserved;
    btn_config_t btns[NUM_BUTTONS];
} button_config_blob_t;

static nvs_handle_t s_nvs;
static bool         s_nvs_open = false;

static void load_defaults(void)
{
    for (int i = 0; i < NUM_BUTTONS; i++) {
        memset(&g_buttons[i], 0, sizeof(g_buttons[i]));
        g_buttons[i].module_type    = MOD_NONE;
        g_buttons[i].icon_codepoint = DEFAULT_ICON_CP;
        snprintf(g_buttons[i].label, BTN_LABEL_MAX, "Device %d", i + 1);
    }
}

static void open_nvs(void)
{
    if (s_nvs_open) return;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %d", NVS_NAMESPACE, err);
        return;
    }
    s_nvs_open = true;
}

void button_config_init(void)
{
    open_nvs();
    if (!s_nvs_open) {
        load_defaults();
        return;
    }

    button_config_blob_t blob;
    size_t sz = sizeof(blob);
    esp_err_t err = nvs_get_blob(s_nvs, NVS_KEY_BLOB, &blob, &sz);
    if (err != ESP_OK || sz != sizeof(blob) ||
        blob.magic != BLOB_MAGIC || blob.version != BLOB_VERSION) {
        ESP_LOGI(TAG, "no valid button config in NVS, writing defaults");
        load_defaults();
        button_config_save();
        return;
    }
    memcpy(g_buttons, blob.btns, sizeof(g_buttons));

    for (int i = 0; i < NUM_BUTTONS; i++) {
        g_buttons[i].label[BTN_LABEL_MAX - 1] = '\0';
    }
    ESP_LOGI(TAG, "loaded button config from NVS");
}

void button_config_save(void)
{
    if (!s_nvs_open) return;

    button_config_blob_t blob = {
        .magic    = BLOB_MAGIC,
        .version  = BLOB_VERSION,
        .reserved = 0,
    };
    memcpy(blob.btns, g_buttons, sizeof(g_buttons));

    esp_err_t err = nvs_set_blob(s_nvs, NVS_KEY_BLOB, &blob, sizeof(blob));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %d", err);
        return;
    }
    nvs_commit(s_nvs);
}

const char *utf8_encode(uint16_t cp, char out[5])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        out[1] = 0;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        out[2] = 0;
    } else {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = 0;
    }
    return out;
}

// Copy a label into dst, replacing every ASCII space with '\n'. This gives
// multi-word button labels a stacked look ("Kitchen Lights" -> "Kitchen\n
// Lights") that matches the hand-styled device buttons from before the
// local-config rework, while leaving the original text intact in NVS.
static void apply_linebreaks(const char *src, char *dst, size_t dst_sz)
{
    if (!src || !dst || dst_sz == 0) return;
    size_t i = 0;
    for (; src[i] != '\0' && i < dst_sz - 1; i++) {
        dst[i] = (src[i] == ' ') ? '\n' : src[i];
    }
    dst[i] = '\0';
}

void button_config_apply_to_ui(void)
{
    lv_obj_t *home_labels[NUM_BUTTONS] = {
        objects.lbl_device01_label, objects.lbl_device02_label,
        objects.lbl_device03_label, objects.lbl_device04_label,
        objects.lbl_device05_label, objects.lbl_device06_label,
        objects.lbl_device07_label, objects.lbl_device08_label,
    };
    lv_obj_t *home_icons[NUM_BUTTONS] = {
        objects.lbl_device01_status_ind, objects.lbl_device02_status_ind,
        objects.lbl_device03_status_ind, objects.lbl_device04_status_ind,
        objects.lbl_device05_status_ind, objects.lbl_device06_status_ind,
        objects.lbl_device07_status_ind, objects.lbl_device08_status_ind,
    };
    lv_obj_t *edit_labels[NUM_BUTTONS] = {
        objects.lbl_edit_btn01_label, objects.lbl_edit_btn02_label,
        objects.lbl_edit_btn03_label, objects.lbl_edit_btn04_label,
        objects.lbl_edit_btn05_label, objects.lbl_edit_btn06_label,
        objects.lbl_edit_btn07_label, objects.lbl_edit_btn08_label,
    };
    lv_obj_t *edit_icons[NUM_BUTTONS] = {
        objects.lbl_edit_btn01_icon, objects.lbl_edit_btn02_icon,
        objects.lbl_edit_btn03_icon, objects.lbl_edit_btn04_icon,
        objects.lbl_edit_btn05_icon, objects.lbl_edit_btn06_icon,
        objects.lbl_edit_btn07_icon, objects.lbl_edit_btn08_icon,
    };

    char icon_buf[5];
    char lbl_buf[BTN_LABEL_MAX];
    for (int i = 0; i < NUM_BUTTONS; i++) {
        apply_linebreaks(g_buttons[i].label, lbl_buf, sizeof(lbl_buf));
        const char *icon = utf8_encode(g_buttons[i].icon_codepoint, icon_buf);

        if (home_labels[i]) lv_label_set_text(home_labels[i], lbl_buf);
        if (home_icons[i])  lv_label_set_text(home_icons[i],  icon);
        if (edit_labels[i]) lv_label_set_text(edit_labels[i], lbl_buf);
        if (edit_icons[i])  lv_label_set_text(edit_icons[i],  icon);
    }
}

uint8_t button_config_find(module_type_t module, uint8_t instance, uint8_t channel)
{
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (g_buttons[i].module_type == (uint8_t)module &&
            g_buttons[i].instance    == instance &&
            g_buttons[i].channel     == channel) {
            return (uint8_t)(i + 1);
        }
    }
    return 0;
}

void button_config_assign(module_type_t module, uint8_t instance,
                          uint8_t channel, uint8_t btn_number)
{
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (g_buttons[i].module_type == (uint8_t)module &&
            g_buttons[i].instance    == instance &&
            g_buttons[i].channel     == channel) {
            g_buttons[i].module_type = MOD_NONE;
            g_buttons[i].instance    = 0;
            g_buttons[i].channel     = 0;
            g_button_state[i]        = 0;
        }
    }
    if (btn_number >= 1 && btn_number <= NUM_BUTTONS) {
        g_buttons[btn_number - 1].module_type = (uint8_t)module;
        g_buttons[btn_number - 1].instance    = instance;
        g_buttons[btn_number - 1].channel     = channel;
    }
    button_config_save();
}

void button_config_set_appearance(uint8_t btn_number, const char *label, uint16_t icon_cp)
{
    if (btn_number < 1 || btn_number > NUM_BUTTONS) return;
    btn_config_t *b = &g_buttons[btn_number - 1];
    if (label) {
        strncpy(b->label, label, BTN_LABEL_MAX - 1);
        b->label[BTN_LABEL_MAX - 1] = '\0';
    }
    if (icon_cp) {
        b->icon_codepoint = icon_cp;
    }
    button_config_save();
}
