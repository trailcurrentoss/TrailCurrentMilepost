/*
 * wifi_config.c — Milepost's CAN-provisioned WiFi bridge.
 *
 * Headwaters broadcasts WiFi credentials over CAN ID 0x01 (multi-frame:
 * start / SSID chunks / password chunks / end+checksum) to every module
 * on the bus. That broadcast format is FIXED — this firmware adapts to
 * it, not the other way around.
 *
 * Storage: all creds live in the "milepost" NVS namespace (keys
 * "wifi_ssid" and "wifi_pass") managed by the milepost_config component.
 * That's the single source of truth shared with the in-UI WiFi wizard
 * (actions.c → milepost_config_set_wifi). OTA (ota.c) and Discovery
 * (discovery.c) both call wifi_connect() below, which reads from
 * milepost_config, so both provisioning paths converge automatically.
 *
 * Migration: legacy Milepost builds stored creds in NVS namespace "wifi"
 * with keys "ssid"/"password". On first boot of this firmware,
 * wifi_config_init() copies those into "milepost" and erases the old
 * keys so we don't have to check two places for the life of the device.
 */
#include "include/wifi_config.h"
#include "milepost_config.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_cfg";

#define CONFIG_TIMEOUT_US (5 * 1000 * 1000) /* 5 s partial-message reset */

static char s_hostname[16];  /* "esp32-XXYYZZ" */
static esp_netif_t *s_netif = NULL;
static volatile bool s_wifi_connecting = false;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_connecting) {
            ESP_LOGW(TAG, "WiFi disconnected, retrying...");
            esp_wifi_connect();
        }
    }
}

/* Multi-frame CAN 0x01 receive state machine — matches Headwaters' broadcast
 * shape byte-for-byte. Do NOT change without coordinating a Headwaters change. */
static struct {
    bool receiving;
    uint8_t ssid_len;
    uint8_t pass_len;
    uint8_t ssid_received;
    uint8_t pass_received;
    char ssid_buf[33];
    char pass_buf[64];
    int64_t last_msg_time;
} state;

/* Migrate legacy "wifi" NVS namespace → "milepost". One-shot; runs once on
 * first boot of this firmware on a device that was provisioned by the
 * older Milepost image. */
static void migrate_legacy_wifi_nvs(void)
{
    if (milepost_config_has_wifi()) return;  /* fresh cred store already populated */

    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) return;

    char ssid[33] = {0};
    char pass[64] = {0};
    size_t ssz = sizeof(ssid), psz = sizeof(pass);
    esp_err_t rs = nvs_get_str(h, "ssid",     ssid, &ssz);
    esp_err_t rp = nvs_get_str(h, "password", pass, &psz);

    if (rs == ESP_OK && ssid[0]) {
        ESP_LOGI(TAG, "Migrating legacy WiFi creds from NVS \"wifi\" → \"milepost\": SSID=%s", ssid);
        milepost_config_set_wifi(ssid, rp == ESP_OK ? pass : "");
        /* Wipe the old keys so this migration is a one-shot. Also erases the
         * legacy namespace's readback so a future rollback to old firmware
         * won't silently re-use stale creds. */
        nvs_erase_key(h, "ssid");
        nvs_erase_key(h, "password");
        nvs_commit(h);
    }
    nvs_close(h);
}

esp_err_t wifi_config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    /* milepost_config is the persistent WiFi store — bring it up first so
     * the migration below has somewhere to write. Idempotent — safe if
     * app_main also calls it. */
    milepost_config_init();
    migrate_legacy_wifi_nvs();

    memset(&state, 0, sizeof(state));

    /* Hostname from MAC — matches the legacy pattern so mDNS names stay
     * stable across the firmware upgrade. */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_hostname, sizeof(s_hostname), "esp32-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device hostname: %s", s_hostname);

    return ESP_OK;
}

bool wifi_config_load(void)
{
    /* milepost_config already loaded its cache in _init; just report status. */
    if (milepost_config_has_wifi()) {
        const milepost_config_t *cfg = milepost_config_get();
        ESP_LOGI(TAG, "Loaded SSID: %s", cfg->wifi_ssid);
        return true;
    }
    return false;
}

bool wifi_config_has_credentials(void)
{
    return milepost_config_has_wifi();
}

const char *wifi_config_get_hostname(void)
{
    return s_hostname;
}

