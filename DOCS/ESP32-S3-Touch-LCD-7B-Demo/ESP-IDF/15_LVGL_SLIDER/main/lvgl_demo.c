/*****************************************************************************
 * | File       :   main.c
 * | Author     :   Waveshare team
 * | Function   :   Main function
 * | Info       :   Uses LVGL to create a slider to control LED brightness.
 *                  The connected LED is active-low (ON when the signal is low, OFF when high),
 *                  so the duty cycle is inverted during PWM configuration.
 *----------------
 * | Version    :   V1.0
 * | Date       :   2024-12-07
 * | Info       :   Basic version
 *
 ******************************************************************************/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_demo.h"
#include "gpio.h"               // Header for custom GPIO management functions
#include "lvgl.h"               // Header for the LVGL graphics library
#include "io_extension.h"       // Include IO_EXTENSION driver header for GPIO functions
 
static lv_obj_t * label;        // Pointer for the label displaying the slider value
static lv_obj_t *BAT_Label;      // Label to display battery voltage
char bat_v[20];                  // Buffer to store formatted battery voltage string

/**
 * @brief Callback function to update the battery voltage label.
 * 
 * Called by an LVGL timer to refresh the battery voltage shown on the UI.
 * 
 * @param timer Pointer to the LVGL timer object
 */
static void bat_cb(lv_timer_t * timer) 
{
    lv_label_set_text(BAT_Label, bat_v); // Update the battery voltage label with latest value
}

/**
 * @brief  Event callback function for slider interaction
 * @param  e: Pointer to the event descriptor
 */
static void slider_event_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e); // Get the slider object that triggered the event

    // Update the label text with the slider's current value
    lv_label_set_text_fmt(label, "%" LV_PRId32, lv_slider_get_value(slider));

    // Align the label above the slider
    lv_obj_align_to(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);

    // Set the PWM duty cycle to control the LED brightness (inverted duty cycle)
    DEV_SET_PWM(100 - lv_slider_get_value(slider));
    IO_EXTENSION_Pwm_Output(100 - lv_slider_get_value(slider));
}

/**
 * @brief  Create a slider to control the brightness of an LED.
 */
void lvgl_slider(void)
{
    // Initialize the GPIO pin for PWM with a 1 kHz frequency
    DEV_GPIO_PWM(LED_GPIO_PIN, 1000);

    // Set the initial PWM duty cycle to 0% (LED ON due to active-low configuration)
    DEV_SET_PWM(0);
    IO_EXTENSION_Pwm_Output(0);
    // Create a slider on the current active screen
    lv_obj_t * slider = lv_slider_create(lv_scr_act());

    // Set the slider width
    lv_obj_set_width(slider, 200);

    // Align the slider to the center of the screen
    lv_obj_center(slider);

    // Set initial slider value to 100 (LED off if active-low)
    lv_slider_set_value(slider, 100, LV_ANIM_OFF);
    
    // Attach an event callback function for slider value changes
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Create a label to display the slider's value
    label = lv_label_create(lv_scr_act());

    // Set the initial text of the label to "0"
    lv_label_set_text(label, "100");

    // Align the label above the slider
    lv_obj_align_to(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);

    // Create a label for displaying battery voltage
    BAT_Label = lv_label_create(lv_scr_act());
    lv_obj_set_width(BAT_Label, LV_SIZE_CONTENT);
    lv_obj_set_height(BAT_Label, LV_SIZE_CONTENT);
    lv_obj_center(BAT_Label);
    lv_obj_set_y(BAT_Label, 30); 
    lv_label_set_text(BAT_Label, "BAT:3.7V"); // Initial placeholder text

    // Style the battery label
    lv_obj_set_style_text_color(BAT_Label, lv_color_hex(0xFFA500), LV_PART_MAIN | LV_STATE_DEFAULT); // Orange text
    lv_obj_set_style_text_opa(BAT_Label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);                     // Full opacity
    lv_obj_set_style_text_font(BAT_Label, &lv_font_montserrat_44, LV_PART_MAIN | LV_STATE_DEFAULT); // Large font

}

void loop_bat()
{
    float value = 0; // Battery voltage accumulator

    // Take 10 ADC readings and average them to reduce noise
    for (int i = 0; i < 10; i++) {
        value += IO_EXTENSION_Adc_Input(); // Custom function to read ADC input
        vTaskDelay(20); // Delay before the next measurement cycle // Small delay between samples
    }
    value /= 10.0; // Compute average

    // Convert ADC value to voltage (assuming 10-bit ADC, 3.3V reference, and 3:1 voltage divider)
    value *= 3 * 3.3 / 1023.0;

    // Clamp voltage to max 4.2V for safe display
    if (value > 4.2) {
        value = 4.2;
    }

    // Format battery voltage string for display
    sprintf(bat_v, "BAT:%0.2fV", value);

    // Create a one-shot LVGL timer to update the label
    lv_timer_t *t = lv_timer_create(bat_cb, 100, NULL); // Trigger after 100 ms
    lv_timer_set_repeat_count(t, 1); // Run only once

    vTaskDelay(100); // Delay before the next measurement cycle // Wait before the next cycle
}