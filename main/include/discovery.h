#pragma once

#include <stdbool.h>

// Discovery window duration (3 minutes)
#define DISCOVERY_TIMEOUT_MS 180000

/**
 * Initialize discovery subsystem.
 * Must be called after wifi_config_init().
 */
void discovery_init(void);

/**
 * Handle a CAN discovery trigger (ID 0x02, broadcast).
 * Joins WiFi, advertises via mDNS with module metadata,
 * and waits for Headwaters to confirm registration.
 */
void discovery_handle_trigger(void);

/**
 * Check whether discovery is currently in progress.
 * Used by OTA to enforce mutual exclusion.
 */
bool discovery_is_running(void);

/**
 * Call from main loop to update LVGL overlay during discovery.
 * Must only be called from the LVGL thread (main loop).
 */
void discovery_update_ui(void);