bool wifi_connect(void)
{
    if (!milepost_config_has_wifi()) {
        ESP_LOGE(TAG, "No WiFi credentials — cannot connect");
        return false;
    }
    const milepost_config_t *cfg = milepost_config_get();

    ESP_LOGI(TAG, "Connecting to WiFi: %s", cfg->wifi_ssid);

    if (s_netif == NULL) {
        esp_netif_init();
        esp_event_loop_create_default();
        s_netif = esp_netif_create_default_wifi_sta();
        esp_netif_set_hostname(s_netif, s_hostname);
        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&init_cfg);
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                   &wifi_event_handler, NULL);
    }

    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid,     cfg->wifi_ssid, sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, cfg->wifi_pass, sizeof(wcfg.sta.password) - 1);

    s_wifi_connecting = true;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    esp_wifi_start();
    esp_wifi_connect();

    /* Wait for AP association AND DHCP IP assignment (up to 15 s). */
    for (int i = 0; i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(s_netif, &ip_info);
        if (ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&ip_info.ip));
            return true;
        }
    }
    s_wifi_connecting = false;
    ESP_LOGE(TAG, "WiFi connection failed (no IP assigned)");
    return false;
}

void wifi_disconnect(void)
{
    s_wifi_connecting = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    ESP_LOGI(TAG, "WiFi disconnected");
}

static bool save_credentials(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Saving SSID (CAN-provisioned): %s", ssid);
    /* Single write path: milepost_config owns the NVS. UI wizard writes
     * through the same call, so both provisioning paths update the same
     * store. */
    return milepost_config_set_wifi(ssid, password ? password : "") == ESP_OK;
}

void wifi_config_handle_can(const uint8_t *data, uint8_t length)
{
    if (length < 1) return;

    switch (data[0]) {
    case 0x01: {
        /* Start: data[1]=ssid_len, data[2]=pass_len */
        memset(&state, 0, sizeof(state));
        state.receiving = true;
        state.ssid_len = data[1];
        state.pass_len = data[2];
        state.last_msg_time = esp_timer_get_time();
        ESP_LOGI(TAG, "Config start: SSID %d bytes, pass %d bytes",
                 state.ssid_len, state.pass_len);
        break;
    }
    case 0x02: {
        /* SSID chunk */
        if (!state.receiving) break;
        uint8_t n = length - 2;
        uint8_t rem = state.ssid_len - state.ssid_received;
        if (n > rem) n = rem;
        if (state.ssid_received + n <= 32) {
            memcpy(state.ssid_buf + state.ssid_received, &data[2], n);
            state.ssid_received += n;
        }
        state.last_msg_time = esp_timer_get_time();
        break;
    }
    case 0x03: {
        /* Password chunk */
        if (!state.receiving) break;
        uint8_t n = length - 2;
        uint8_t rem = state.pass_len - state.pass_received;
        if (n > rem) n = rem;
        if (state.pass_received + n <= 63) {
            memcpy(state.pass_buf + state.pass_received, &data[2], n);
            state.pass_received += n;
        }
        state.last_msg_time = esp_timer_get_time();
        break;
    }
    case 0x04: {
        /* End with checksum */
        if (!state.receiving) break;
        state.receiving = false;

        uint8_t checksum = 0;
        for (uint8_t i = 0; i < state.ssid_received; i++) checksum ^= state.ssid_buf[i];
        for (uint8_t i = 0; i < state.pass_received; i++) checksum ^= state.pass_buf[i];

        if (checksum == data[1] &&
            state.ssid_received == state.ssid_len &&
            state.pass_received == state.pass_len) {
            state.ssid_buf[state.ssid_received] = '\0';
            state.pass_buf[state.pass_received] = '\0';
            save_credentials(state.ssid_buf, state.pass_buf);
        } else {
            ESP_LOGE(TAG, "Checksum mismatch or incomplete data");
        }
        break;
    }
    default:
        ESP_LOGW(TAG, "Unknown config message type: 0x%02X", data[0]);
    }
}

void wifi_config_check_timeout(void)
{
    if (state.receiving && (esp_timer_get_time() - state.last_msg_time > CONFIG_TIMEOUT_US)) {
        ESP_LOGW(TAG, "Config timeout — resetting");
        memset(&state, 0, sizeof(state));
    }
}
