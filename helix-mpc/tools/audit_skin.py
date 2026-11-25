#!/usr/bin/env python3
"""Audit the HELIX MPC skin: every control on every page, how it is wired, and whether anything is unreachable.

    python3 helix-mpc/tools/audit_skin.py [--skin helix-mpc/skins] [--params helix-mpc/build/params.json] [--ui helix-mpc/ui.json]

Checks (exit code 1 if any fails):
  - every parameter on the old generated pages (ui.json) is somewhere in the skin
  - every control's component exists and is one of the known mechanisms
  - every list / switch / button group has replacementStrings with exactly the parameter's number of choices
  - radio groups have one Button per choice with buttonId 0..n-1
  - every overlay a control opens exists, and every cell that opens one has a child reading its parameter
  - every control on a page is on one of that page's Q-Links
  - no Label is drawn on top of a control
  - no MenuOverlay (list pop-up) anywhere, at most 8 pages per function key and 32 in all
"""
import argparse, collections, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__)); MPC = os.path.dirname(HERE)
ap = argparse.ArgumentParser()
ap.add_argument("--skin", default=os.path.join(MPC, "skins"))
ap.add_argument("--params", default=os.path.join(MPC, "build", "params.json"))
ap.add_argument("--ui", default=os.path.join(MPC, "ui.json"))
a = ap.parse_args()

tui = json.load(open(os.path.join(a.skin, "TUI.json")))
ql = json.load(open(os.path.join(a.skin, "Q-Links.json")))
params = json.load(open(a.params))["params"]
P = {p["index"]: p for p in params}
byname = {p["name"]: p for p in params}
pd = tui["pageData"]
defs = {d["key"]: d["value"] for d in pd["componentDefinitions"]["localComponentDefinitions"]}
repl = {int(r["key"].split()[1]): r["value"] for r in pd["replacementStrings"]}
fails = []

def pidx(handle):
    m = re.search(r"Parameter (\d+)$", handle or "")
    return int(m.group(1)) if m else None

def mechanism(comp):
    v = defs[comp]
    kids = [k["componentData"] for k in v["componentsData"]]
    types = [k["type"] for k in kids]
    acts = [(x["onAction"], x["handler"], x["additionalData"]) for x in v["actions"]]
    handlers = {h for _, h, _ in acts}
    if "Knob" in types: kind = "knob (drag / Q-Link / wheel)"
    elif any(k["data"].get("sliderType") == "ColourBar" for k in kids): kind = "draw bar (drag up / down)"
    elif "Toggle Switch" in handlers and "Button" not in types: kind = "flip on touch"
    elif types.count("Button") > 1: kind = "radio buttons"
    elif "Slider" in types: kind = "list (drag sideways / wheel; list on double-tap)"
    else: kind = "other: " + ",".join(types)
    overlays = [d for _, h, d in acts if h == "Show Overlay"]
    return kind, kids, overlays

