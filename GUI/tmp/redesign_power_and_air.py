#!/usr/bin/env python3
"""
Redesign PagePower and PageAir per user's mockups:
  - Remove all historical chart panels (power_*_chart, air_*_chart)
  - PageAir: 5 current-measurement cards (temp/hum/tvoc top, eco2/co bottom)
             with icons + gradient bars + status pills; header title/subtitle/overall-pill
  - PagePower: 3-column layout — solar+shore (left), battery big card (middle),
               consumption arc gauge (right)
  - Preserve every widget identifier the C code depends on (see PRESERVE set below).
  - Match Milepost visual language (existing styles + palette). No new styles.

Idempotent: re-running is a no-op if the redesign has already been applied
(detected by presence of the new sentinel identifier `air_header_title`).
"""

import base64, hashlib, json, os, shutil, sys, time

PROJ = "/media/dave/extstorage/TrailCurrent/Product/TrailCurrentMilepost/GUI/TrailCurrentMilepost.eez-project"

# Deterministic UUID generation so re-runs produce stable JSON diffs.
_seq = [0]
def oid(tag):
    _seq[0] += 1
    h = hashlib.md5(f"redesign:{tag}:{_seq[0]}".encode()).hexdigest()
    return f"{h[:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:32]}"

# ---------- widget builders (mirror observed shapes) ----------

def _base_widget(ident, wtype, x, y, w, h, tag):
    """Fields common to every EEZ Studio widget."""
    return {
        "objID": oid(f"{tag}:{ident or 'anon'}"),
        "type": wtype,
        "left": x, "top": y, "width": w, "height": h,
        "customInputs": [], "customOutputs": [],
        "style": {
            "objID": oid(f"{tag}:{ident or 'anon'}:style"),
            "useStyle": "default",
            "conditionalStyles": [], "childStyles": [],
        },
        "timeline": [], "eventHandlers": [],
        "leftUnit": "px", "topUnit": "px",
        "widthUnit": "px", "heightUnit": "px",
        "children": [],
        "widgetFlags": "",
        "hiddenFlagType": "literal",
        "clickableFlag": False, "clickableFlagType": "literal",
        "flagScrollbarMode": "", "flagScrollDirection": "",
        "scrollSnapX": "", "scrollSnapY": "",
        "checkedStateType": "literal", "disabledStateType": "literal",
        "states": "",
        "localStyles": {
            "objID": oid(f"{tag}:{ident or 'anon'}:ls"),
        },
        "group": "", "groupIndex": 0,
    }

def _put_local_style(w, main_default):
    """Attach a MAIN/DEFAULT localStyles block."""
    w["localStyles"]["definition"] = {
        "MAIN": {"DEFAULT": main_default}
    }

def _put_local_style_parts(w, parts):
    """parts = {PART_NAME: {STATE_NAME: {props...}}}"""
    w["localStyles"]["definition"] = parts

def label(ident, x, y, w, h, text, font="rr14", color="TextPrimary",
          align="LEFT", long_mode="CLIP"):
    n = _base_widget(ident, "LVGLLabelWidget", x, y, w, h, "lbl")
    if ident:
        n["identifier"] = ident
    _put_local_style(n, {
        "text_font": font,
        "text_color": color,
        "text_align": align,
    })
    n["text"] = text
    n["textType"] = "literal"
    n["longMode"] = long_mode
    n["recolor"] = False
    return n

def panel(ident, x, y, w, h, use_style=None, children=None, local_style=None):
    n = _base_widget(ident, "LVGLPanelWidget", x, y, w, h, "pnl")
    if ident:
        n["identifier"] = ident
    if use_style:
        n["useStyle"] = use_style
    if local_style:
        _put_local_style(n, local_style)
    if children:
        n["children"] = children
    return n

