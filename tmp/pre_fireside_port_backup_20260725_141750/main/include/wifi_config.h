#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize WiFi config subsystem: NVS flash, hostname from MAC.
 * Must be called before any other wifi_config or NVS functions.
 */
esp_err_t wifi_config_init(void);

/**
 * Load WiFi credentials from NVS into internal cache.
 * Returns true if valid credentials were found.
 */
bool wifi_config_load(void);

/** Check whether cached credentials are available. */
bool wifi_config_has_credentials(void);

/** Get device hostname ("esp32-XXYYZZ"). */
const char *wifi_config_get_hostname(void);

/**
 * Blocking WiFi connect using cached credentials.
 * Returns true when IP is assigned (up to 15 s timeout).
 */
bool wifi_connect(void);

/** Disconnect WiFi (does not tear down netif). */
void wifi_disconnect(void);

/**
 * Handle a CAN WiFi-config message (ID 0x01).
 * Multi-frame protocol: start/ssid chunks/password chunks/end+checksum.
 */
void wifi_config_handle_can(const uint8_t *data, uint8_t length);

/** Call periodically to reset stale partial config reception. */
void wifi_config_check_timeout(void);
