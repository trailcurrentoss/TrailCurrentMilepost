/*****************************************************************************
 * | File         :   main.c
 * | Author       :   Waveshare team
 * | Function     :   Main function
 * | Info         :
 * |                 Basic implementation to toggle an LED.
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-26
 * | Info         :   Basic version
 *
 ******************************************************************************/

#include "freertos/FreeRTOS.h"  // FreeRTOS main include file
#include "freertos/task.h"      // FreeRTOS task management functions
#include "gpio.h"               // Custom GPIO management functions

// Main application entry point
void app_main()
{
    // Configure the GPIO pin connected to the LED as both input and output
    DEV_GPIO_Mode(LED_GPIO_PIN, GPIO_MODE_INPUT_OUTPUT);
    
    // Infinite loop to toggle the LED state
    while(1)
    {
        // Read the current state of the LED GPIO pin and toggle it
        DEV_Digital_Write(LED_GPIO_PIN, !DEV_Digital_Read(LED_GPIO_PIN));
        
        // Delay for 500 milliseconds before the next toggle
        vTaskDelay(500);
    }
}