def bar(ident, x, y, w, h, value=0, vmin=0, vmax=100,
        indicator_color="AccentPrimary", grad_end_color=None):
    n = _base_widget(ident, "LVGLBarWidget", x, y, w, h, "bar")
    if ident:
        n["identifier"] = ident
    parts = {
        "MAIN": {"DEFAULT": {
            "bg_color": "BgBar",
            "bg_opa": 255,
            "border_width": 0,
            "border_opa": 0,
            "radius": 4,
            "pad_all": 0,
            "pad_top": 0, "pad_bottom": 0,
            "pad_left": 0, "pad_right": 0,
        }},
        "INDICATOR": {"DEFAULT": {
            "bg_color": indicator_color,
            "bg_opa": 255,
            "radius": 4,
        }},
    }
    if grad_end_color:
        parts["INDICATOR"]["DEFAULT"]["bg_grad_color"] = grad_end_color
        parts["INDICATOR"]["DEFAULT"]["bg_grad_dir"] = "HOR"
    _put_local_style_parts(n, parts)
    n["min"] = vmin; n["minType"] = "literal"
    n["max"] = vmax; n["maxType"] = "literal"
    n["mode"] = "NORMAL"
    n["value"] = value; n["valueType"] = "literal"
    n["valueStart"] = 0; n["valueStartType"] = "literal"
    n["enableAnimation"] = False
    return n

def arc(ident, x, y, w, h, arc_color="AccentPrimary", arc_width=20,
        rmin=0, rmax=100, value=0, bg_start=135, bg_end=45):
    n = _base_widget(ident, "LVGLArcWidget", x, y, w, h, "arc")
    if ident:
        n["identifier"] = ident
    parts = {
        "MAIN": {"DEFAULT": {
            "bg_opa": 0, "border_width": 0, "border_opa": 0,
            "arc_color": "BorderColor",
            "arc_width": arc_width, "arc_opa": 255, "arc_rounded": True,
            "pad_top": 0, "pad_bottom": 0,
            "pad_left": 0, "pad_right": 0, "pad_all": 0,
        }},
        "INDICATOR": {"DEFAULT": {
            "arc_color": arc_color,
            "arc_width": arc_width, "arc_opa": 255, "arc_rounded": True,
        }},
        "KNOB": {"DEFAULT": {"bg_opa": 0, "border_opa": 0, "pad_all": 0}},
    }
    _put_local_style_parts(n, parts)
    n["rangeMin"] = rmin; n["rangeMinType"] = "literal"
    n["rangeMax"] = rmax; n["rangeMaxType"] = "literal"
    n["value"] = value; n["valueType"] = "literal"
    n["bgStartAngle"] = bg_start
    n["bgEndAngle"] = bg_end
    n["mode"] = "NORMAL"
    n["rotation"] = 0
    return n

# ---------- Identifiers we PRESERVE (C code depends on them) ----------
PRESERVE = {
    # Air value/unit/label widgets referenced from vars.c
    "air_temp_value", "air_temp_unit",
    "air_hum_value",
    "air_eco2_value",
    "air_tvoc_value",
    "air_co_value",
    "air_rec_text", "air_rec_lbl",
    "air_temp_badge", "air_temp_badge_l",
    "air_hum_badge",  "air_hum_badge_l",
    "air_tvoc_badge", "air_tvoc_badge_l",
    "air_eco2_badge", "air_eco2_badge_l",
    "air_co_badge",   "air_co_badge_l",
    # Power value/unit/label widgets referenced from vars.c
    "power_solar_value", "power_solar_unit",
    "power_charge_type",
    "power_soc_value",
    "power_volts_value",
    "power_load_value",
    "power_time_value", "power_time_load",
    "power_shore_state",
}

# ---------- FA icon codepoints we need for card icons ----------
NEW_FA_CODEPOINTS = [
    0xf2c7,  # thermometer
    0xf72e,  # wind
    0xf132,  # shield
    # 0xf043 (droplet) and 0xf0c2 (cloud) already in every fa font subset
]

# ---------- New PageAir tree ----------

