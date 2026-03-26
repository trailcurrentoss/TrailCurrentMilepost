#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h"
#include "lvgl.h"

#include "include/ota.h"

static const char *TAG = "ota";

#define OTA_TIMEOUT_US      (180 * 1000000LL)  // 180 seconds
#define WIFI_TIMEOUT_US     (15 * 1000000LL)   // 15 seconds
#define OTA_HTTP_PORT       3232
#define OTA_BUF_SIZE        4096

// ============================================================================
// State machine
// ============================================================================
typedef enum {
    OTA_IDLE,
    OTA_START,
    OTA_CONNECTING,
    OTA_WAITING,
    OTA_UPLOADING,
    OTA_COMPLETE,
    OTA_FAILED,
} ota_state_t;

static volatile ota_state_t ota_state = OTA_IDLE;
static volatile bool ota_triggered = false;
static volatile int ota_progress_pct = 0;

// Device MAC bytes 3-5 for CAN trigger matching
static uint8_t device_mac[3] = {0};
static char hostname[20] = {0};

// WiFi / HTTP state (only valid when OTA active)
static esp_netif_t *sta_netif = NULL;
static httpd_handle_t http_server = NULL;
static EventGroupHandle_t wifi_events = NULL;
static int64_t state_start_us = 0;

// LVGL overlay (only touched from main loop)
static lv_obj_t *ota_overlay = NULL;
static lv_obj_t *ota_label = NULL;

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

// ============================================================================
// WiFi event handler
// ============================================================================
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(wifi_events, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

// ============================================================================
// HTTP OTA upload handler (runs in httpd task context)
// ============================================================================
static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        ota_state = OTA_FAILED;
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        ota_state = OTA_FAILED;
        return ESP_FAIL;
    }

    ota_state = OTA_UPLOADING;
    int total = req->content_len;
    int received = 0;
    char buf[OTA_BUF_SIZE];

    ESP_LOGI(TAG, "Receiving firmware (%d bytes) to partition '%s'", total, partition->label);

    while (received < total) {
        int len = httpd_req_recv(req, buf, sizeof(buf));
        if (len <= 0) {
            if (len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Receive error");
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            ota_state = OTA_FAILED;
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            ota_state = OTA_FAILED;
            return ESP_FAIL;
        }

        received += len;
        if (total > 0) {
            ota_progress_pct = (received * 100) / total;
        }
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        ota_state = OTA_FAILED;
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot failed");
        ota_state = OTA_FAILED;
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OK");
    ESP_LOGI(TAG, "OTA complete, setting state to reboot");
    ota_state = OTA_COMPLETE;
    return ESP_OK;
}

// ============================================================================
// LVGL overlay (called only from main loop)
// ============================================================================
static void overlay_create(const char *text)
{
    ota_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ota_overlay);
    lv_obj_set_size(ota_overlay, 800, 480);
    lv_obj_set_style_bg_color(ota_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ota_overlay, LV_OPA_80, 0);
    lv_obj_clear_flag(ota_overlay, LV_OBJ_FLAG_SCROLLABLE);

    ota_label = lv_label_create(ota_overlay);
    lv_obj_set_style_text_color(ota_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(ota_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(ota_label, text);
    lv_obj_center(ota_label);
}

static void overlay_update(const char *text)
{
    if (ota_label) lv_label_set_text(ota_label, text);
}

static void overlay_destroy(void)
{
    if (ota_overlay) {
        lv_obj_del(ota_overlay);
        ota_overlay = NULL;
        ota_label = NULL;
    }
}

// ============================================================================
// WiFi start / stop
// ============================================================================
static bool wifi_start(const char *ssid, const char *password)
{
    wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_set_hostname(sta_netif, hostname);
    esp_wifi_connect();

    ESP_LOGI(TAG, "WiFi connecting to '%s' as %s", ssid, hostname);
    return true;
}

static void wifi_stop(void)
{
    if (http_server) {
        httpd_stop(http_server);
        http_server = NULL;
    }

    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler);

    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();

    if (sta_netif) {
        esp_netif_destroy_default_wifi(sta_netif);
        sta_netif = NULL;
    }

    esp_event_loop_delete_default();

    if (wifi_events) {
        vEventGroupDelete(wifi_events);
        wifi_events = NULL;
    }

    ESP_LOGI(TAG, "WiFi torn down");
}

// ============================================================================
// HTTP server
// ============================================================================
static bool http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = OTA_HTTP_PORT;
    config.stack_size = 8192;

    if (httpd_start(&http_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return false;
    }

    httpd_uri_t update_uri = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = ota_upload_handler,
    };
    httpd_register_uri_handler(http_server, &update_uri);

    ESP_LOGI(TAG, "HTTP OTA server started on port %d", OTA_HTTP_PORT);
    return true;
}

