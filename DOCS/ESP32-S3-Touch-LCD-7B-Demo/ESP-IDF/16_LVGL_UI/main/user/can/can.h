#ifndef _CAN_
#define _CAN_

#include "twai.h"  // Include the TWAI (CAN) driver for communication with the CAN bus

// Define the maximum number of hexadecimal bytes that can be processed
#define MAX_HEX_DATA 32  // Maximum number of hexadecimal bytes

extern TaskHandle_t can_TaskHandle;  // Declare the task handle for the CAN task

/**
 * @brief Task for handling CAN communication.
 *
 * This task initializes the CAN interface, listens for CAN messages, 
 * processes the messages, and updates the user interface accordingly.
 * It continuously runs in a loop to read and process incoming CAN data.
 *
 * @param arg Argument passed to the task (not used in this function).
 */
void can_task(void *arg);

/**
 * @brief Converts a hexadecimal string to an array of bytes.
 *
 * This function takes an input string representing hexadecimal values (with or without spaces)
 * and converts it to an array of bytes. The result is stored in the output array.
 *
 * @param input The input hexadecimal string to be converted.
 * @param output The output array where the resulting bytes will be stored.
 * @param max_output_size The maximum size of the output array.
 *
 * @return The number of bytes successfully converted, or a negative error code:
 *         -1 if the output buffer is too small,
 *         -2 if the input string contains non-hexadecimal characters.
 */
int string_to_hex(const char *input, uint8_t *output, size_t max_output_size);

#endif
