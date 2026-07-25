#include "include/ota.h"
#include "include/wifi_config.h"
#include "include/discovery.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mdns.h"
#include "lvgl.h"

static const char *TAG = "ota";

// ---------------------------------------------------------------------------
// OTA state
// ---------------------------------------------------------------------------

static volatile bool s_ota_running = false;
static volatile bool s_ota_complete = false;

// UI state communicated to main loop (LVGL is not thread-safe)
typedef enum {
    OTA_UI_NONE,
    OTA_UI_CONNECTING,
    OTA_UI_READY,
    OTA_UI_UPLOADING,
    OTA_UI_COMPLETE,
    OTA_UI_FAILED,
    OTA_UI_CLEANUP,
} ota_ui_state_t;

static volatile ota_ui_state_t s_ui_state = OTA_UI_NONE;
static volatile char s_ui_msg[80] = {0};
static volatile int s_ui_progress = 0;
static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_label = NULL;

// ---------------------------------------------------------------------------
// HTTP OTA server — accepts firmware upload at POST /ota
// ---------------------------------------------------------------------------

static esp_ota_handle_t s_ota_handle;
static const esp_partition_t *s_update_partition;

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA upload started (%d bytes)", req->content_len);

    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition available");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_begin(s_update_partition, OTA_WITH_SEQUENTIAL_WRITES, &s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char buf[1024];
    int received = 0;
    int total = 0;

    s_ui_state = OTA_UI_UPLOADING;

    while (total < req->content_len) {
        received = httpd_req_recv(req, buf, sizeof(buf));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "OTA receive error");
            esp_ota_abort(s_ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        err = esp_ota_write(s_ota_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(s_ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return ESP_FAIL;
        }

        total += received;
        if (req->content_len > 0) {
            s_ui_progress = (total * 100) / req->content_len;
        }
        if ((total % (64 * 1024)) == 0 || total == req->content_len) {
            ESP_LOGI(TAG, "OTA progress: %d / %d bytes (%d%%)",
                     total, req->content_len, s_ui_progress);
        }
    }

    err = esp_ota_end(s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(s_update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA upload complete, rebooting...");
    httpd_resp_sendstr(req, "OTA OK, rebooting...\n");

    s_ota_complete = true;
    return ESP_OK;
}

static httpd_handle_t start_ota_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    httpd_uri_t ota_uri = {
        .uri       = "/ota",
        .method    = HTTP_POST,
        .handler   = ota_post_handler,
    };
    httpd_register_uri_handler(server, &ota_uri);

    ESP_LOGI(TAG, "OTA HTTP server started on port %d", config.server_port);
    ESP_LOGI(TAG, "Upload firmware: curl -X POST http://%s.local/ota --data-binary @build/trailcurrent_milepost.bin",
             wifi_config_get_hostname());

    return server;
}

// ---------------------------------------------------------------------------
// OTA task — runs on its own FreeRTOS task to avoid blocking CAN/LVGL
// ---------------------------------------------------------------------------

static void ota_task_fn(void *arg)
{
    ESP_LOGI(TAG, "=== Entering OTA mode ===");
    snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "OTA: Connecting to WiFi...");
    s_ui_state = OTA_UI_CONNECTING;

    if (!wifi_connect()) {
        ESP_LOGE(TAG, "WiFi connection failed — aborting OTA");
        snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "OTA: WiFi connect failed");
        s_ui_state = OTA_UI_FAILED;
        vTaskDelay(pdMS_TO_TICKS(3000));
        s_ui_state = OTA_UI_CLEANUP;
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    // Start mDNS for .local hostname resolution
    mdns_init();
    mdns_hostname_set(wifi_config_get_hostname());
    mdns_instance_name_set("TrailCurrent Milepost OTA");
    ESP_LOGI(TAG, "mDNS hostname: %s.local", wifi_config_get_hostname());

    httpd_handle_t server = start_ota_server();
    if (!server) {
        snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "OTA: HTTP server failed");
        s_ui_state = OTA_UI_FAILED;
        mdns_free();
        wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(3000));
        s_ui_state = OTA_UI_CLEANUP;
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    // Show ready message with IP and hostname
    snprintf((char *)s_ui_msg, sizeof(s_ui_msg),
             "OTA Ready\n%s.local:80\nWaiting for upload...",
             wifi_config_get_hostname());
    s_ui_state = OTA_UI_READY;

    // Wait for upload or timeout
    s_ota_complete = false;
    s_ui_progress = 0;
    int64_t start = esp_timer_get_time();

    while (!s_ota_complete) {
        vTaskDelay(pdMS_TO_TICKS(100));

        // Update progress message during upload
        if (s_ui_state == OTA_UI_UPLOADING) {
            snprintf((char *)s_ui_msg, sizeof(s_ui_msg),
                     "OTA: Uploading... %d%%", s_ui_progress);
        }

        int64_t elapsed_ms = (esp_timer_get_time() - start) / 1000;
        if (elapsed_ms >= OTA_TIMEOUT_MS) {
            ESP_LOGW(TAG, "OTA timeout — no upload received");
            break;
        }
    }

    // Cleanup
    if (server) {
        httpd_stop(server);
    }
    mdns_free();

    if (s_ota_complete) {
        snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "OTA complete!\nRebooting...");
        s_ui_state = OTA_UI_COMPLETE;
        ESP_LOGI(TAG, "Restarting with new firmware...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

    // Timeout path
    wifi_disconnect();
    snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "OTA: Timeout");
    s_ui_state = OTA_UI_FAILED;
    vTaskDelay(pdMS_TO_TICKS(3000));
    s_ui_state = OTA_UI_CLEANUP;

    ESP_LOGI(TAG, "=== OTA mode exited, resuming normal operation ===");
    s_ota_running = false;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ota_init(void)
{
    ESP_LOGI(TAG, "OTA ready — will respond to CAN 0x00 trigger");
}

