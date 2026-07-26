#include "ui.h"
#include "rs485.h"

static const char *TAG = "RS485";  // Tag used for ESP log output

TaskHandle_t rs485_TaskHandle;

// Allocate a buffer to store incoming UART data
static char data[BUF_SIZE] = {0};  // Buffer for receiving data

/**
 * @brief Callback function to update the text area with the received data.
 *
 * This function is called periodically by a timer to update the text area
 * with the latest data stored in the global buffer. If the RS485_Clear flag is set,
 * it clears the buffer before updating the text area.
 *
 * @param timer Pointer to the lv_timer_t structure.
 * @return None
 */
void rs485_update_textarea_cb(lv_timer_t *timer) {
    // If the RS485_Clear flag is set, clear the data buffer
    if (RS485_Clear)
    {
        memset(data, 0, sizeof(data));  // Clear the data buffer
        RS485_Clear = false;  // Reset the flag
    }
    
    // Update the text area with the received data
    lv_textarea_set_text(ui_RS485_Read_Area, data);
}

/**
 * @brief RS485 communication task.
 *
 * This function is responsible for initializing UART communication for RS485,
 * reading incoming data, and updating the UI with the received data. The data is
 * appended to a global buffer and displayed in a text area on the screen.
 * 
 * It also periodically checks if the RS485_Clear flag is set, in which case it
 * clears the data buffer and updates the UI.
 *
 * @param arg Pointer to the argument passed to the task (unused).
 * @return None
 */
void rs485_task(void *arg)
{
    // Initialize UART communication for RS485 with specified TX, RX pins and baud rate
    DEV_UART_Init(ECHO_TEST_TXD, ECHO_TEST_RXD, RS485_BaudRate);

    while (1)
    {
        // Get the number of bytes available in the UART receive buffer
        int len = UART_Get_Date_Len();
        if (len > 0)
        {
            // Clear the current read buffer to store new data
            char temp_buf[len + 1];  // Temporary buffer for storing the received data
            memset(temp_buf, 0, sizeof(temp_buf));
            
            // Read the data from UART into the buffer
            UART_Read_Byte((uint8_t *)temp_buf, len);
            temp_buf[len] = '\0';  // Null-terminate the received data

            // Append the new data to the global buffer (if you need to save all data)
            strncat(data, temp_buf, sizeof(data) - strlen(data) - 1);   
            
            // Log the received data
            ESP_LOGI(TAG, "UART_Read_Byte:%s", data);

            // Create a timer to update the text area every 100ms
            lv_timer_t *t = lv_timer_create(rs485_update_textarea_cb, 100, NULL); 
            lv_timer_set_repeat_count(t, 1);
        }  
        
        // If the RS485_Clear flag is set, clear the data buffer and update the UI
        if (RS485_Clear)
        {
            lv_timer_t *t = lv_timer_create(rs485_update_textarea_cb, 100, NULL); // Update the UI every 100ms
            lv_timer_set_repeat_count(t, 1);
        }
        
        // Delay for 10ms before checking again
        vTaskDelay(10);
    }
}