def build_air_card_metric(ident_prefix, icon_glyph, value_ident, unit_ident,
                          card_title, unit_text, bar_ident, bar_min, bar_max,
                          bar_indicator_color, bar_grad_color,
                          marker_left, marker_mid, marker_right,
                          badge_ident=None, badge_l_ident=None,
                          x=0, y=0, w=328, h=180):
    """Build one Air-quality metric card."""
    children = []
    # Icon
    children.append(label(
        f"{ident_prefix}_icon", 16, 14, 32, 32,
        icon_glyph, font="fa20", color="TextSecondary", align="CENTER"
    ))
    # Value (BIG)
    children.append(label(
        value_ident, 56, 8, 130, 38,
        "--", font="rl26", color="TextPrimary", align="LEFT"
    ))
    # Unit
    children.append(label(
        unit_ident, 188, 20, 40, 18,
        unit_text, font="rr14", color="TextMuted", align="LEFT"
    ))
    # Card title (uppercase small caps)
    children.append(label(
        f"{ident_prefix}_label", 56, 46, 200, 14,
        card_title, font="rm11", color="TextMuted", align="LEFT"
    ))
    # Gradient bar
    children.append(bar(
        bar_ident, 16, 92, 296, 8,
        value=0, vmin=bar_min, vmax=bar_max,
        indicator_color=bar_indicator_color,
        grad_end_color=bar_grad_color,
    ))
    # Marker labels below bar
    children.append(label(
        None, 16, 108, 88, 12,
        marker_left, font="rr11", color="TextMuted", align="LEFT"
    ))
    children.append(label(
        None, 104, 108, 120, 12,
        marker_mid, font="rr11", color="TextMuted", align="CENTER"
    ))
    children.append(label(
        None, 224, 108, 88, 12,
        marker_right, font="rr11", color="TextMuted", align="RIGHT"
    ))
    # Status pill (only for cards that have one)
    if badge_ident and badge_l_ident:
        badge_children = [
            label(badge_l_ident, 0, 4, 76, 14,
                  "--", font="rm11", color="TextPrimary", align="CENTER")
        ]
        badge_panel = panel(
            badge_ident, 236, 12, 76, 22, use_style=None,
            children=badge_children,
            local_style={
                "bg_color": "BgBar",
                "bg_opa": 255,
                "border_width": 0,
                "border_opa": 0,
                "radius": 11,
                "pad_all": 0,
                "pad_top": 0, "pad_bottom": 0,
                "pad_left": 0, "pad_right": 0,
            }
        )
        children.append(badge_panel)

    return panel(
        f"{ident_prefix}_card", x, y, w, h,
        use_style="Card", children=children
    )

