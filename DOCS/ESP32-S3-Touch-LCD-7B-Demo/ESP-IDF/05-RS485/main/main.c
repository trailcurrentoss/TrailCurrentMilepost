/*****************************************************************************
 * | File         :   main.c
 * | Author       :   Waveshare team
 * | Function     :   Main function for RS485 communication
 * | Info         :
 * |                 Basic implementation for handling RS485 bus communication.
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-28
 * | Info         :   Basic version for initializing RS485 communication and
 * |                 handling RS485 alerts.
 *
 ******************************************************************************/

#include "freertos/FreeRTOS.h"  // FreeRTOS main include file
#include "freertos/task.h"      // FreeRTOS task management functions
#include "usart.h"              // Include the USART driver for UART communication

/* RS485 Pin Definitions */
#define ECHO_TEST_TXD (GPIO_NUM_16)   // RS485 Transmit (TX) pin
#define ECHO_TEST_RXD (GPIO_NUM_15)   // RS485 Receive (RX) pin
#define ECHO_TEST_BAUDRATE (921600)   // RS485 Baud rate (115200 bps)

/**
 * @brief Main application entry point.
 * 
 * This function initializes RS485 communication using UART, configures the 
 * necessary UART pins (TX, RX), and enters an infinite loop where it reads 
 * incoming data from the RS485 bus and sends it back to the bus.
 */
void app_main()
{
    // Initialize UART communication for RS485 with specified TX, RX pins and baud rate
    DEV_UART_Init(ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_BAUDRATE);
    
    // Allocate a buffer to store incoming UART data
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);  // Buffer for receiving data
    
    // Infinite loop to constantly check for and process incoming data
    while (1)
    {
        // Get the number of bytes available in the UART receive buffer
        int len = UART_Get_Date_Len();
        
        // If there is data available to read from the UART buffer
        if (len > 0)
        {
            // Clear the buffer to avoid leftover data from previous reads
            memset(data, 0, BUF_SIZE);
            
            // Read the data from UART into the buffer
            UART_Read_Byte(data, len);
            
            // Write the received data back to the UART (echoing)
            UART_Write_Byte(data);
        }  
        vTaskDelay(10);
    }
}
