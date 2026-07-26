/*****************************************************************************
 * | File       :   main.c
 * | Author     :   Waveshare team
 * | Function   :   Main function
 * | Info       :   Create a button using LVGL to control an external LED
 *----------------
 * | Version    :   V1.0
 * | Date       :   2024-12-07
 * | Info       :   Basic version
 *
 ******************************************************************************/

#include "lvgl_demo.h"
#include "gpio.h"               // Header for custom GPIO management functions
#include "lvgl.h"               // Header for the LVGL graphics library

/**
 * @brief  Event callback for button interactions
 * @param  *e: A pointer to `lv_event_t` containing event-related data
 * @return None
 */
static void btn_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e); // Retrieve the event code
    if (code == LV_EVENT_CLICKED) {
        // Toggle the state of the GPIO pin connected to the LED
        DEV_Digital_Write(LED_GPIO_PIN, !DEV_Digital_Read(LED_GPIO_PIN));
    }
}

/**
 * @brief  Creates an LVGL button that toggles an external LED
 * @return None
 */
void lvgl_btn()
{
    // Configure the LED GPIO pin as both input and output
    DEV_GPIO_Mode(LED_GPIO_PIN, GPIO_MODE_INPUT_OUTPUT);

    // Set the initial LED state to ON
    DEV_Digital_Write(LED_GPIO_PIN, 1);

    // Create a button on the active screen
    lv_obj_t * btn = lv_btn_create(lv_scr_act());     
    lv_obj_set_pos(btn, 10, 10);                      // Set the button's position
    lv_obj_set_size(btn, 120, 50);                    // Set the button's size
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);         // Align the button to the center of the screen

    // Add the event callback to handle button actions
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);

    // Create a label and add it to the button
    lv_obj_t * label = lv_label_create(btn);          
    lv_label_set_text(label, "Button");               // Set the label's text
    lv_obj_center(label);                             // Center the label within the button
}