// ============================================================================
// Public API
// ============================================================================
void ota_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    device_mac[0] = mac[3];
    device_mac[1] = mac[4];
    device_mac[2] = mac[5];
    snprintf(hostname, sizeof(hostname), "esp32-%02X%02X%02X",
             device_mac[0], device_mac[1], device_mac[2]);
    ESP_LOGI(TAG, "OTA hostname: %s", hostname);
}

void ota_check_can_trigger(const twai_message_t *msg)
{
    if (ota_state != OTA_IDLE) return;
    if (msg->data_length_code < 3) return;

    if (msg->data[0] == device_mac[0] &&
        msg->data[1] == device_mac[1] &&
        msg->data[2] == device_mac[2]) {
        ESP_LOGI(TAG, "OTA trigger matched");
        ota_triggered = true;
    }
}

void ota_process(void)
{
    static int last_pct = -1;

    switch (ota_state) {

    case OTA_IDLE:
        if (!ota_triggered) return;
        ota_triggered = false;
        ota_state = OTA_START;
        // fall through

    case OTA_START: {
        // Read WiFi credentials from NVS
        nvs_handle_t h;
        char ssid[33] = {0};
        char pass[64] = {0};
        size_t ssid_len = sizeof(ssid);
        size_t pass_len = sizeof(pass);

        if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) {
            overlay_create("OTA: No WiFi credentials");
            ESP_LOGW(TAG, "No wifi NVS namespace");
            state_start_us = esp_timer_get_time();
            ota_state = OTA_FAILED;
            return;
        }
        bool ok = (nvs_get_str(h, "ssid", ssid, &ssid_len) == ESP_OK &&
                   nvs_get_str(h, "password", pass, &pass_len) == ESP_OK &&
                   strlen(ssid) > 0);
        nvs_close(h);

        if (!ok) {
            overlay_create("OTA: No WiFi credentials stored");
            ESP_LOGW(TAG, "WiFi credentials missing or empty");
            state_start_us = esp_timer_get_time();
            ota_state = OTA_FAILED;
            return;
        }

        overlay_create("OTA: Connecting to WiFi...");
        wifi_start(ssid, pass);
        state_start_us = esp_timer_get_time();
        ota_state = OTA_CONNECTING;
        return;
    }

    case OTA_CONNECTING: {
        EventBits_t bits = xEventGroupWaitBits(wifi_events,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, 0);

        if (bits & WIFI_CONNECTED_BIT) {
            // Get IP address for display
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(sta_netif, &ip_info);
            char msg[80];
            snprintf(msg, sizeof(msg), "OTA Ready\n" IPSTR ":%d\nWaiting for upload...",
                     IP2STR(&ip_info.ip), OTA_HTTP_PORT);
            overlay_update(msg);

            if (!http_start()) {
                overlay_update("OTA: HTTP server failed");
                state_start_us = esp_timer_get_time();
                ota_state = OTA_FAILED;
                return;
            }
            state_start_us = esp_timer_get_time();
            ota_state = OTA_WAITING;
            return;
        }

        if ((bits & WIFI_FAIL_BIT) ||
            (esp_timer_get_time() - state_start_us > WIFI_TIMEOUT_US)) {
            overlay_update("OTA: WiFi connect failed");
            ESP_LOGW(TAG, "WiFi connection failed/timeout");
            state_start_us = esp_timer_get_time();
            ota_state = OTA_FAILED;
            return;
        }
        return;
    }

    case OTA_WAITING: {
        if (esp_timer_get_time() - state_start_us > OTA_TIMEOUT_US) {
            ESP_LOGI(TAG, "OTA timeout, resuming normal operation");
            overlay_update("OTA: Timeout");
            state_start_us = esp_timer_get_time();
            ota_state = OTA_FAILED;
        }
        return;
    }

    case OTA_UPLOADING: {
        int pct = ota_progress_pct;
        if (pct != last_pct) {
            char msg[40];
            snprintf(msg, sizeof(msg), "OTA: Uploading... %d%%", pct);
            overlay_update(msg);
            last_pct = pct;
        }
        return;
    }

    case OTA_COMPLETE: {
        overlay_update("OTA complete!\nRebooting...");
        ESP_LOGI(TAG, "Rebooting into new firmware");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        return;  // unreachable
    }

    case OTA_FAILED: {
        // Show error for 3 seconds then clean up
        if (esp_timer_get_time() - state_start_us > 3000000LL) {
            wifi_stop();
            overlay_destroy();
            ota_progress_pct = 0;
            last_pct = -1;
            ota_state = OTA_IDLE;
            ESP_LOGI(TAG, "OTA cleanup done, resuming normal operation");
        }
        return;
    }
    }
}
