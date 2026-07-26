/*****************************************************************************
 * | File         :   gpio.h
 * | Author       :   Waveshare team
 * | Function     :   Hardware underlying interface
 * | Info         :
 * |                 GPIO driver code for hardware-level operations.
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-19
 * | Info         :   Basic version
 *
 ******************************************************************************/

#ifndef __GPIO_H
#define __GPIO_H

#include "driver/gpio.h"  // ESP-IDF GPIO driver library
#include "driver/ledc.h"

/* Pin Definitions */
#define LED_GPIO_PIN    GPIO_NUM_6  /* GPIO pin connected to the LED */

/* Function Prototypes */

/**
 * @brief Configure a GPIO pin as input or output
 *
 * This function initializes a GPIO pin with the specified mode (input or output).
 * If set as input, it also enables the pull-up resistor by default.
 *
 * @param Pin GPIO pin number
 * @param Mode GPIO mode: 0 or GPIO_MODE_INPUT for input, others for output
 */
void DEV_GPIO_Mode(uint16_t Pin, uint16_t Mode);

/**
 * @brief Configure a GPIO pin for interrupt handling
 *
 * This function sets up a GPIO pin to generate an interrupt on a negative edge
 * (falling edge) and registers the specified interrupt handler.
 *
 * @param Pin GPIO pin number
 * @param isr_handler Pointer to the interrupt handler function
 */
void DEV_GPIO_INT(int32_t Pin, gpio_isr_t isr_handler);

/**
 * @brief Configure a GPIO pin for PWM output
 *
 * This function sets up a GPIO pin to generate Pulse Width Modulation (PWM) signals at the specified frequency.
 * The pin is configured as a PWM output pin.
 *
 * @param Pin GPIO pin number for PWM output
 * @param frequency PWM frequency in Hertz
 */
void DEV_GPIO_PWM(uint16_t Pin, uint16_t frequency);

/**
 * @brief Set the PWM duty cycle for the configured GPIO pin
 *
 * This function sets the duty cycle for the PWM signal on the GPIO pin previously configured for PWM output.
 * The duty cycle determines the proportion of time the PWM signal stays high within a period.
 *
 * @param Value Duty cycle percentage (0 to 100), where 0 is 0% duty and 100 is 100% duty.
 */
void DEV_SET_PWM(uint8_t Value);

/**
 * @brief Set the logic level of a GPIO pin
 *
 * This function sets the logic level (high or low) of a GPIO pin.
 *
 * @param Pin GPIO pin number
 * @param Value Logic level: 0 for low, 1 for high
 */
void DEV_Digital_Write(uint16_t Pin, uint8_t Value);

/**
 * @brief Read the logic level of a GPIO pin
 *
 * This function reads and returns the current logic level of a GPIO pin.
 *
 * @param Pin GPIO pin number
 * @return uint8_t Logic level: 0 for low, 1 for high
 */
uint8_t DEV_Digital_Read(uint16_t Pin);

#endif
