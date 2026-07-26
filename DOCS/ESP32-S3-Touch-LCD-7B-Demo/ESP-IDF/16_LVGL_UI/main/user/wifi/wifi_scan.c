#include "ui.h"
#include "wifi.h"

static const char *TAG = "wifi_scan";

wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];

void print_auth_mode(int authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_OPEN");
        lv_label_set_text(ui_WIFI_Aurhmode,"Authmode \tWIFI_AUTH_OPEN");
        break;
    case WIFI_AUTH_OWE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_OWE");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_OWE");
        break;
    case WIFI_AUTH_WEP:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WEP");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_WEP");
        break;
    case WIFI_AUTH_WPA_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA_PSK");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_WPA_PSK");
        break;
    case WIFI_AUTH_WPA2_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_PSK");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_WPA2_PSK");
        break;
    case WIFI_AUTH_WPA_WPA2_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA_WPA2_PSK");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_WPA_WPA2_PSK");
        break;
    case WIFI_AUTH_ENTERPRISE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_ENTERPRISE");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_ENTERPRISE");
        break;
    case WIFI_AUTH_WPA3_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_PSK");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_WPA3_PSK");
        break;
    case WIFI_AUTH_WPA2_WPA3_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_WPA3_PSK");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_WPA2_WPA3_PSK");
        break;
    case WIFI_AUTH_WPA3_ENTERPRISE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_ENTERPRISE");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_WPA3_ENTERPRISE");
        break;
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_WPA3_ENTERPRISE");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_WPA2_WPA3_ENTERPRISE");
        break;
    case WIFI_AUTH_WPA3_ENT_192:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_ENT_192");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_WPA3_ENT_192");
        break;
    default:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_UNKNOWN");
        lv_label_set_text(ui_WIFI_Aurhmode, "Authmode \tWIFI_AUTH_UNKNOWN");
        break;
    }
}

void print_cipher_type(int pairwise_cipher, int group_cipher)
{
    switch (pairwise_cipher) {
    case WIFI_CIPHER_TYPE_NONE:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_NONE");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_NONE");
        break;
    case WIFI_CIPHER_TYPE_WEP40:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP40");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_WEP40");
        break;
    case WIFI_CIPHER_TYPE_WEP104:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP104");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_WEP104");
        break;
    case WIFI_CIPHER_TYPE_TKIP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_TKIP");
        break;
    case WIFI_CIPHER_TYPE_CCMP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_CCMP");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_CCMP");
        break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        break;
    case WIFI_CIPHER_TYPE_AES_CMAC128:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_AES_CMAC128");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_AES_CMAC128");
        break;
    case WIFI_CIPHER_TYPE_SMS4:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_SMS4");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_SMS4");
        break;
    case WIFI_CIPHER_TYPE_GCMP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_GCMP");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_GCMP");
        break;
    case WIFI_CIPHER_TYPE_GCMP256:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_GCMP256");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_GCMP256");
        break;
    default:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
        lv_label_set_text(ui_WIFI_Pairwise, "Authmode \tWIFI_CIPHER_TYPE_UNKNOWN");
        break;
    }

    switch (group_cipher) {
    case WIFI_CIPHER_TYPE_NONE:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_NONE");
        lv_label_set_text(ui_WIFI_Group, "Authmode \tWIFI_CIPHER_TYPE_NONE");
        break;
    case WIFI_CIPHER_TYPE_WEP40:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP40");
        lv_label_set_text(ui_WIFI_Group, "Authmode \tWIFI_CIPHER_TYPE_WEP40");
        break;
    case WIFI_CIPHER_TYPE_WEP104:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP104");
        lv_label_set_text(ui_WIFI_Group, "Authmode \tWIFI_CIPHER_TYPE_WEP104");
        break;
    case WIFI_CIPHER_TYPE_TKIP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP");
        lv_label_set_text(ui_WIFI_Group, "Authmode \tWIFI_CIPHER_TYPE_TKIP");
        break;
    case WIFI_CIPHER_TYPE_CCMP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_CCMP");
        lv_label_set_text(ui_WIFI_Group, "Authmode \tWIFI_CIPHER_TYPE_CCMP");
        break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        lv_label_set_text(ui_WIFI_Group, "Authmode \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        break;
    case WIFI_CIPHER_TYPE_SMS4:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_SMS4");
        lv_label_set_text(ui_WIFI_Group, "Authmode \tWIFI_CIPHER_TYPE_SMS4");
        break;
    case WIFI_CIPHER_TYPE_GCMP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_GCMP");
        lv_label_set_text(ui_WIFI_Group, "Authmode \tWIFI_CIPHER_TYPE_GCMP");
        break;
    case WIFI_CIPHER_TYPE_GCMP256:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_GCMP256");
        lv_label_set_text(ui_WIFI_Group, "Authmode \tWIFI_CIPHER_TYPE_GCMP256");
        break;
    default:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
        lv_label_set_text(ui_WIFI_Group, "Authmode \tWIFI_CIPHER_TYPE_UNKNOWN");
        break;
    }
}

