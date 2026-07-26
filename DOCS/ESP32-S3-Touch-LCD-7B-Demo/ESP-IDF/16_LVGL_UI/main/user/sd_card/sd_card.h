/*****************************************************************************
 * | File         :   sd.h
 * | Author       :   Waveshare team
 * | Function     :   SD card configuration header file
 * | Info         :
 * |                 This header file declares the initialization interface for the SD card.
 * |                 
 * |                 
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-28
 * | Info         :   Basic version
 *
 ******************************************************************************/

 #ifndef __SD_CARD_H
 #define __SD_CARD_H
 
 #include "sd.h"          // Include the SD card driver header file (assuming this is the correct file for SD card operations)
 
 /**
  * @brief Initialize the SD card.
  * 
  * This function initializes the SD card and retrieves its information.
  * It formats the information into a string and updates the UI label with the formatted string.
  * 
  * @note This function assumes that the SD card is connected and properly configured.
  * @note The function uses the `lvgl` library to update the UI label.
  */
 void sd_init();
 
 #endif  // __SD_CARD_H