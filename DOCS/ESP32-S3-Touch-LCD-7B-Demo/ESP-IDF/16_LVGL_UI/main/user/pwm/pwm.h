#ifndef _PWM_
#define _PWM_

#include "io_extension.h"              // Include the USART driver for UART communication

/**
 * @brief Initialize the PWM (Pulse Width Modulation) module.
 *
 * This function initializes the PWM module, which can be used to control the brightness of the LCD backlight or other PWM-driven devices.
 *
 * @note The PWM initialization may involve setting up GPIO pins, configuring the PWM frequency, and setting the initial duty cycle.
 * @note This function assumes that the hardware platform supports PWM functionality and that the necessary GPIO pins are available.
 */
void pwm_init();

#endif