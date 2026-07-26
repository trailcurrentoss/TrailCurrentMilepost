/*****************************************************************************
 * | File       :   main.c
 * | Author     :   Waveshare team
 * | Function   :   Main function
 * | Info       :   
 * |                Create a button using LVGL to control an external LED
 *----------------
 * | Version    :   V1.0
 * | Date       :   2024-12-06
 * | Info       :   Basic version
 *
 ******************************************************************************/


#include "rgb_lcd_port.h"    // Header for Waveshare RGB LCD driver
#include "gt911.h"           // Header for touch screen operations (GT911)
#include "lvgl_port.h"
#include "lvgl_demo.h"

static const char *TAG = "main";
// Main application function
void app_main()
{
    static esp_lcd_panel_handle_t panel_handle = NULL; // Declare a handle for the LCD panel
    static esp_lcd_touch_handle_t tp_handle = NULL;
    // Initialize the GT911 touch screen controller
    tp_handle = touch_gt911_init();  
    
    // Initialize the Waveshare ESP32-S3 RGB LCD hardware
    panel_handle = waveshare_esp32_s3_rgb_lcd_init(); 

    // Turn on the LCD backlight
    wavesahre_rgb_lcd_bl_on();   

    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle)); // Initialize LVGL with the panel and touch handles

    ESP_LOGI(TAG, "Display LVGL demos");

    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {
        
        lvgl_btn();

        // Release the mutex
        lvgl_port_unlock();
    }
}
