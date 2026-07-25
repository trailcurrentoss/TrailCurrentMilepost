#!/usr/bin/env python3
"""
Transform Fireside .eez-project -> Milepost .eez-project.

Removals:
  - topbar_battery_icon, topbar_battery_pct (TopBar user widget)
  - home_ptt_btn + children (PageHome)
  - slider_speaker_volume, settings_volume_lbl, settings_volume_pct (PageSettings)
  - PageMqttSetup screen
  - PageMqttConnecting screen
  - Actions: VolumeChanged, MqttSubmit, MqttSkip, MqttTogglePassword,
             PttPressed, PttReleased

Everything else preserved verbatim (widget geometry, styles, themes, fonts,
event handlers, action UUIDs).
"""
import json, sys, os

SRC = '/media/dave/extstorage/TrailCurrent/Product/TrailCurrentMilepost/GUI/TrailCurrentMilepost.eez-project.new'
DST = '/media/dave/extstorage/TrailCurrent/Product/TrailCurrentMilepost/GUI/TrailCurrentMilepost.eez-project'

REMOVE_IDENTIFIERS = {
    'topbar_battery_icon',
    'topbar_battery_pct',
    'home_ptt_btn',       # removes children too
    'slider_speaker_volume',
    'settings_volume_lbl',
    'settings_volume_pct',
}

REMOVE_PAGE_NAMES = {
    'PageMqttSetup',
    'PageMqttConnecting',
}

REMOVE_ACTION_NAMES = {
    'VolumeChanged',
    'MqttSubmit',
    'MqttSkip',
    'MqttTogglePassword',
    'PttPressed',
    'PttReleased',
}

def strip_children(children):
    """Recursively remove any widget whose identifier is in REMOVE_IDENTIFIERS.
    Also strips their subtrees (drop entire node = drop entire subtree)."""
    out = []
    for c in children:
        if isinstance(c, dict):
            idn = c.get('identifier') or ''
            if idn in REMOVE_IDENTIFIERS:
                continue
            # Recurse
            if 'children' in c and isinstance(c['children'], list):
                c['children'] = strip_children(c['children'])
            if 'components' in c and isinstance(c['components'], list):
                c['components'] = strip_children(c['components'])
        out.append(c)
    return out

def strip_event_handlers(obj):
    """Recursively remove eventHandlers referencing removed actions."""
    if isinstance(obj, dict):
        if 'eventHandlers' in obj and isinstance(obj['eventHandlers'], list):
            obj['eventHandlers'] = [
                h for h in obj['eventHandlers']
                if not (isinstance(h, dict) and h.get('action') in REMOVE_ACTION_NAMES)
            ]
        for k, v in obj.items():
            strip_event_handlers(v)
    elif isinstance(obj, list):
        for v in obj:
            strip_event_handlers(v)

def main():
    with open(SRC) as f:
        d = json.load(f)

    # 1) Delete MQTT pages
    d['userPages'] = [p for p in d.get('userPages', []) if p.get('name') not in REMOVE_PAGE_NAMES]

    # 2) Delete actions
    d['actions'] = [a for a in d.get('actions', []) if a.get('name') not in REMOVE_ACTION_NAMES]

    # 3) Strip identifiers (recursively) from all pages, user widgets
    for p in d.get('userPages', []):
        if 'components' in p:
            p['components'] = strip_children(p['components'])

    for uw in d.get('userWidgets', []):
        if 'components' in uw:
            uw['components'] = strip_children(uw['components'])

    # 4) Strip event handlers referencing removed actions
    strip_event_handlers(d)

    # 5) Write
    with open(DST, 'w') as f:
        json.dump(d, f, indent=2)
    # Preserve trailing newline convention
    with open(DST, 'a') as f:
        f.write('\n')
    print(f"Wrote {DST}")

    # Report
    print("\nRemaining pages:")
    for p in d.get('userPages', []):
        print(f"  {p['name']}")
    print("\nRemaining actions:")
    for a in d.get('actions', []):
        print(f"  {a['name']}")

if __name__ == '__main__':
    main()