def build_air_body():
    """Rebuild air_body children — header row + 5 cards in 2 rows."""
    # Header row (y=0..64)
    header_title = label(
        "air_header_title", 12, 8, 400, 30,
        "Air Quality", font="rl26", color="TextPrimary", align="LEFT"
    )
    # Subtitle — PRESERVED identifier `air_rec_text`
    subtitle = label(
        "air_rec_text", 12, 44, 500, 16,
        "--", font="rr14", color="TextSecondary", align="LEFT"
    )
    # Overall status pill (right side). The pill panel is new (`air_overall_pill`),
    # but its inner label REUSES the preserved identifier `air_rec_lbl` so vars.c
    # continues to work.
    overall_pill_l = label(
        "air_rec_lbl", 0, 4, 160, 20,
        "--", font="rm11", color="TextPrimary", align="CENTER"
    )
    overall_pill = panel(
        "air_overall_pill", 852, 20, 160, 28, use_style=None,
        children=[overall_pill_l],
        local_style={
            "bg_color": "BgBar",
            "bg_opa": 255,
            "border_width": 0,
            "border_opa": 0,
            "radius": 14,
            "pad_all": 0,
            "pad_top": 0, "pad_bottom": 0,
            "pad_left": 0, "pad_right": 0,
        }
    )

    # 5 metric cards.
    # Row 1 (y=80): temp | humidity | tvoc
    # Row 2 (y=280): eco2 | co
    cards = []
    cards.append(build_air_card_metric(
        "air_temp",
        icon_glyph="",
        value_ident="air_temp_value",
        unit_ident="air_temp_unit",
        card_title="TEMPERATURE",
        unit_text="°F",
        bar_ident="air_temp_bar",
        bar_min=40, bar_max=100,
        bar_indicator_color="Info",
        bar_grad_color="Danger",
        marker_left="40°",
        marker_mid="Comfort",
        marker_right="100°",
        badge_ident="air_temp_badge",
        badge_l_ident="air_temp_badge_l",
        x=12, y=80,
    ))
    cards.append(build_air_card_metric(
        "air_hum",
        icon_glyph="",
        value_ident="air_hum_value",
        unit_ident="air_hum_unit_new",
        card_title="HUMIDITY",
        unit_text="%",
        bar_ident="air_hum_bar",
        bar_min=0, bar_max=100,
        bar_indicator_color="Info",
        bar_grad_color="AccentPrimary",
        marker_left="Dry",
        marker_mid="30-50% ideal",
        marker_right="Humid",
        badge_ident="air_hum_badge",
        badge_l_ident="air_hum_badge_l",
        x=348, y=80,
    ))
    cards.append(build_air_card_metric(
        "air_tvoc",
        icon_glyph="",
        value_ident="air_tvoc_value",
        unit_ident="air_tvoc_unit_new",
        card_title="TVOC",
        unit_text="ppb",
        bar_ident="air_tvoc_bar",
        bar_min=0, bar_max=1000,
        bar_indicator_color="AccentPrimary",
        bar_grad_color="Danger",
        marker_left="0",
        marker_mid="220 moderate",
        marker_right="1000+",
        badge_ident="air_tvoc_badge",
        badge_l_ident="air_tvoc_badge_l",
        x=684, y=80,
    ))
    cards.append(build_air_card_metric(
        "air_eco2",
        icon_glyph="",
        value_ident="air_eco2_value",
        unit_ident="air_eco2_unit_new",
        card_title="ECO₂",
        unit_text="ppm",
        bar_ident="air_eco2_bar",
        bar_min=0, bar_max=2000,
        bar_indicator_color="AccentPrimary",
        bar_grad_color="Danger",
        marker_left="400",
        marker_mid="1000 high",
        marker_right="2000+",
        badge_ident="air_eco2_badge",
        badge_l_ident="air_eco2_badge_l",
        x=12, y=280,
    ))
    cards.append(build_air_card_metric(
        "air_co",
        icon_glyph="",
        value_ident="air_co_value",
        unit_ident="air_co_unit_new",
        card_title="CO — CARBON MONOXIDE",
        unit_text="ppm",
        bar_ident="air_co_bar",
        bar_min=0, bar_max=200,
        bar_indicator_color="AccentPrimary",
        bar_grad_color="Danger",
        marker_left="0",
        marker_mid="70 warn",
        marker_right="200+",
        badge_ident="air_co_badge",
        badge_l_ident="air_co_badge_l",
        x=348, y=280,
    ))

    return [header_title, subtitle, overall_pill] + cards

# ---------- New PagePower tree ----------

def build_power_solar_card():
    """Left col top: solar wattage + charging status."""
    children = []
    # Sun icon
    children.append(label(
        "power_solar_icon", 16, 16, 32, 32,
        "", font="fa24", color="Solar", align="CENTER"
    ))
    # Big value
    children.append(label(
        "power_solar_value", 56, 12, 160, 40,
        "--", font="rl26", color="Solar", align="LEFT"
    ))
    # Unit "WATTS"
    children.append(label(
        "power_solar_unit", 56, 56, 100, 14,
        "WATTS", font="rm11", color="TextMuted", align="LEFT"
    ))
    # Charging state text
    children.append(label(
        "power_charge_type", 16, 100, 248, 30,
        "--", font="rr20", color="TextPrimary", align="LEFT"
    ))
    # Subtitle
    children.append(label(
        None, 16, 134, 248, 14,
        "Charge status", font="rm11", color="TextMuted", align="LEFT"
    ))
    return panel("power_solar_card", 12, 12, 280, 160,
                 use_style="Card", children=children)

