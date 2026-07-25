#ifndef MILEPOST_ALARMS_H
#define MILEPOST_ALARMS_H

/*
 * Milepost alarms — active-alarm evaluator.
 *
 * Alarms are computed locally from CAN inputs. main.c's handle_can_frame
 * dispatches per-board status frames into alarms_apply_inputs() (bit
 * patterns from Switchback / Picket boards) and set_var_battery_soc()
 * calls alarms_apply_battery() with the latest shunt SoC. Everything
 * is evaluated against a single ESP-owned arm config stored in NVS
 * (namespace "milepost_alarm").
 *
 * Single config (no per-mode profiles) — the user has one arm set that
 * applies at all times. Milepost is visual-only (no speaker on this
 * board), so the "active" state surfaces as the topbar bell badge and
 * the toaster panel, not TTS.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Source of an alarm input. */
typedef enum {
    ALARM_SRC_SWITCHBACK = 0,  /* 8-bit DI board  (CAN 0x28-0x2A status frames) */
    ALARM_SRC_PICKET     = 1,  /* 12-bit reed switch board (no CAN wiring yet) */
} alarm_src_t;

/* Initialize the alarms module. Loads arm config from NVS (namespace
 * "milepost_alarm") if present; otherwise leaves everything disarmed so
 * no false positives fire. Call once from main.c after nvs_flash_init(). */
void alarms_init(void);

/* CAN-input dispatcher. Called from main.c's handle_can_frame on every
 * Switchback (0x28-0x2A) status message. `bits` is the raw 8-bit
 * (switchback) or 12-bit (picket) input word — bit N high means sensor N
 * is asserted. `addr` is the board instance 0..2. */
void alarms_apply_inputs(alarm_src_t src, uint8_t addr, uint16_t bits);

/* Update the battery-critical alarm input. Called from set_var_battery_soc.
 * The battery-critical alarm is included in the active count iff the
 * battery-alarm switch is enabled AND percent < threshold. */
void alarms_apply_battery(int32_t percent);

/* Current active-alarm count (0..N). Consumed by paint_notif_badge() in
 * vars.c to render the topbar bell + count. */
int alarms_active_count(void);

/* Rising-edge event — what just went from inactive → active. vars.c
 * drains this queue every tick; on Milepost the payload is dropped
 * (visual-only, no audio) but the queue itself is kept so future audible
 * output — or a serial-log breadcrumb — can be added without changing
 * alarms.c. */
typedef struct {
    bool          is_battery;   /* true → the battery-below-threshold event.
                                 * src/addr/sensor are undefined in that case. */
    alarm_src_t   src;
    uint8_t       addr;
    uint8_t       sensor;       /* 0-based within the board */
} alarm_edge_t;

/* Run once per tick (1 Hz from vars.c). Compares the newly-computed active
 * bitmap against the previously-saved one, pushes any newly-set bits (and
 * the battery-crossed-threshold event) into an internal edge queue, and
 * updates the saved bitmap. First call establishes the baseline and pushes
 * nothing — matches the s_baseline_seen pattern in vars.c.
 *
 * Also queues re-alert events for any alarm that has been active for at
 * least `alarms_get_snooze_secs()` seconds since the last announcement.
 * This is the "keep reminding the user" behavior. Consumers see re-alerts
 * as ordinary alarm_edge_t events on the queue; no special-casing needed. */
void alarms_tick_edges(void);

/* Pop the next rising-edge event. Returns true and fills *out if there
 * was one; false if the queue is empty. Non-blocking. Consumer drains the
 * queue in a loop each tick. */
bool alarms_pop_rising_edge(alarm_edge_t *out);

/* Report which board addresses have published inputs since boot (i.e.
 * which sensor modules are online). Writes up to `max` addresses into
 * `addrs_out` (sorted ascending) and returns the count. Consumers use
 * this to render one row per (src, addr, sensor) they can arm. */
int alarms_get_known_boards(alarm_src_t src, uint8_t *addrs_out, int max);

