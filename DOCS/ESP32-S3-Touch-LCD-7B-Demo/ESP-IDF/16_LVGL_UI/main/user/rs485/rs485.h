#ifndef _RS485_
#define _RS485_

#include "usart.h"              // Include the USART driver for UART communication

// Declare the task handle for the RS485 task
extern TaskHandle_t rs485_TaskHandle;

/* RS485 Pin Definitions */
/**
 * @brief Defines the GPIO pins used for RS485 communication.
 * 
 * - ECHO_TEST_TXD: GPIO pin for RS485 Transmit (TX) signal.
 * - ECHO_TEST_RXD: GPIO pin for RS485 Receive (RX) signal.
 * - ECHO_TEST_BAUDRATE: Baud rate for RS485 communication, set to 115200 bps.
 */
#define ECHO_TEST_TXD (GPIO_NUM_16)   // RS485 Transmit (TX) pin
#define ECHO_TEST_RXD (GPIO_NUM_15)   // RS485 Receive (RX) pin
#define ECHO_TEST_BAUDRATE (115200)   // RS485 Baud rate (115200 bps)

/**
 * @brief Function to handle RS485 communication tasks.
 *
 * This function initializes UART communication, continuously checks for incoming data,
 * processes the received data, and updates the user interface as necessary.
 * It is intended to run as a FreeRTOS task.
 *
 * @param arg Pointer to any argument passed to the task (not used here).
 * @return None
 */
void rs485_task(void *arg);

#endif