def build_power_shore_card():
    """Left col bottom: Shore Power section."""
    children = []
    # Section title
    children.append(label(
        None, 16, 12, 248, 14,
        "SHORE POWER", font="rm11", color="TextMuted", align="LEFT"
    ))
    # Plug icon (0xf1e6, already in fa font subsets)
    children.append(label(
        None, 124, 44, 32, 32,
        "", font="fa24", color="TextSecondary", align="CENTER"
    ))
    # State text
    children.append(label(
        "power_shore_state", 16, 90, 248, 24,
        "--", font="rr20", color="TextPrimary", align="CENTER"
    ))
    return panel("power_shore_card", 12, 184, 280, 140,
                 use_style="Card", children=children)

def build_power_battery_card():
    """Middle col: big battery status card."""
    children = []
    # Header
    children.append(label(
        None, 16, 12, 368, 16,
        "BATTERY STATUS", font="rm11", color="TextMuted", align="LEFT"
    ))
    # BIG SOC value
    children.append(label(
        "power_soc_value", 24, 90, 260, 90,
        "--", font="rl104", color="TextPrimary", align="CENTER"
    ))
    # SOC % unit
    children.append(label(
        None, 290, 130, 60, 30,
        "%", font="rr22", color="TextPrimary", align="LEFT"
    ))
    # Divider
    children.append(panel(
        None, 40, 218, 320, 1, use_style=None,
        local_style={
            "bg_color": "BorderColor",
            "bg_opa": 255,
            "border_width": 0, "border_opa": 0,
        }
    ))
    # Voltage BIG
    children.append(label(
        "power_volts_value", 24, 250, 260, 70,
        "--", font="rl26", color="AccentPrimary", align="CENTER"
    ))
    # V unit
    children.append(label(
        None, 290, 274, 60, 24,
        "V", font="rr22", color="AccentPrimary", align="LEFT"
    ))
    # Subtitle
    children.append(label(
        None, 16, 340, 368, 14,
        "State of charge / bus voltage",
        font="rm11", color="TextMuted", align="CENTER"
    ))
    return panel("power_battery_card", 304, 12, 400, 380,
                 use_style="Card", children=children)

def build_power_consumption_card():
    """Right col: consumption arc gauge with hours remaining center + watts below."""
    children = []
    # Header
    children.append(label(
        None, 16, 12, 264, 16,
        "BATTERY CONSUMPTION", font="rm11", color="TextMuted", align="LEFT"
    ))
    # Big ring arc gauge (SOC-like proxy — actually driven by load% of some max in C)
    children.append(arc(
        "power_load_arc", 40, 40, 216, 216,
        arc_color="AccentPrimary", arc_width=20,
        rmin=0, rmax=100, value=0,
        bg_start=135, bg_end=45,
    ))
    # Center text: hours remaining
    children.append(label(
        "power_time_value", 40, 116, 216, 46,
        "----", font="rl26", color="TextPrimary", align="CENTER"
    ))
    # Center subtitle: "Remaining"
    children.append(label(
        None, 40, 166, 216, 16,
        "Remaining", font="rm11", color="TextMuted", align="CENTER"
    ))
    # Below arc: watts value
    children.append(label(
        "power_load_value", 24, 280, 200, 40,
        "--", font="rl26", color="AccentPrimary", align="RIGHT"
    ))
    # WATTS label + subtitle
    children.append(label(
        None, 232, 296, 48, 20,
        "WATTS", font="rm11", color="TextMuted", align="LEFT"
    ))
    # Load subtitle
    children.append(label(
        "power_time_load", 16, 340, 264, 14,
        "at 0 W draw", font="rm11", color="TextMuted", align="CENTER"
    ))
    return panel("power_consumption_card", 716, 12, 296, 380,
                 use_style="Card", children=children)

def build_power_body():
    return [
        build_power_solar_card(),
        build_power_shore_card(),
        build_power_battery_card(),
        build_power_consumption_card(),
    ]

