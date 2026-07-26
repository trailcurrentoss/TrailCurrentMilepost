/*****************************************************************************
 * | File       :   main.c
 * | Author     :   Waveshare team
 * | Function   :   Main function
 * | Info       :   
 * |                Demonstrates an LVGL slider to control LED brightness.
 *----------------
 * | Version    :   V1.0
 * | Date       :   2024-12-07
 * | Info       :   Basic version
 *
 ******************************************************************************/

#include "rgb_lcd_port.h"    // Header for Waveshare RGB LCD driver
#include "gt911.h"           // Header for touch screen operations (GT911)
#include "lvgl_port.h"       // Header for LVGL port initialization and locking
#include "lvgl_demo.h"       // Header for LVGL demo implementations

static const char *TAG = "main";  // Tag used for ESP log output

// Main application function
void app_main()
{
    static esp_lcd_panel_handle_t panel_handle = NULL; // LCD panel handle
    static esp_lcd_touch_handle_t tp_handle = NULL;    // Touch panel handle

    // Initialize the GT911 touch screen controller
    tp_handle = touch_gt911_init();  
    
    // Initialize the Waveshare ESP32-S3 RGB LCD hardware
    panel_handle = waveshare_esp32_s3_rgb_lcd_init(); 

    // Turn on the LCD backlight
    wavesahre_rgb_lcd_bl_on();   

    // Initialize the LVGL library with the panel and touch handles
    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle)); 

    ESP_LOGI(TAG, "Display LVGL demos");

    // Lock the LVGL port to ensure thread safety during API calls
    if (lvgl_port_lock(-1)) {
        // Call the slider demo to control LED brightness
        lvgl_slider();

        // Release the mutex after LVGL operations
        lvgl_port_unlock();
    }
    while (1)
    {
        loop_bat();
        vTaskDelay(100); // Delay before the next measurement cycle
    }
    
}
