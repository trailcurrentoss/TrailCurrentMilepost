#!/usr/bin/env python3
"""
Post-redesign fixes:
  1. Populate embeddedFontFile for fa20 and fa24 with base64 of the source
     OTF so the newly-added FA codepoints (thermometer 0xf2c7, wind 0xf72e,
     shield 0xf132) actually render on Ctrl+B. Per eezstudio skill Trap 18:
     empty embeddedFontFile => no ui_font_*.c => missing glyphs on device.
  2. Replace "ECO₂" (U+2082 subscript-2) with "ECO2" in the air_eco2 card
     label — U+2082 isn't in Roboto Medium 11's subset so it renders as [].
     Per eezstudio skill Trap 11.

Idempotent.
"""
import base64, json, os, shutil, sys, time

PROJ = "/media/dave/extstorage/TrailCurrent/Product/TrailCurrentMilepost/GUI/TrailCurrentMilepost.eez-project"

FONTS_TO_EMBED = ("fa20", "fa24")

def read_font_bytes(gui_dir, rel_path):
    # Try both (a) the naive gui_dir + rel_path and (b) GUI/ASSETS/basename —
    # the on-disk asset lives at GUI/ASSETS/ even though the JSON says
    # "../ASSETS/" (historical layout inconsistency).
    candidates = [
        os.path.join(gui_dir, rel_path),
        os.path.join(gui_dir, "ASSETS", os.path.basename(rel_path)),
    ]
    for c in candidates:
        if os.path.exists(c):
            return open(c, "rb").read()
    raise FileNotFoundError(f"font source not found; tried: {candidates}")

def main():
    ts = time.strftime("%Y%m%d-%H%M%S")
    shutil.copyfile(PROJ, PROJ + ".bak." + ts)
    print(f"backup: {PROJ}.bak.{ts}")

    with open(PROJ) as f:
        proj = json.load(f)

    gui_dir = os.path.dirname(PROJ)

    # 1. Populate embeddedFontFile
    changed = 0
    for f in proj["fonts"]:
        if f["name"] not in FONTS_TO_EMBED:
            continue
        # Always re-populate — the base64 must correspond to the CURRENT
        # source OTF, not whatever old snapshot happened to be embedded.
        raw = read_font_bytes(gui_dir, f["source"]["filePath"])
        f["embeddedFontFile"] = base64.b64encode(raw).decode("ascii")
        changed += 1
        print(f"  embedded {f['name']}  ({len(raw)} bytes source -> "
              f"{len(f['embeddedFontFile'])} bytes base64)")

    # 2. Replace ECO subscript-2 with ASCII 2
    def walk(node):
        n = 0
        if isinstance(node, dict):
            t = node.get("text")
            if isinstance(t, str) and "ECO₂" in t:
                node["text"] = t.replace("ECO₂", "ECO2")
                n += 1
            for v in node.values():
                n += walk(v)
        elif isinstance(node, list):
            for x in node:
                n += walk(x)
        return n

    eco2_fixes = walk(proj)
    print(f"replaced 'ECO\\u2082' -> 'ECO2' in {eco2_fixes} label(s)")

    with open(PROJ, "w") as f:
        json.dump(proj, f, indent=2)
        f.write("\n")

    print(f"\nDone. Font embed changes: {changed}, glyph fixes: {eco2_fixes}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