# ---------- Tree traversal helpers ----------

def find_page(proj, name):
    for p in proj["userPages"]:
        if p.get("name") == name: return p
    raise KeyError(name)

def find_node(root, ident):
    if isinstance(root, dict):
        if root.get("identifier") == ident: return root
        for v in root.values():
            r = find_node(v, ident)
            if r is not None: return r
    elif isinstance(root, list):
        for x in root:
            r = find_node(x, ident)
            if r is not None: return r
    return None

def all_identifiers(root):
    out = set()
    def walk(n):
        if isinstance(n, dict):
            i = n.get("identifier")
            if i: out.add(i)
            for v in n.values(): walk(v)
        elif isinstance(n, list):
            for x in n: walk(x)
    walk(root)
    return out

# ---------- FA font range extension ----------

def add_fa_codepoints(proj, font_name, codepoints):
    """Append missing hex codepoints to the given font's lvglRanges."""
    for f in proj["fonts"]:
        if f["name"] != font_name: continue
        existing = set()
        for tok in f.get("lvglRanges", "").split(","):
            tok = tok.strip()
            if not tok: continue
            if "-" in tok:  # range like 0x20-0x7f
                continue
            try:
                existing.add(int(tok, 16))
            except ValueError:
                pass
        added = []
        for cp in codepoints:
            if cp not in existing:
                added.append(f"0x{cp:04x}")
        if added:
            cur = f.get("lvglRanges", "").rstrip(",")
            if cur:
                f["lvglRanges"] = cur + "," + ",".join(added)
            else:
                f["lvglRanges"] = ",".join(added)
        return added
    return []

# ---------- Main ----------

def main():
    ts = time.strftime("%Y%m%d-%H%M%S")
    shutil.copyfile(PROJ, PROJ + ".bak." + ts)
    print(f"backup: {PROJ}.bak.{ts}")

    with open(PROJ) as f:
        proj = json.load(f)

    before_idents = all_identifiers(proj)

    # Idempotence check
    if "air_header_title" in before_idents:
        print("redesign already applied (air_header_title present) — no-op")
        return 0

    # 1. Extend fa20 font subset with icon codepoints we need
    added_fa20 = add_fa_codepoints(proj, "fa20", NEW_FA_CODEPOINTS)
    added_fa24 = add_fa_codepoints(proj, "fa24", NEW_FA_CODEPOINTS)
    print(f"fa20 added: {added_fa20}")
    print(f"fa24 added: {added_fa24}")

    # 2. Rebuild PageAir body
    page_air = find_page(proj, "PageAir")
    air_body = find_node(page_air, "air_body")
    if air_body is None:
        raise RuntimeError("air_body not found")
    air_body["children"] = build_air_body()
    print("PageAir: rebuilt air_body children")

    # 3. Rebuild PagePower body
    page_power = find_page(proj, "PagePower")
    power_body = find_node(page_power, "power_body")
    if power_body is None:
        raise RuntimeError("power_body not found")
    power_body["children"] = build_power_body()
    print("PagePower: rebuilt power_body children")

    # 4. Write back
    with open(PROJ, "w") as f:
        json.dump(proj, f, indent=2)
        f.write("\n")

    after_idents = all_identifiers(proj)
    added   = sorted(after_idents - before_idents)
    removed = sorted(before_idents - after_idents)
    print(f"\nIDENTIFIER DELTA:")
    print(f"  ADDED ({len(added)}):   {added}")
    print(f"  REMOVED ({len(removed)}): {removed}")

    # 5. Verify PRESERVE set is intact
    missing_preserved = PRESERVE - after_idents
    if missing_preserved:
        print(f"\nERROR: PRESERVED identifiers lost: {sorted(missing_preserved)}")
        print("Rolling back from backup!")
        shutil.copyfile(PROJ + ".bak." + ts, PROJ)
        return 1
    print(f"\nPreserved-identifier check: OK ({len(PRESERVE)} identifiers intact)")

    return 0

if __name__ == "__main__":
    sys.exit(main())
