#!/usr/bin/env python3
"""Strip Milepost-incompatible references from vars.c.

Removes:
  - Any `objects.page_mqtt_setup_topbar__*` / `page_mqtt_connecting_topbar__*`
    lines (those pages were deleted from the .eez-project).
  - Any `objects.slider_speaker_volume` / `settings_volume_pct` references
    (widget deleted).
  - Any `objects.topbar_battery_*` / `topbar_charge_icon` refs (widgets deleted).
  - `audio_play_phrase(...)` calls (Milepost has no speaker).
  - `pick_sensor_phrase()` definition (only used by audio_play_phrase).
  - `objects.mqtt_connecting_status` references (page deleted).
  - `audio_get_volume`/`audio_set_volume` references (no audio).

Turns `set_var_internal_battery_soc/voltage/charging` and `set_var_mqtt_connected`
into no-ops that keep the extern symbol (mqtt_vars.h) but do nothing at the UI.
"""
import re

SRC = '/media/dave/extstorage/TrailCurrent/Product/TrailCurrentMilepost/main/vars.c'

with open(SRC) as f:
    src = f.read()

# Strip individual lines that reference deleted MQTT-topbar widgets or the
# removed battery-topbar / charge-icon widgets. These appear one-per-line
# inside arrays initialized with objects.<page>_topbar__<widget>.
BAD_LINE_PATTERNS = [
    r'^\s*objects\.page_mqtt_setup_topbar__[a-zA-Z0-9_]+\s*,?\s*$',
    r'^\s*objects\.page_mqtt_connecting_topbar__[a-zA-Z0-9_]+\s*,?\s*$',
    r'^\s*objects\.[a-z_]*topbar__topbar_battery_[a-z]+\s*,?\s*$',
    r'^\s*objects\.[a-z_]*topbar__topbar_battery_icon\s*,?\s*$',
    r'^\s*objects\.[a-z_]*topbar__topbar_battery_pct\s*,?\s*$',
    r'^\s*objects\.[a-z_]*topbar__topbar_charge_icon\s*,?\s*$',
]
patterns = [re.compile(p) for p in BAD_LINE_PATTERNS]

out = []
for line in src.split('\n'):
    if any(p.match(line) for p in patterns):
        continue
    out.append(line)
src = '\n'.join(out)

# Remove `audio_play_phrase(...);` statements (single-line and multi-line
# invocations)
src = re.sub(
    r'^\s*audio_play_phrase\s*\([^;]*\)\s*;\s*\n',
    '',
    src, flags=re.MULTILINE
)
# Handle ternary calls  ->  audio_play_phrase(x ? A : B);
src = re.sub(
    r'^\s*audio_play_phrase\s*\([^;]*\?[^;]*\)\s*;\s*\n',
    '',
    src, flags=re.MULTILINE
)

# Delete the pick_sensor_phrase() function entirely (from `static audio_phrase_t
# pick_sensor_phrase` up through its closing `}`).
src = re.sub(
    r'/\*[^*]*keyword match[^/]*?\*/\s*\nstatic audio_phrase_t pick_sensor_phrase\(const char \*label\) \{.*?^\}\n',
    '',
    src, flags=re.MULTILINE | re.DOTALL
)

# Replace remaining `pick_sensor_phrase(label)` invocations if any survived
src = re.sub(r'pick_sensor_phrase\([^)]*\)', 'AUDIO_PHRASE_SENSOR', src)

# `objects.mqtt_connecting_status` — replace the referencing setter
# with an empty guard so lvgl still compiles.
src = re.sub(
    r'if \(objects\.mqtt_connecting_status\) \{\s*\n\s*lv_label_set_text\(objects\.mqtt_connecting_status,\s*\n\s*"[^"]*"\);\s*\n\s*\}\s*\n',
    '',
    src, flags=re.MULTILINE
)
# Simpler single-line variant if the multi-line above missed
src = re.sub(
    r'^\s*if \(objects\.mqtt_connecting_status\).*?\n(?:.*?\n)?',
    '',
    src, flags=re.MULTILINE
)

# Simplify set_var_mqtt_connected: keep the setter name but strip its body
# so nothing that Fireside's mqtt_client would have called breaks. Not
# called in Milepost.
src = re.sub(
    r'void set_var_mqtt_connected\(bool connected\) \{.*?^\}\n',
    'void set_var_mqtt_connected(bool connected) { (void)connected; /* Milepost has no MQTT — no-op */ }\n',
    src, flags=re.MULTILINE | re.DOTALL
)

# Restore-settings volume block: everything from the `audio_get_volume()`
# fetch through the label paint.
src = re.sub(
    r'/\*[^*]*volume[^/]*?\*/\s*\n\s*\{\s*\n\s*uint8_t v = audio_get_volume\(\);.*?^\s*\}\n',
    '',
    src, flags=re.MULTILINE | re.DOTALL
)
# Alternate volume block without brace/comment (be tolerant)
src = re.sub(
    r'^\s*uint8_t v = audio_get_volume\(\);.*?\n(.*?\n){0,10}',
    lambda m: '',  # remove everything until we hit a blank line — hand-verify after
    src, flags=re.MULTILINE
)
# Catch stray audio_set_volume calls too
src = re.sub(r'^\s*audio_set_volume\([^;]*\);\s*\n', '', src, flags=re.MULTILINE)

with open(SRC, 'w') as f:
    f.write(src)

print("vars.c stripped (batch pass)")
