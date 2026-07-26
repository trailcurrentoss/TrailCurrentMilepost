/*****************************************************************************
 * | File         :   main.c
 * | Author       :   Waveshare team
 * | Function     :   Main function
 * | Info         :
 * |                 This example implements USB to serial communication
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-26
 * | Info         :   Basic version
 *
 ******************************************************************************/

#include "freertos/FreeRTOS.h"  // FreeRTOS main include file
#include "freertos/task.h"      // FreeRTOS task management functions
#include "usart.h"              // Include the USART driver for UART communication

/* UART Pin Definitions */
#define ECHO_TEST_TXD (GPIO_NUM_43)   // UART TX (Transmit) pin
#define ECHO_TEST_RXD (GPIO_NUM_44)   // UART RX (Receive) pin
#define ECHO_TEST_BAUDRATE (115200)   // UART BAUDRATE

// Main application entry point
void app_main()
{ 
    // Initialize UART communication with a baud rate of 115200
    DEV_UART_Init(ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_BAUDRATE);
    
    // Configure a temporary buffer for the incoming data 
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
    
    // Infinite loop to constantly check for and process incoming data
    while(1)
    {
        // Get the length of available data in the UART receive buffer
        int len = UART_Get_Date_Len();
        
        // If there is data available to read
        if (len > 0)
        {
            // Clear the buffer before reading new data
            memset(data, 0, BUF_SIZE);
            
            // Read the data from UART into the buffer
            UART_Read_Byte(data, len);
            
            // Write the received data back to the UART
            UART_Write_Byte(data);
        }  
        vTaskDelay(10);
    }
}
