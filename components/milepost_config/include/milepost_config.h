#pragma once

/*
 * Persistent configuration for Milepost (NVS-backed, namespace "milepost").
 *
 * Stores WiFi credentials used by the OTA (main/ota.c) and Discovery
 * (main/discovery.c) modules. Two provisioning paths write here:
 *   - CAN 0x01 broadcast from Bearing (main/wifi_config.c → save_credentials)
 *   - In-UI wizard on PageWifiSetup (main/actions.c → action_wifi_submit_password)
 * Both converge on milepost_config_set_wifi(), so the store is a single
 * source of truth regardless of which path the user chose.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MILEPOST_CFG_SSID_MAX  33    /* 32 + NUL */
#define MILEPOST_CFG_PASS_MAX  65    /* 64 + NUL */

typedef struct {
    char wifi_ssid[MILEPOST_CFG_SSID_MAX];
    char wifi_pass[MILEPOST_CFG_PASS_MAX];
} milepost_config_t;

/* Initialize NVS namespace and load cached config. Safe to call once.
 * Caller must have already called nvs_flash_init(). */
esp_err_t milepost_config_init(void);

/* Pointer to the in-memory cached config. Read-only access; mutate via setters. */
const milepost_config_t *milepost_config_get(void);

/* True if a non-empty SSID is saved. */
bool milepost_config_has_wifi(void);

/* Write SSID + password to NVS and update cache. password may be empty for open APs. */
esp_err_t milepost_config_set_wifi(const char *ssid, const char *pass);

/* Erase WiFi creds from NVS and cache. */
esp_err_t milepost_config_clear_wifi(void);

#ifdef __cplusplus
}
#endif