static void wifi_update_list_cb(lv_timer_t * timer) {
    // Show the loading spinner while updating the Wi-Fi list
    _ui_flag_modify(ui_WIFI_Spinner, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
    // Disable the Wi-Fi open button and AP open button while scanning
    _ui_state_modify(ui_WIFI_OPEN, LV_STATE_DISABLED, _UI_MODIFY_STATE_REMOVE);
    _ui_state_modify(ui_WIFI_AP_OPEN, LV_STATE_DISABLED, _UI_MODIFY_STATE_REMOVE);
    
    // Iterate through the scanned AP list
    for (int i = 0; i < DEFAULT_SCAN_LIST_SIZE; i++)
    {
        // If no more valid APs in the list, stop
        if (ap_info[i].rssi == 0 && ap_info[i].ssid[0] == '\0')
        {
            break;
        }
        else if(ap_info[i].rssi > -25)  // Strong signal (RSSI > -25)
        {
            // Add button with strong signal icon
            WIFI_List_Button = lv_list_add_btn(ui_WIFI_SCAN_List, &ui_img_wifi_4_png, (const char *)ap_info[i].ssid);
        }
        else if ((ap_info[i].rssi < -25) && (ap_info[i].rssi > -50))  // Medium signal
        {
            // Add button with medium signal icon
            WIFI_List_Button = lv_list_add_btn(ui_WIFI_SCAN_List, &ui_img_wifi_3_png, (const char *)ap_info[i].ssid);
        }
        else if ((ap_info[i].rssi < -50) && (ap_info[i].rssi > -75))  // Weak signal
        {
            // Add button with weak signal icon
            WIFI_List_Button = lv_list_add_btn(ui_WIFI_SCAN_List, &ui_img_wifi_2_png, (const char *)ap_info[i].ssid);
        }
        else  // Very weak signal (RSSI < -75)
        {
            // Add button with very weak signal icon
            WIFI_List_Button = lv_list_add_btn(ui_WIFI_SCAN_List, &ui_img_wifi_1_png, (const char *)ap_info[i].ssid);
        }
        
        // Customize button appearance
        lv_obj_set_style_bg_opa(WIFI_List_Button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);  // Set background opacity to 0 (transparent)
        lv_obj_set_style_text_color(WIFI_List_Button, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);  // Set text color to white
        lv_obj_set_style_text_opa(WIFI_List_Button, 255, LV_PART_MAIN | LV_STATE_DEFAULT);  // Set text opacity to full (255)

        // Add event callback for each button
        lv_obj_add_event_cb(WIFI_List_Button, ui_WIFI_list_event_cb, LV_EVENT_ALL, (void *)i);  // Pass index as user data
    }
}

/* Initialize Wi-Fi as STA (Station mode) and set the scan method */
void wifi_scan(void)
{
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;  // Maximum number of APs to scan for
    uint16_t ap_count = 0;  // Variable to hold the number of found APs

    // Clear the ap_info array to hold fresh scan results
    memset(ap_info, 0, sizeof(ap_info));

    // Start the Wi-Fi scan
    esp_wifi_scan_start(NULL, true);

    ESP_LOGI(TAG, "Max AP number ap_info can hold = %u", number);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));  // Get the number of scanned APs
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));  // Get the scan results (AP records)
    ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, number);

    // Create a timer to update the UI with the scanned Wi-Fi list
    lv_timer_t *t = lv_timer_create(wifi_update_list_cb, 100, NULL); // Update the UI every 100ms
    lv_timer_set_repeat_count(t, 1);

    // Delay to allow UI update time
    vTaskDelay(pdMS_TO_TICKS(100));
}
