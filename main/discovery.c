#include "include/discovery.h"
#include "include/wifi_config.h"
#include "include/ota.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "mdns.h"
#include "lvgl.h"

static const char *TAG = "discovery";

#define MODULE_TYPE "milepost"

// ---------------------------------------------------------------------------
// Discovery state
// ---------------------------------------------------------------------------

static volatile bool s_confirmed = false;
static volatile bool s_discovery_running = false;

// UI state communicated to main loop
typedef enum {
    DISC_UI_NONE,
    DISC_UI_CONNECTING,
    DISC_UI_WAITING,
    DISC_UI_CONFIRMED,
    DISC_UI_TIMEOUT,
    DISC_UI_FAILED,
    DISC_UI_CLEANUP,
} disc_ui_state_t;

static volatile disc_ui_state_t s_ui_state = DISC_UI_NONE;
static volatile char s_ui_msg[80] = {0};
static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_label = NULL;

// ---------------------------------------------------------------------------
// mDNS service advertisement with TXT records
// ---------------------------------------------------------------------------

static void discovery_mdns_start(void)
{
    const char *hostname = wifi_config_get_hostname();
    const esp_app_desc_t *app = esp_app_get_description();

    mdns_init();
    mdns_hostname_set(hostname);
    mdns_instance_name_set("TrailCurrent Module");

    mdns_txt_item_t txt[] = {
        { "type",  MODULE_TYPE },
        { "fw",    app->version },
    };

    mdns_service_add("TrailCurrent Discovery", "_trailcurrent", "_tcp",
                     80, txt, sizeof(txt) / sizeof(txt[0]));

    ESP_LOGI(TAG, "mDNS discovery: %s.local type=%s fw=%s",
             hostname, MODULE_TYPE, app->version);
}

// ---------------------------------------------------------------------------
// HTTP confirmation endpoint
// ---------------------------------------------------------------------------

static esp_err_t confirm_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Discovery confirmed by Headwaters");
    httpd_resp_sendstr(req, "confirmed\n");
    s_confirmed = true;
    return ESP_OK;
}

static httpd_handle_t discovery_start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    httpd_uri_t confirm_uri = {
        .uri     = "/discovery/confirm",
        .method  = HTTP_GET,
        .handler = confirm_handler,
    };
    httpd_register_uri_handler(server, &confirm_uri);

    return server;
}

// ---------------------------------------------------------------------------
// Discovery task
// ---------------------------------------------------------------------------

static void discovery_task_fn(void *arg)
{
    if (!wifi_config_has_credentials()) {
        ESP_LOGE(TAG, "Discovery triggered but no WiFi credentials");
        snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "Discovery: No WiFi credentials");
        s_ui_state = DISC_UI_FAILED;
        vTaskDelay(pdMS_TO_TICKS(3000));
        s_ui_state = DISC_UI_CLEANUP;
        s_discovery_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "=== Entering discovery mode ===");
    snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "Discovery: Connecting to WiFi...");
    s_ui_state = DISC_UI_CONNECTING;

    if (!wifi_connect()) {
        ESP_LOGE(TAG, "WiFi connection failed — aborting discovery");
        snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "Discovery: WiFi connect failed");
        s_ui_state = DISC_UI_FAILED;
        vTaskDelay(pdMS_TO_TICKS(3000));
        s_ui_state = DISC_UI_CLEANUP;
        s_discovery_running = false;
        vTaskDelete(NULL);
        return;
    }

    discovery_mdns_start();
    httpd_handle_t server = discovery_start_server();

    snprintf((char *)s_ui_msg, sizeof(s_ui_msg),
             "Discovery: Waiting for\nHeadwaters confirmation...");
    s_ui_state = DISC_UI_WAITING;

    // Wait for confirmation or timeout
    s_confirmed = false;
    int64_t start = esp_timer_get_time();

    while (!s_confirmed) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t elapsed_ms = (esp_timer_get_time() - start) / 1000;
        if (elapsed_ms >= DISCOVERY_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Discovery timeout — no confirmation received");
            break;
        }
    }

    // Cleanup
    if (server) {
        httpd_stop(server);
    }
    mdns_free();
    wifi_disconnect();

    if (s_confirmed) {
        ESP_LOGI(TAG, "=== Discovery complete — module registered ===");
        snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "Discovery: Registered!");
        s_ui_state = DISC_UI_CONFIRMED;
    } else {
        ESP_LOGI(TAG, "=== Discovery timed out ===");
        snprintf((char *)s_ui_msg, sizeof(s_ui_msg), "Discovery: Timeout");
        s_ui_state = DISC_UI_TIMEOUT;
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    s_ui_state = DISC_UI_CLEANUP;
    s_discovery_running = false;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void discovery_init(void)
{
    ESP_LOGI(TAG, "Discovery ready — will respond to CAN 0x02 trigger");
}

bool discovery_is_running(void)
{
    return s_discovery_running;
}

void discovery_handle_trigger(void)
{
    if (s_discovery_running) {
        ESP_LOGW(TAG, "Discovery already in progress — ignoring trigger");
        return;
    }
    if (ota_is_running()) {
        ESP_LOGW(TAG, "OTA in progress — ignoring discovery trigger");
        return;
    }
    s_discovery_running = true;
    xTaskCreate(discovery_task_fn, "discovery", 8192, NULL, 3, NULL);
}

void discovery_update_ui(void)
{
    disc_ui_state_t st = s_ui_state;

    if (st == DISC_UI_NONE) return;

    if (st == DISC_UI_CLEANUP) {
        if (s_overlay) {
            lv_obj_del(s_overlay);
            s_overlay = NULL;
            s_label = NULL;
        }
        s_ui_state = DISC_UI_NONE;
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