bool ota_is_running(void)
{
    return s_ota_running;
}

void ota_handle_trigger(const uint8_t *data, uint8_t len)
{
    if (len < 3) return;

    // Compare against this device's MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    if (data[0] != mac[3] || data[1] != mac[4] || data[2] != mac[5]) {
        ESP_LOGD(TAG, "OTA trigger ignored (MAC mismatch)");
        return;
    }

    if (s_ota_running) {
        ESP_LOGW(TAG, "OTA already in progress — ignoring trigger");
        return;
    }
    if (discovery_is_running()) {
        ESP_LOGW(TAG, "Discovery in progress — cannot start OTA");
        return;
    }
    if (!wifi_config_has_credentials()) {
        ESP_LOGE(TAG, "OTA triggered but no WiFi credentials available");
        // Show error briefly on display
        snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "OTA: No WiFi credentials");
        s_ui_state = OTA_UI_FAILED;
        // Can't spawn a task just for this — main loop will show it and clean up
        return;
    }

    s_ota_running = true;
    xTaskCreate(ota_task_fn, "ota", 8192, NULL, 3, NULL);
}

void ota_update_ui(void)
{
    ota_ui_state_t st = s_ui_state;

    if (st == OTA_UI_NONE) return;

    // Handle the brief "no credentials" error without a task
    if (st == OTA_UI_FAILED && !s_ota_running && !s_overlay) {
        // Create overlay, show message, then auto-cleanup after main loop cycles
        static int64_t fail_start = 0;
        if (fail_start == 0) {
            fail_start = esp_timer_get_time();
        }
        // Show for 3 seconds then clean up
        if (esp_timer_get_time() - fail_start > 3000000LL) {
            s_ui_state = OTA_UI_NONE;
            fail_start = 0;
            if (s_overlay) {
                lv_obj_del(s_overlay);
                s_overlay = NULL;
                s_label = NULL;
            }
            return;
        }
        // Fall through to create/update overlay below
    }

    if (st == OTA_UI_CLEANUP) {
        if (s_overlay) {
            lv_obj_del(s_overlay);
            s_overlay = NULL;
            s_label = NULL;
        }
        s_ui_state = OTA_UI_NONE;
        return;
    }

    // Create overlay if needed
    if (!s_overlay) {
        s_overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_overlay);
        lv_obj_set_size(s_overlay, 1024, 600);
        lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_overlay, LV_OPA_80, 0);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

        s_label = lv_label_create(s_overlay);
        lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(s_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_label);
    }

    if (s_label) {
        lv_label_set_text(s_label, (const char *)s_ui_msg);
    }
}
