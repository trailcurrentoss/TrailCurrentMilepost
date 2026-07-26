#!/usr/bin/env python3
"""Fix Air-page bar tracks that were invisible because bg_color=BgBar
resolves to #ffffff, matching BgCard (#ffffff). Switch to BgCardHover
(matches the working BarDefault style)."""
import json, shutil, time
PROJ = "/media/dave/extstorage/TrailCurrent/Product/TrailCurrentMilepost/GUI/TrailCurrentMilepost.eez-project"
ts = time.strftime("%Y%m%d-%H%M%S")
shutil.copyfile(PROJ, PROJ + ".bak." + ts)
p = json.load(open(PROJ))

BAR_IDS = {"air_temp_bar", "air_hum_bar", "air_tvoc_bar", "air_eco2_bar", "air_co_bar"}
BADGE_IDS = {"air_temp_badge", "air_hum_badge", "air_tvoc_badge", "air_eco2_badge",
             "air_co_badge", "air_overall_pill"}

def walk(n, ident_target_set, prop_setter):
    if isinstance(n, dict):
        if n.get("identifier") in ident_target_set:
            prop_setter(n)
        for v in n.values(): walk(v, ident_target_set, prop_setter)
    elif isinstance(n, list):
        for x in n: walk(x, ident_target_set, prop_setter)

def set_bar_bg(node):
    main = node["localStyles"]["definition"]["MAIN"]["DEFAULT"]
    main["bg_color"] = "BgCardHover"

def set_badge_bg(node):
    main = node["localStyles"]["definition"]["MAIN"]["DEFAULT"]
    main["bg_color"] = "BgCardHover"

fixed_bars = fixed_badges = 0
def bar_setter(n):
    global fixed_bars
    set_bar_bg(n); fixed_bars += 1
def badge_setter(n):
    global fixed_badges
    set_badge_bg(n); fixed_badges += 1

walk(p, BAR_IDS, bar_setter)
walk(p, BADGE_IDS, badge_setter)
json.dump(p, open(PROJ, "w"), indent=2)
open(PROJ, "a").write("\n")
print(f"bars fixed: {fixed_bars} | badges fixed: {fixed_badges}")