/* Per-sensor arm inspector + setter. Sensor index is 0-based within the
 * board (0..7 for Switchback, 0..11 for Picket). Setter persists the
 * whole board's bitmap to NVS. */
bool alarms_is_armed(alarm_src_t src, uint8_t addr, uint8_t sensor);
void alarms_set_armed(alarm_src_t src, uint8_t addr, uint8_t sensor, bool armed);

/* Per-sensor polarity — whether the alarm triggers on the input HIGH
 * ("normal": cabinet door open = HIGH) or on the input LOW ("inverted":
 * power lost = LOW). Two independent bits per sensor: armed (above) +
 * polarity here. Alarm fires iff (input XOR polarity) == 1 AND armed.
 *
 * `inverted = false` (default) → alarm on HIGH.
 * `inverted = true`            → alarm on LOW. */
bool alarms_is_inverted(alarm_src_t src, uint8_t addr, uint8_t sensor);
void alarms_set_inverted(alarm_src_t src, uint8_t addr, uint8_t sensor,
                         bool inverted);

/* Per-sensor label. Default is "SB0 · S3" / "PK1 · S7" style; user can
 * rename via the alarms page for a friendlier display + future TTS.
 * Max 24 chars including nul terminator.
 * `out_buf` filled with a null-terminated string; returns non-zero if the
 * caller-supplied `out_buf` needs to hold at least `strlen(out) + 1`
 * bytes. Pass a 32-byte buffer to be safe. */
#define ALARM_LABEL_MAX 24
void alarms_get_label(alarm_src_t src, uint8_t addr, uint8_t sensor,
                      char *out_buf, size_t buf_sz);
void alarms_set_label(alarm_src_t src, uint8_t addr, uint8_t sensor,
                      const char *label);

/* Re-alert snooze window, in seconds. If a sensor / battery alarm is still
 * active after this many seconds since the last TTS announcement, the tick
 * pushes another edge event and the announcement fires again — matching
 * Persisted to NVS
 * key "snooze_s". Range clamped to [ALARM_SNOOZE_SECS_MIN, MAX]; default
 * is ALARM_SNOOZE_SECS_DEFAULT. */
#define ALARM_SNOOZE_SECS_MIN     30
#define ALARM_SNOOZE_SECS_MAX     900
#define ALARM_SNOOZE_SECS_DEFAULT 120
int  alarms_get_snooze_secs(void);
void alarms_set_snooze_secs(int secs);

/* User acknowledged the active alarms (topbar notif icon tap). For every
 * alarm currently active, reset the per-alarm "last announced" timestamp
 * to now — so the next re-alert doesn't fire until another snooze window
 * elapses. Alarms whose condition has already cleared are not touched
 * (they weren't going to re-alert anyway). Returns the number of alarms
 * that were acknowledged, for logging. */
int alarms_acknowledge_all(void);

/* Per-alarm acknowledge — slides ONLY the specified alarm's last-announced
 * timestamp forward to now, so its next re-alert fires one snooze window
 * from now instead of from the original announcement. Used by the topbar
 * toaster (one Ack button per active alarm) so the user can silence a
 * single nuisance without also snoozing the others.
 *
 * Returns true if the alarm is still active (was ack'd); false if it has
 * already cleared or the coordinates don't match a known alarm. */
bool alarms_acknowledge_sensor(alarm_src_t src, uint8_t addr, uint8_t sensor);
bool alarms_acknowledge_battery(void);

/* Enumerate currently-active alarms. The callback is invoked once per
 * active alarm with the alarm's coordinates in an `alarm_edge_t` (same
 * shape as the rising-edge queue — is_battery=true marks the battery
 * entry, otherwise src/addr/sensor identify the sensor). Enumeration
 * order is stable across calls provided the arm bitmaps don't change:
 * switchback boards ascending by addr then sensor bit, then picket
 * boards, then battery. */
typedef void (*alarms_enum_cb_t)(const alarm_edge_t *entry, void *ctx);
int  alarms_enumerate_active(alarms_enum_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* MILEPOST_ALARMS_H */
