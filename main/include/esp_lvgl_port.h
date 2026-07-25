#pragma once

/*
 * esp_lvgl_port.h — Milepost stand-in for the Espressif esp_lvgl_port
 * component.
 *
 * Milepost drives LVGL directly from app_main's loop (see main.c) rather
 * than from a BSP-managed task. Any code that touches LVGL widgets from
 * other tasks — the WiFi event callback in app_state.c, the OTA HTTP
 * server task in ota.c — needs a mutex to serialize with the main loop.
 * The two symbols below match the esp_lvgl_port API so those callers
 * don't need Milepost-specific glue. Implementation lives in main.c
 * (recursive FreeRTOS mutex, created before ui_init).
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Acquire the LVGL access mutex. `timeout_ms == 0` waits forever.
 * Returns true if the mutex was taken, false on timeout. Recursive — a
 * single task may lock multiple times without deadlocking itself. */
bool lvgl_port_lock(uint32_t timeout_ms);

/* Release the LVGL access mutex. Must be paired with a successful
 * lvgl_port_lock call on the same task. */
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