per_kind = collections.Counter()
seen_params = set()
for tab in pd["tabs"]:
    name = tab["tabName"]
    qm = next(m for m in ql["Screen Mode Q-Links"]["map"] if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == tab["fnKeySubIndex"] + 1)
    qparams = {v for v in qm["Q-Links"].values() if v >= 0}
    touch = []
    for e in tab["componentDefinition"]["componentsData"]:
        typ = e["componentData"]["type"]
        rm = {m["key"]: m["value"] for m in e["handle remapping"]["map"]}
        x, y, w, h = map(float, e["bounds"]["bounds"].split())
        if typ == "Label":
            if e["componentData"]["data"].get("type") == "Value" and rm.get("Data"):
                seen_params.add(pidx(rm["Data"]))          # a read-only display (e.g. CPU Load)
            if not e["bounds"].get("additionalInvalidatingHandles"):
                for nm, (X, Y, W, H) in touch:
                    if x < X + W and X < x + w and y < Y + H and Y < y + h:
                        fails.append("%s: label over %s" % (name, nm))
            continue
        if typ in ("Image", "Focus"):
            continue
        if typ == "Envelope":
            per_kind["envelope display (drag points)"] += 1
            for k in ("attackHandle", "decayHandle", "sustainHandle", "releaseHandle"):
                seen_params.add(pidx(e["componentData"]["data"][k]))
            touch.append((e["componentData"]["name"], (x, y, w, h)))
            continue
        if typ not in defs:
            fails.append("%s: %s uses unknown component %s" % (name, e["componentData"]["name"], typ)); continue
        pi = pidx(rm.get("Data"))
        seen_params.add(pi)
        kind, kids, overlays = mechanism(typ)
        per_kind[kind] += 1
        touch.append((e["componentData"]["name"], (x, y, w, h)))
        p = P[pi]
        discrete = p["kind"] in ("bool", "choice") and p["name"] not in ("A Table", "B Table")
        if kind in ("flip on touch", "radio buttons", "list (drag sideways / wheel; list on double-tap)") and discrete:
            if pi not in repl or len(repl[pi]) != len(p["choices"]):
                fails.append("%s: %s has no / wrong replacementStrings" % (name, p["name"]))
        if kind == "flip on touch" and len(p["choices"]) != 2 and not e["componentData"]["name"].startswith("tag"):
            fails.append("%s: %s flips but has %d choices" % (name, p["name"], len(p["choices"])))
        if kind == "radio buttons" or (e["componentData"]["name"].startswith("bar sel") and "Button" in [k["type"] for k in kids]):
            if e["componentData"]["name"].startswith("bar sel"):
                b = [k for k in kids if k["type"] == "Button"]
                if len(b) != 1 or b[0]["data"]["numButtonsInGroup"] != len(p["choices"]) or b[0]["data"].get("gestureBehaviour") != "Instant":
                    fails.append("%s: %s step-select button miswired" % (name, e["componentData"]["name"]))
                continue
            ids = sorted(k["data"]["buttonId"] for k in kids if k["type"] == "Button")
            nb = {k["data"]["numButtonsInGroup"] for k in kids if k["type"] == "Button"}
            if ids != list(range(len(p["choices"]))) or nb != {len(p["choices"])}:
                fails.append("%s: %s radio group ids %s for %d choices" % (name, p["name"], ids, len(p["choices"])))
            if any(k["data"].get("gestureBehaviour") != "Instant" for k in kids if k["type"] == "Button"):
                fails.append("%s: %s radio buttons not Instant" % (name, p["name"]))
        for ov in overlays:
            if ov not in defs:
                fails.append("%s: %s opens missing overlay %s" % (name, p["name"], ov))
            if "Menu" in json.dumps(defs.get(ov, {})) and not discrete:
                fails.append("%s: %s opens a list but is not a choice parameter" % (name, p["name"]))
        if overlays and not any(k["type"] != "Focus" and k["data"].get("handleName") == "Data" for k in kids):
            fails.append("%s: %s opens an overlay but nothing in the cell reads it" % (name, p["name"]))
        # draw bars (performer steps) are on Q-Links on the P STEP pages, not on the draw page
        if pi not in qparams and not e["componentData"]["name"].startswith(("tag", "bar ")):
            fails.append("%s: %s is not on this page's Q-Links" % (name, p["name"]))

# The MPC firmware can open MenuOverlay with empty entries: no list pop-ups anywhere.
if "MenuOverlay" in json.dumps(defs):
    fails.append("a MenuOverlay is defined (the firmware can show it empty); use pickers or knobs")
if len(pd["tabs"]) > 32:
    fails.append("%d pages (the MPC allows 32)" % len(pd["tabs"]))
subs = collections.Counter(t["fnKeyIndex"] for t in pd["tabs"])
for k, n in subs.items():
    if n > 8:
        fails.append("function key %d has %d pages (the MPC allows 8)" % (k, n))

old = json.load(open(a.ui))
# Reached through an editing proxy (the app mirrors the chosen step): P1/P2 Shape n via P1/P2 Edit Step + Curve.
PROXIED = {"P%d Shape %d" % (q, i): "P%d Curve" % q for q in (1, 2) for i in range(1, 17)}
PROXIED.update({"F1 Type": "F1 Filter", "F2 Type": "F2 Filter"})
missing = []
for pg in old["pages"]:
    for c in pg["controls"]:
        nm = c if isinstance(c, str) else c.get("param")
        if nm in PROXIED and byname[PROXIED[nm]]["index"] in seen_params:
            continue
        if nm and nm != "empty" and nm in byname and byname[nm]["index"] not in seen_params:
            missing.append("%s (was on %s)" % (nm, pg["name"]))
if missing:
    fails.append("not reachable in the new skin: " + ", ".join(missing))

print("HELIX MPC skin audit: %d pages, %d controls" % (len(pd["tabs"]), sum(per_kind.values())))
for k, n in per_kind.most_common():
    print("  %4d  %s" % (n, k))
print("  parameters reachable: %d" % len([s for s in seen_params if s is not None]))
if fails:
    print("FAIL (%d):" % len(fails)); [print("  " + f) for f in fails]; sys.exit(1)
print("OK: every old control reachable, every control wired, lists complete, no labels over controls, all on Q-Links")
