#!/usr/bin/env python3
"""Raise the generated skin's text sizes to the HELIX minimums: values >= 32 px, all other text >= 24 px.

The SDK's gen_ui.py draws names at 24-26 px and values at 26-30 px. Each label's height in its bounds is raised
with the font so the text is not clipped. Usage: skin_fonts.py <Plugin Skins/TUI.json>
"""
import json, sys

MIN_VALUE, MIN_TEXT = 32.0, 24.0

def walk(node, changed):
    if isinstance(node, dict):
        name = node.get("componentData", {}).get("name") if isinstance(node.get("componentData"), dict) else None
        font = None
        cd = node.get("componentData")
        if isinstance(cd, dict):
            ts = cd.get("data", {}).get("textStyle") if isinstance(cd.get("data"), dict) else None
            if isinstance(ts, dict) and isinstance(ts.get("font"), dict):
                font = ts["font"]
        if font is not None and isinstance(font.get("height"), (int, float)):
            want = MIN_VALUE if name == "value" else MIN_TEXT
            if font["height"] < want:
                grow = want - font["height"]
                font["height"] = want
                b = node.get("bounds")
                if isinstance(b, str):
                    parts = b.split()
                    if len(parts) == 4:
                        x, y, w, h = (int(float(p)) for p in parts)
                        node["bounds"] = "%d %d %d %d" % (x, max(0, y - int(grow / 2)), w, h + int(grow + 0.5))
                changed[0] += 1
        for v in node.values():
            walk(v, changed)
    elif isinstance(node, list):
        for v in node:
            walk(v, changed)

def main():
    path = sys.argv[1]
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    changed = [0]
    walk(data, changed)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    print("skin_fonts: %d text styles raised to >= %g px (values >= %g px)" % (changed[0], MIN_TEXT, MIN_VALUE))

if __name__ == "__main__":
    main()
