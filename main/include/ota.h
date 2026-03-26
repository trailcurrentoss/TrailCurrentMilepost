#ifndef OTA_H
#define OTA_H

#include "driver/twai.h"

// Call once from app_main() after nvs_flash_init() to read device MAC
void ota_init(void);

// Call from CAN RX task when CAN ID 0x00 is received.
// Compares MAC bytes in message to this device — sets trigger flag on match.
void ota_check_can_trigger(const twai_message_t *msg);

// Call from main loop each iteration. Non-blocking state machine that manages
// WiFi connect, HTTP server, OTA flash, LVGL status overlay, and cleanup.
void ota_process(void);

#endif // OTA_H
