#include "include/wifi_config.h"
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

#define NVS_NAMESPACE   "wifi"
#define NVS_KEY_SSID    "ssid"
#define NVS_KEY_PASS    "password"
#define CONFIG_TIMEOUT_US (5 * 1000 * 1000) // 5 seconds

static nvs_handle_t nvs_hdl;
static char s_hostname[16];  // "esp32-XXYYZZ"
static char s_ssid[33];
static char s_password[64];
static bool s_has_credentials = false;
static esp_netif_t *s_netif = NULL;
static volatile bool s_wifi_connecting = false;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_connecting) {
            ESP_LOGW(TAG, "WiFi disconnected, retrying...");
            esp_wifi_connect();
        }
    }
}

// Multi-message receive state machine
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

esp_err_t wifi_config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_hdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    memset(&state, 0, sizeof(state));

    // Build hostname from MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_hostname, sizeof(s_hostname), "esp32-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device hostname: %s", s_hostname);

    return ESP_OK;
}

bool wifi_config_load(void)
{
    size_t s_len = sizeof(s_ssid);
    size_t p_len = sizeof(s_password);

    if (nvs_get_str(nvs_hdl, NVS_KEY_SSID, s_ssid, &s_len) != ESP_OK) return false;
    if (nvs_get_str(nvs_hdl, NVS_KEY_PASS, s_password, &p_len) != ESP_OK) return false;
    if (s_len <= 1) return false;

    s_has_credentials = true;
    ESP_LOGI(TAG, "Loaded SSID: %s", s_ssid);
    return true;
}

bool wifi_config_has_credentials(void)
{
    return s_has_credentials;
}

const char *wifi_config_get_hostname(void)
{
    return s_hostname;
}

bool wifi_connect(void)
{
    if (!s_has_credentials) {
        ESP_LOGE(TAG, "No WiFi credentials — cannot connect");
        return false;
    }

    ESP_LOGI(TAG, "Connecting to WiFi: %s", s_ssid);

    if (s_netif == NULL) {
        esp_netif_init();
        esp_event_loop_create_default();
        s_netif = esp_netif_create_default_wifi_sta();
        esp_netif_set_hostname(s_netif, s_hostname);
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                   &wifi_event_handler, NULL);
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, s_password, sizeof(wifi_config.sta.password) - 1);

    s_wifi_connecting = true;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();

    // Wait for AP association AND DHCP IP assignment (up to 15 seconds)
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
    ESP_LOGI(TAG, "Saving SSID: %s", ssid);
    if (nvs_set_str(nvs_hdl, NVS_KEY_SSID, ssid) != ESP_OK) return false;
    if (nvs_set_str(nvs_hdl, NVS_KEY_PASS, password) != ESP_OK) return false;
    if (nvs_commit(nvs_hdl) != ESP_OK) return false;

    // Update cached credentials
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_password, password, sizeof(s_password) - 1);
    s_password[sizeof(s_password) - 1] = '\0';
    s_has_credentials = true;

    ESP_LOGI(TAG, "Credentials saved to NVS");
    return true;
}

void wifi_config_handle_can(const uint8_t *data, uint8_t length)
{
    if (length < 1) return;

    switch (data[0]) {
    case 0x01: {
        // Start: data[1]=ssid_len, data[2]=pass_len
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
        // SSID chunk
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
        // Password chunk
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
        // End with checksum
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
