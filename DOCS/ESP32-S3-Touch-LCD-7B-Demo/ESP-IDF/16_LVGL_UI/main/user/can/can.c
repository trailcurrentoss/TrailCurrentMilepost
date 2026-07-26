#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "ui.h"
#include "can.h"  // Include the CAN driver for communication

TaskHandle_t can_TaskHandle;

static char can_data[2 * 1024] = {0};  // Buffer for receiving data

/**
 * @brief Callback function to update the textarea with CAN data.
 *
 * This function is triggered periodically by a timer to update the
 * text area with the latest received CAN data. It checks the CAN 
 * clear flag and clears the buffer if needed.
 *
 * @param timer The timer that triggers the callback.
 */
void can_update_textarea_cb(lv_timer_t * timer) {
    if (CAN_Clear) {
        memset(can_data, 0, sizeof(can_data));  // Clear the buffer when flag is set
        CAN_Clear = false;
    }
    lv_textarea_set_text(ui_CAN_Read_Area, can_data);  // Update the UI with the new CAN data
}

/**
 * @brief Task for handling CAN communication.
 *
 * This task is responsible for initializing the CAN interface, reading CAN 
 * messages, and updating the UI with the received data. It runs in an 
 * infinite loop, constantly checking for new CAN messages and formatting 
 * them for display.
 *
 * @param arg Task argument, not used in this function.
 */
void can_task(void *arg)
{
    // TWAI configuration settings for the CAN bus
    static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();  // Set CAN bus speed to 500 kbps
    static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();  // Accept all incoming CAN messages
    static const twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);  // General configuration, set TX/RX GPIOs and mode
    lv_roller_set_selected(ui_CAN_Roller, 5, LV_ANIM_OFF);
    // Initialize the CAN communication interface
    IO_EXTENSION_Output(IO_EXTENSION_IO_5, 1);  // Select CAN communication interface (0 for USB, 1 for CAN)
    
    // Initialize the CAN communication system
    can_init(t_config, f_config, g_config);  // Initialize CAN with specified configurations

    uint32_t alerts_triggered;  // Variable to store triggered CAN bus alerts
    static twai_message_t message;  // Variable to store received CAN message
    char message_str[256];  // Buffer to store formatted message string

    while (1)
    {
        alerts_triggered = can_read_alerts();  // Check for any triggered CAN bus alerts

        // If new CAN data is received, process and display it
        if (alerts_triggered & TWAI_ALERT_RX_DATA) {
            message = can_read_Byte();  // Read the received CAN message

            // Format the CAN message into a string
            snprintf(message_str, sizeof(message_str),
                     "ID: 0x%03lX\nLength: %d\nData: ",
                     message.identifier, message.data_length_code);

            // Append data bytes to the string
            for (int i = 0; i < message.data_length_code; i++) {
                snprintf(message_str + strlen(message_str), sizeof(message_str) - strlen(message_str),
                         "%02X ", message.data[i]);
            }
            // Add newline at the end of the string
            snprintf(message_str + strlen(message_str), sizeof(message_str) - strlen(message_str), "\n");

            // Append the formatted message to the global CAN data buffer
            strncat(can_data, message_str, sizeof(can_data) - strlen(can_data) - 1);   

            // Create a timer to update the textarea every 100ms
            lv_timer_t * t = lv_timer_create(can_update_textarea_cb, 100, NULL);
            lv_timer_set_repeat_count(t, 1);  // Execute once
        }
    
        // If the CAN Clear flag is set, update the display again
        if (CAN_Clear) {
            lv_timer_t * t = lv_timer_create(can_update_textarea_cb, 100, NULL);  // Update every 100ms
            lv_timer_set_repeat_count(t, 1);  // Execute once
        }
    }
}

/**
 * @brief Converts a hexadecimal string to an array of bytes.
 *
 * This function removes spaces from the input string and converts each pair
 * of hexadecimal characters to a byte, storing the result in the output array.
 * If the string contains invalid hexadecimal characters or cannot be fully 
 * converted, an error code is returned.
 *
 * @param input The input string to be converted.
 * @param output The output array to store the converted bytes.
 * @param max_output_size The maximum size of the output array.
 *
 * @return The number of bytes successfully converted, or a negative error code.
 */
int string_to_hex(const char *input, uint8_t *output, size_t max_output_size) {
    size_t len = 0;

    // Traverse the input string
    for (size_t i = 0; input[i] != '\0'; i++) {
        if (input[i] == ' ') continue;  // Skip spaces

        // Check if the character is a valid hexadecimal digit
        if (!isxdigit((int)input[i])) return -2;  // Invalid hexadecimal character
        if (len / 2 >= max_output_size) return -1;  // Output buffer is full

        // Convert the character to its hexadecimal value and store it in the output array
        output[len / 2] = (output[len / 2] << 4) | (uint8_t)strtol((char[]){input[i], '\0'}, NULL, 16);
        len++;
    }

    return (len % 2 == 0) ? len / 2 : -1;  // Return the number of bytes if valid, or an error code if invalid
}
