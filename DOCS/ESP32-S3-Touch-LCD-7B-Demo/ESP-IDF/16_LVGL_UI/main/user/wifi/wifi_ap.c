#include "ui.h"
#include "wifi.h"
#include "rgb_lcd_port.h"

static esp_netif_t *ap_netif;

// Function to initialize the Wi-Fi Access Point (AP) mode
void wifi_ap_init(uint8_t *ssid, uint8_t *pwd, uint8_t channel)
{
    wifi_config_t wifi_ap_config = {
        .ap = {
            .channel = channel,
            .max_connection = 5, // Maximum number of allowed connections
            .authmode = WIFI_AUTH_WPA2_PSK, // Set WPA2 encryption for AP
            .pmf_cfg = {
                .required = false, // Disable Protected Management Frames (PMF)
            },
        },
    };

    // Copy SSID and password to the Wi-Fi configuration
    strcpy((char *)wifi_ap_config.ap.ssid, (const char *)ssid);
    wifi_ap_config.ap.ssid_len = strlen((const char *)ssid); // Set SSID length
    strcpy((char *)wifi_ap_config.ap.password, (const char *)pwd);

    // If password is empty, set the AP to open authentication (no password)
    if (strlen((const char *)pwd) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Set the Wi-Fi AP configuration
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             ssid, pwd, channel);
    // Uncomment the next line to enable NAT (Network Address Translation) for the AP
    // wifi_napt_enable();
}

// Function to open the Wi-Fi AP
void wifi_open_ap()
{
    wifi_mode_t mode;
    ESP_ERROR_CHECK(esp_wifi_get_mode(&mode)); // Get the current Wi-Fi mode

    // Stop Wi-Fi to configure the mode
    ESP_ERROR_CHECK(esp_wifi_stop());

    // Create the AP network interface if it doesn't exist
    if (ap_netif == NULL)
    {
        ap_netif = esp_netif_create_default_wifi_ap();
        assert(ap_netif); // Ensure the interface was created successfully
    }

    // Save current STA configuration
    wifi_config_t sta_config;
    
    // If the current mode is STA (Station) mode
    if (mode == WIFI_MODE_STA)
    {
        // Switch to AP + STA mode
        printf("AP:STA + AP \r\n");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_start());

        if (connection_flag)
        {
            // Save the current STA configuration
            ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &sta_config));
            ESP_LOGI(TAG_STA, "GET: SSID:%s, password:%s",
                    sta_config.sta.ssid, sta_config.sta.password);
            printf("AP esp_wifi_connect \r\n");
            
            // Set the STA configuration back and reconnect
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
            ESP_ERROR_CHECK(esp_wifi_start());
            
            // Adjust the pixel clock for the display
            waveahre_rgb_lcd_set_pclk(12 * 1000 * 1000);
            vTaskDelay(20);
            ESP_ERROR_CHECK(esp_wifi_connect()); // Reconnect to the Wi-Fi network
            connection_last_flag = true;
            wifi_wait_connect();
            waveahre_rgb_lcd_set_pclk(EXAMPLE_LCD_PIXEL_CLOCK_HZ);
        } 
    }
    else
    {
        // Only AP mode
        printf("AP:AP \r\n");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
}

// Function to close the Wi-Fi AP
void wifi_close_ap()
{
    // Stop AP broadcasting
    wifi_mode_t mode;
    ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));

    // If the current mode is AP + STA mode, switch to STA mode
    if (mode != WIFI_MODE_APSTA)
    {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL)); // Disable all Wi-Fi modes
        ESP_ERROR_CHECK(esp_wifi_stop());
    }
    else
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Destroy the AP network interface
    if (ap_netif != NULL)
    {
        esp_netif_destroy(ap_netif); // Clean up the AP network interface
        ap_netif = NULL; // Prevent double destruction
    }

    ESP_LOGI("WIFI", "WiFi AP Mode Disabled");

    // Hide the AP information from the UI
    _ui_flag_modify(ui_WIFI_AP_MAC_List, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
    lv_label_set_text(ui_WIFI_AP_CON_NUM, "AP is not started.");
}

// Function to enable Network Address Translation (NAPT) for the AP netif
void wifi_napt_enable()
{
    /* Enable napt on the AP netif */
    if (esp_netif_napt_enable(ap_netif) != ESP_OK)
    {
        ESP_LOGE(TAG_STA, "NAPT not enabled on the netif: %p", ap_netif);
    }
}
