#!/usr/bin/env python3
"""HELIX "Concrete Rack" MPC skin generator.

Writes a hand-written MPC Plugin Skins folder (TUI.json, Q-Links.json and every PNG it uses) into helix-mpc/skins/.
The SDK's build.py copies that folder verbatim to "Plugin Skins/" and skips its own ui.json generator.

Design: light concrete ground, every Q-Link row of four is one full-width strip with a thick black border and an
orange tag on the left (the tag is the section's on/off button where the section has one). Waveform boxes are
film strips that follow a parameter (shape, type, wavetable position, unison count); envelopes use the MPC's own
Envelope widget. Text: Roboto, names 24 px, values 32 px, dark on light everywhere.

    python3 helix-mpc/tools/make_skin.py [--params helix-mpc/build/params.json] [--out helix-mpc/skins]
                                         [--preview <dir>]   (also renders an approximate PNG of every page)

params.json comes from the SDK build (host_sim dump). Parameters are looked up by name, so the skin follows the
plugin's parameter order automatically. Needs Pillow.
"""
import argparse, json, math, os, random, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
MPC = os.path.dirname(HERE)

# ---------------------------------------------------------------- palette (Massive): navy panels in light grey frames
GROUND = (178, 182, 188)      # between the panels
STRIP = (34, 37, 56)          # panel body (navy)
FRAME = (206, 209, 214)       # panel frame and the section tag (light grey)
INK = (24, 27, 42)            # dark text / lines on light parts
LINE = (236, 239, 246)        # light text / traces on dark parts
TEXT = "ffffffff"             # values on the panels
MUTED = "ffc6ccda"            # names on the panels
TAG_TEXT = "ff181b2a"         # captions on the light tags
ORANGE = (232, 236, 244)      # lit button / switch (light, like Massive's selected boxes)
LED_ON = (96, 214, 244)       # section on: cyan LED
LED_OFF = (58, 62, 82)
ACCENT = (70, 172, 236)       # knob arcs, focus frame (blue)
TRACK = (66, 71, 98)
KNOB_BODY = (236, 238, 242)   # white knob caps
SCREEN = (12, 14, 26)         # display boxes (near black)
GRID = (38, 43, 66)
OFF = (150, 154, 162)         # section tag when the section is off
BTN_OFF = (24, 27, 44)        # unlit button (dark)
BTN_EDGE = (150, 156, 176)

def is_light(rgb):
    return sum(rgb[:3]) > 380

def argb(rgb, a=255):
    return "%02x%02x%02x%02x" % (a, rgb[0], rgb[1], rgb[2])

# ---------------------------------------------------------------- geometry (tab coordinates, 1280 x 628)
PAD, STRIP_H, STRIP_GAP, BORDER = 10, 146, 8, 3
TAG_W = 150
CONTENT_X0 = PAD + BORDER + TAG_W + BORDER + 22
CONTENT_X1 = 1280 - PAD - BORDER - 20
KNOB, KNOB_S = 88, 72
FRAMES = 128

def strip_y(r):
    return PAD + r * (STRIP_H + STRIP_GAP)

# ---------------------------------------------------------------- JSON with ordered (and duplicate) keys
class O(list):
    """An ordered JSON object; a list of (key, value) pairs so duplicate keys survive."""

def o(*pairs):
    return O(pairs)

def dump(v, ind=0):
    sp = " " * ind
    if isinstance(v, O):
        if not v:
            return "{}"
        inner = ",\n".join("%s %s: %s" % (sp, json.dumps(k), dump(x, ind + 1)) for k, x in v)
        return "{\n" + inner + "\n" + sp + "}"
    if isinstance(v, list):
        if not v:
            return "[]"
        if all(not isinstance(x, (list, O)) for x in v):
            return "[" + ", ".join(json.dumps(x) for x in v) + "]"
        return "[\n" + ",\n".join(sp + " " + dump(x, ind + 1) for x in v) + "\n" + sp + "]"
    if isinstance(v, float):
        return repr(round(v, 3))
    return json.dumps(v)

def img(colour="0", image=""):
    return o(("version", 1), ("colour", colour), ("image", image))

def bg(colour="0", image=""):
    return o(("version", 1), ("focussed", img(colour, image)), ("unfocussed", img(colour, image)))

def act(on, handler, handle, extra=""):
    return o(("version", 1), ("onAction", on), ("handler", handler), ("handleName", handle), ("additionalData", extra))

def bounds(rect, focus="No", invalid="Show", when="Always", prop=False, inval=None):
    r = " ".join(("%g" % v) for v in rect)
    pairs = [("version", 2 if inval else 1), ("acceptsHWFocus", focus), ("showWhenDataModelInvalid", invalid),
             ("whenVisible", when), ("boundsType", "Proportional" if prop else "Absolute"), ("bounds", r)]
    if inval:
        pairs.append(("additionalInvalidatingHandles", inval))
    return o(*pairs)

def entry(name, typ, data, b, remap=()):
    return o(("version", 2),
             ("componentData", o(("version", 1), ("name", name), ("type", typ), ("data", data))),
             ("handle remapping", o(("version", 1), ("map", [o(("key", k), ("value", v)) for k, v in remap]))),
             ("bounds", b))

def component(children, actions=(), colour="0", image="", ignore=False):
    return o(("version", 2), ("actions", list(actions)), ("backgroundData", bg(colour, image)),
             ("ignoreMousePresses", ignore), ("disableCoarseDataWheel", False), ("componentsData", children))

def font(h, style="Bold"):
    return o(("version", 1), ("name", "Roboto"), ("style", style), ("height", float(h)))

def tstyle(h, style="Bold", colour=TEXT, just="left verticallyCentred"):
    return o(("version", 1), ("font", font(h, style)), ("colour", colour), ("justification", just), ("case", "Original"))

def label(kind, h, style="Bold", colour=TEXT, just="left verticallyCentred", text=None, handle="Data"):
    pairs = [("version", 1), ("textStyle", tstyle(h, style, colour, just)), ("type", kind), ("handleName", handle)]
    if kind == "Text":
        pairs.append(("text", text))
    return o(*pairs)

FOCUS = o(("version", 1), ("backgroundColour", "2446ace6"), ("outlineColour", argb(ACCENT)),
          ("backgroundInset", 0.0), ("outlineThickness", 4.0))

FM = 6   # margin round a button group / screen so its focus frame, drawn underneath, shows round the edge

def focus_entry():
    return entry("focus", "Focus", FOCUS, bounds((0, 0, 1, 1), when="WhenFocussed", prop=True))

FOCUS_LOST = [act("Focus Lost", "Focus Lost", "Focus Lost")]

def knob_overlay(bipolar):
    return o(("version", 2), ("version", 1), ("actions", FOCUS_LOST), ("overlayType", "Knob With Header"),
             ("scaleType", "Linear"), ("polarityType", "Bipolar" if bipolar else "Unipolar"),
             ("showMinusSign", True), ("handleName", "Data"))

def menu_overlay():
    return o(("version", 1), ("version", 1), ("actions", FOCUS_LOST), ("withNumbers", False), ("handleName", "Data"))

def overlay_component(typ, data):
    return component([entry("overlay", typ, data, bounds((0, 0, 1280, 800)))], colour=argb(SCREEN), ignore=True)

def actions_for(overlay):
    return [act("Mouse Down", "Q-Link", "Data"), act("Double Click", "Show Overlay", "Data", overlay),
            act("Enter Pressed", "Show Overlay", "Data", overlay)]

OVERLAYS = {"knob": "hx knob overlay", "bi": "hx knob overlay bi"}

# ---------------------------------------------------------------- artwork
FONT_FILE = None
for cand in ("/usr/share/fonts/truetype/google-fonts/ofl/roboto/Roboto[wdth,wght].ttf",
             "/usr/share/fonts/truetype/roboto/Roboto-Bold.ttf"):
    if os.path.exists(cand):
        FONT_FILE = cand
        break

def pil_font(size, weight=700):
    if not FONT_FILE:
        return ImageFont.load_default()
    f = ImageFont.truetype(FONT_FILE, size)
    try:
        f.set_variation_by_axes([weight, 100])
    except Exception:
        pass
    return f

SS = 4  # supersampling for antialiased artwork

def aa_canvas(w, h, fill=(0, 0, 0, 0)):
    im = Image.new("RGBA", (w * SS, h * SS), fill)
    return im, ImageDraw.Draw(im)

def finish(im, w, h):
    return im.resize((w, h), Image.LANCZOS)

def arc_pts(cx, cy, r, a0, a1, n=64):
    pts = []
    for i in range(n + 1):
        a = math.radians(a0 + (a1 - a0) * i / n - 90)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts

def thick_line(d, pts, width, colour):
    d.line(pts, fill=colour, width=int(width), joint="curve")
    r = width / 2
    for x, y in (pts[0], pts[-1]):
        d.ellipse((x - r, y - r, x + r, y + r), fill=colour)

def knob_frame(size, frac, bipolar):
    im, d = aa_canvas(size, size)
    s = SS
    st = 8 * s if size >= 80 else 7 * s
    c = size * s / 2
    r = c - st / 2 - s
    thick_line(d, arc_pts(c, c, r, -135, 135), st, TRACK + (255,))
    ang = -135 + 270 * frac
    a0, a1 = (0, ang) if bipolar else (-135, ang)
    if a0 > a1:
        a0, a1 = a1, a0
    if a1 - a0 > 0.5:
        thick_line(d, arc_pts(c, c, r, a0, a1), st, ACCENT + (255,))
    ir = r - st - 4 * s
    d.ellipse((c - ir, c - ir, c + ir, c + ir), fill=KNOB_BODY + (255,))
    t = math.radians(ang - 90)
    p0 = (c + ir * 0.22 * math.cos(t), c + ir * 0.22 * math.sin(t))
    p1 = (c + ir * 0.82 * math.cos(t), c + ir * 0.82 * math.sin(t))
    thick_line(d, [p0, p1], 4.5 * s, INK + (255,))
    return finish(im, size, size)

def film(frames):
    w, h = frames[0].size
    out = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        out.paste(f, (0, i * h))
    return out

# ---- waveform drawing inside a screen box. A shape is f(u) -> v, u in 0..1, v in -1..1 (None = pen up)
def screen_frame(w, h, paths, label_space=True, frame=True):
    """paths: list of (points [(u,v)], width_px, alpha 0..1). Coordinates normalised to the drawing area."""
    im, d = aa_canvas(w, h, SCREEN + (255,))
    s = SS
    top, bot = 12 * s, (h - (42 if label_space else 12)) * s
    l, rgt = 12 * s, (w - 12) * s
    for i in range(1, 4):
        y = top + (bot - top) * i / 4
        d.line([(8 * s, y), ((w - 8) * s, y)], fill=GRID + (255,), width=s)
    for i in range(1, 6):
        x = w * s * i / 6
        d.line([(x, 8 * s), (x, bot)], fill=GRID + (255,), width=s)
    X = lambda u: l + (rgt - l) * u
    Y = lambda v: (top + bot) / 2 - (bot - top) / 2 * 0.84 * v
    for pts, width, alpha in paths:
        col = LINE + (int(255 * alpha),)
        seg = []
        for p in pts + [None]:
            if p is None:
                if len(seg) > 1:
                    d.line([(X(u), Y(v)) for u, v in seg], fill=col, width=int(width * s), joint="curve")
                seg = []
            else:
                seg.append(p)
    if frame:
        d.rectangle((0, 0, w * s - 1, h * s - 1), outline=BTN_EDGE + (255,), width=3 * s)
    return finish(im, w, h)

def sample(f, n=160, u0=0.0, u1=1.0):
    return [(u0 + (u1 - u0) * i / n, max(-1.0, min(1.0, f(u0 + (u1 - u0) * i / n)))) for i in range(n + 1)]

def cyc(u, k=2):
    return (u * k) % 1.0

def saw(q): return 1 - 2 * q
def tri(q): return 4 * q - 1 if q < 0.5 else 3 - 4 * q
def sq(q, duty=0.5): return 1.0 if q < duty else -1.0

def steps(pts_xy):
    out = []
    for i, (u, v) in enumerate(pts_xy):
        if i:
            out.append((u, pts_xy[i - 1][1]))
        out.append((u, v))
    return out

def hard(f, n=400):
    """Sample with enough points that jumps look vertical."""
    return sample(f, n)

WARP = {
    "Off": lambda u: saw(cyc(u)),
    "Bend +": lambda u: saw(cyc(u) ** 0.45),
    "Bend -": lambda u: saw(cyc(u) ** 2.2),
    "Asym": lambda u: (lambda q: 1 - 2 * q / 0.3 if q < 0.3 else -1 + 2 * (q - 0.3) / 0.7)(cyc(u)),
    "PWM": lambda u: sq(cyc(u), 0.28),
    "Mirror": lambda u: tri(cyc(u)),
    "Sync": lambda u: saw((cyc(u) * 1.8) % 1.0),
    "Formant": lambda u: math.sin(2 * math.pi * cyc(u) * 3.5) * (1 - cyc(u)),
    "FM Other": lambda u: math.sin(2 * math.pi * cyc(u) + 2.2 * math.sin(4 * math.pi * cyc(u))),
    "FM Sub": lambda u: math.sin(2 * math.pi * cyc(u) + 1.6 * math.sin(math.pi * cyc(u))),
    "FM Noise": lambda u: math.sin(2 * math.pi * cyc(u) + 0.6 * math.sin(u * 177.0) + 0.4 * math.sin(u * 313.0)),
    "AM": lambda u: saw(cyc(u)) * (0.55 + 0.45 * math.sin(2 * math.pi * cyc(u) * 4)),
    "RM": lambda u: saw(cyc(u)) * math.sin(2 * math.pi * cyc(u) * 3),
    "Quantize": lambda u: round(saw(cyc(u)) * 3) / 3,
    "Bit Red": lambda u: round(saw(cyc(u)) * 1.5) / 1.5,
}

def noise_series(kind, n=240, seed=7):
    rnd = random.Random(seed)
    v, out = 0.0, []
    b = [0.0] * 3
    for i in range(n + 1):
        w = rnd.uniform(-1, 1)
        if kind == "White":
            x = w * 0.9
        elif kind == "Pink":
            b[0] = 0.9 * b[0] + w * 0.5; b[1] = 0.6 * b[1] + w * 0.4; x = (b[0] + b[1]) * 0.8
        elif kind == "Brown":
            v = max(-0.9, min(0.9, v + w * 0.18)); x = v
        elif kind == "Vinyl":
            x = w * 0.18 + (0.85 * rnd.choice((-1, 1)) if rnd.random() < 0.03 else 0)
        elif kind == "Tape Hiss":
            x = w * 0.32
        else:  # Crackle
            x = (rnd.uniform(0.5, 0.95) * rnd.choice((-1, 1))) if rnd.random() < 0.05 else w * 0.05
        out.append((i / n, max(-1, min(1, x))))
    return out

def filter_curve(name):
    fc = 0.5
    def lp(u, order, res=0.35):
        x = u / fc
        g = 1 / math.sqrt(1 + x ** (2 * order))
        return g * (1 + res * math.exp(-((u - fc) / 0.04) ** 2) * 2.2)
    def bp(u, order):
        q = (u - fc) / (0.16 if order == 1 else 0.1)
        return math.exp(-q * q)
    def to_v(g):
        return -0.95 + 1.7 * min(1.15, g) / 1.15
    if name.startswith("LP"):
        o_ = 2 if "24" in name else 1
        return sample(lambda u: to_v(lp(u, o_)))
    if name.startswith("HP"):
        o_ = 2 if "24" in name else 1
        return sample(lambda u: to_v(lp(1 - u, o_)))
    if name.startswith("BP"):
        return sample(lambda u: to_v(bp(u, 1 if "12" in name else 2)))
    if name.startswith("Notch"):
        k = 0.06 if "12" in name else 0.035
        return sample(lambda u: to_v(1 - math.exp(-((u - fc) / k) ** 2)))
    if name.startswith("Ladder"):
        return sample(lambda u: to_v(lp(u, 2, 0.6)))
    if name == "Comb +":
        return sample(lambda u: to_v(0.55 + 0.45 * math.cos(2 * math.pi * u * 6)))
    if name == "Comb -":
        return sample(lambda u: to_v(0.55 - 0.45 * math.cos(2 * math.pi * u * 6)))
    if name == "Formant":
        return sample(lambda u: to_v(0.15 + 0.9 * math.exp(-((u - 0.22) / 0.05) ** 2) + 0.7 * math.exp(-((u - 0.5) / 0.05) ** 2)
                                     + 0.45 * math.exp(-((u - 0.78) / 0.05) ** 2)))
    if name == "Phaser":
        return sample(lambda u: to_v(0.85 - 0.75 * math.exp(-((u - 0.35) / 0.03) ** 2) - 0.75 * math.exp(-((u - 0.65) / 0.03) ** 2)))
    return sample(lambda u: 0)

def lfo_shape(name):
    if name == "Sine": return sample(lambda u: 0.9 * math.sin(2 * math.pi * u * 2))
    if name == "Triangle": return sample(lambda u: 0.9 * tri(cyc(u)))
    if name == "Saw Up": return hard(lambda u: -0.9 * saw(cyc(u)))
    if name == "Saw Down": return hard(lambda u: 0.9 * saw(cyc(u)))
    if name == "Square": return hard(lambda u: 0.9 * sq(cyc(u)))
    vals = [0.6, -0.3, 0.9, -0.8, 0.1, 0.5, -0.6, -0.1]
    if name == "S&H":
        return steps([(i / 8, vals[i]) for i in range(8)] + [(1.0, vals[-1])])
    if name == "Smooth Rnd":
        return sample(lambda u: sum(vals[i] * math.exp(-((u * 7 - i) ** 2) / 0.5) for i in range(8)) * 0.9)
    if name == "Breakpoint":
        return [(0, -0.8), (0.15, 0.9), (0.35, 0.2), (0.55, 0.6), (0.8, -0.5), (1.0, -0.8)]
    return steps([(i / 8, [-0.9, -0.4, 0.1, 0.6, 0.9, 0.3, -0.2, -0.6][i]) for i in range(8)] + [(1.0, -0.6)])

def dist_curve(name):
    axes = [([(0.5, -1), (0.5, 1)], 1, 0.35), ([(0, 0), (1, 0)], 1, 0.35)]
    x = lambda u: 2 * u - 1
    if name == "Tube": f = lambda u: math.tanh(3 * x(u)) / math.tanh(3)
    elif name == "Hard Clip": f = lambda u: max(-0.7, min(0.7, 1.6 * x(u))) / 0.7
    elif name == "Fold": f = lambda u: math.sin(2.6 * x(u))
    elif name == "Crush": return axes + [(steps([(i / 8, -0.875 + i * 0.25) for i in range(8)] + [(1.0, 0.875)]), 3.5, 1)]
    else: f = lambda u: (math.tanh(3 * x(u)) / math.tanh(3)) if x(u) > 0 else 0.45 * x(u)
    return axes + [(sample(f), 3.5, 1)]

def unison_paths(n):
    paths = []
    det = [0.0] if n == 1 else [(-1 + 2 * i / (n - 1)) * 0.07 for i in range(n)]
    mid = (n - 1) // 2
    for i, dt in enumerate(det):
        paths.append((hard(lambda u, dt=dt: 0.85 * saw(((u * 2 * (1 + dt)) + dt * 3) % 1.0)), 3.2 if i == mid else 1.7,
                      1.0 if i == mid else 0.45))
    return paths

def wtpos_paths(frac):
    """A generic stack of morphing frames (saw -> rounded square) with the current position drawn bold."""
    paths, N = [], 7
    cur = frac * (N - 1)
    for i in range(N):
        m = i / (N - 1)
        back = N - 1 - i          # frame 0 (start of the table) at the back, upper right
        dx, dy = back * 0.045, back * 0.16
        f = lambda u, m=m: (1 - m) * saw(u) * 0.95 + m * math.tanh(3 * math.sin(2 * math.pi * u)) / math.tanh(3)
        pts = [(dx + 0.73 * u, 0.36 * v - 0.5 + dy) for u, v in sample(f, 140)]
        near = max(0.0, 1 - abs(i - cur))
        paths.append((pts, 1.3 + 2.4 * near, 0.25 + 0.75 * near))
    return paths

def delay_paths(mode):
    out = []
    for i, (u, hgt) in enumerate([(0.06, 1), (0.25, 0.74), (0.44, 0.55), (0.63, 0.4), (0.82, 0.28)]):
        if mode == "Ping-Pong":
            sgn = 1 if i % 2 == 0 else -1
            out.append(([(u, 0), (u, sgn * hgt * 0.95)], 7, 1.0 if i == 0 else 0.8))
        else:
            out.append(([(u, -0.95), (u, -0.95 + 1.9 * hgt * 0.95)], 7, 1.0 if i == 0 else 0.8))
    if mode == "Ping-Pong":
        out.append(([(0, 0), (1, 0)], 1, 0.4))
    return out

def reverb_paths(kind):
    k = 2.2 if kind == "Hall" else 4.2
    rnd = random.Random(3)
    pts = []
    for i in range(241):
        u = i / 240
        env = math.exp(-k * u)
        pts.append((u, (-0.95 if u < 0.03 else -0.95 + 1.85 * env * rnd.uniform(0.55, 1.0))))
    return [(pts, 1.6, 1)]

def chorus_paths(kind):
    n = 2 if kind == "Chorus" else 3
    return [(sample(lambda u, k=k: 0.85 * math.sin(2 * math.pi * (u * 2 * (1 + 0.05 * k) + 0.06 * k))), 3.2 if k == 0 else 1.8,
             1.0 if k == 0 else 0.5) for k in range(n)]

def ott_paths():
    out = []
    for i, (lo, hi) in enumerate([(0.55, 0.8), (0.45, 0.7), (0.6, 0.85)]):
        x0 = 0.06 + i * 0.32
        out.append(([(x0, -0.95), (x0, -0.95 + 1.9 * lo), (x0 + 0.24, -0.95 + 1.9 * lo), (x0 + 0.24, -0.95)], 3.5, 1))
        out.append(([(x0, -0.95 + 1.9 * hi), (x0 + 0.24, -0.95 + 1.9 * hi)], 3, 0.55))
    return out

SUB = {"Sine": sample(lambda u: 0.85 * math.sin(2 * math.pi * u * 2)), "Triangle": sample(lambda u: 0.85 * tri(cyc(u))),
       "Square": hard(lambda u: 0.8 * sq(cyc(u))), "Saw": hard(lambda u: 0.85 * saw(cyc(u)))}

def screen_film(kind, choices, w, h):
    """Returns a list of frames for a screen bound to a parameter of this kind."""
    if kind == "warp":
        return [screen_frame(w, h, [(hard(WARP.get(c, WARP["Off"])), 3.2, 1)]) for c in choices]
    if kind == "sub":
        return [screen_frame(w, h, [(SUB.get(c, SUB["Sine"]), 3.2, 1)]) for c in choices]
    if kind == "noise":
        return [screen_frame(w, h, [(noise_series(c), 1.6, 1)]) for c in choices]
    if kind == "filter":
        return [screen_frame(w, h, [(filter_curve(c), 3.5, 1)]) for c in choices]
    if kind == "lfo":
        return [screen_frame(w, h, [(lfo_shape(c), 3.2, 1)]) for c in choices]
    if kind == "dist":
        return [screen_frame(w, h, dist_curve(c)) for c in choices]
    if kind == "delay":
        return [screen_frame(w, h, delay_paths(c)) for c in choices]
    if kind == "reverb":
        return [screen_frame(w, h, reverb_paths(c)) for c in choices]
    if kind == "chorus":
        return [screen_frame(w, h, chorus_paths(c)) for c in choices]
    if kind == "unison":
        return [screen_frame(w, h, unison_paths(n)) for n in range(1, 8)]
    if kind == "wtpos":
        return [screen_frame(w, h, wtpos_paths(i / 32)) for i in range(33)]
    if kind == "ott":
        return [screen_frame(w, h, ott_paths())]
    raise ValueError(kind)

def box_image(w, h, fill, chevron=False):
    im, d = aa_canvas(w, h, fill + (255,))
    s = SS
    d.rectangle((0, 0, w * s - 1, h * s - 1), outline=(INK if is_light(fill) else BTN_EDGE) + (255,), width=3 * s)
    if chevron:
        cx, cy = (w - 22) * s, h * s / 2
        d.polygon([(cx - 8 * s, cy - 4 * s), (cx + 8 * s, cy - 4 * s), (cx, cy + 6 * s)], fill=(INK if is_light(fill) else LINE) + (255,))
    return finish(im, w, h)

def slug(t):
    """File-name form of a label. Symbols are spelled out: dropping them made "Bend +" and "Bend -" (and "Comb +" /
    "Comb -") share one image file, so both buttons showed "Bend +" (0.2.7)."""
    for a, b in (("+", "plus"), ("-", "minus"), ("/", "of"), ("&", "and"), ("%", "pct"), (">", "to"), ("<", "from")):
        t = t.replace(a, " %s " % b)
    return "_".join("".join(ch if ch.isalnum() else " " for ch in t).split()).lower() or "x"

def draw_text(im, text, cx, cy, size, weight=700, colour=INK):
    """Text drawn into the picture itself. Labels placed over a touch area take the touch on the MPC, so anything
    written on something you press (tags, switches, segment buttons) is part of its image instead."""
    d = ImageDraw.Draw(im)
    d.text((cx, cy), text, font=pil_font(size, weight), fill=colour, anchor="mm")

def tag_frame(lines, state, on):
    """Section tag in Massive's style: light grey (darker when off), dark title, a round LED (cyan = on)."""
    h = STRIP_H - 2 * BORDER
    im = Image.new("RGB", (TAG_W, h), FRAME if on else OFF)
    d = ImageDraw.Draw(im)
    ty = h / 2 - 17 * len(lines) - 14
    for i, line in enumerate(lines):
        draw_text(im, line, TAG_W / 2 + 10, ty + i * 34 + 17, 28, 700, INK)
    r = 9
    cx, cy = 16, ty + 17
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=LED_ON if on else LED_OFF, outline=INK, width=2)
    draw_text(im, state, TAG_W / 2, ty + len(lines) * 34 + 6 + 15, 24, 600, INK)
    return im

def labelled_box(w, h, fill, text, size=28):
    im = box_image(w, h, fill).convert("RGB")
    draw_text(im, text, w / 2, h / 2, size, 700, INK if is_light(fill) else LINE)
    return im

def icon_box(w, h, fill, name):
    """A picker button showing a step shape as a small curve (performer step shapes)."""
    im, d = aa_canvas(w, h, fill + (255,))
    s = SS
    d.rectangle((0, 0, w * s - 1, h * s - 1), outline=(INK if is_light(fill) else BTN_EDGE) + (255,), width=3 * s)
    l, r_, t, b = 14 * s, (w - 14) * s, 14 * s, (h - 14) * s
    X = lambda u: l + (r_ - l) * u
    Y = lambda v: b - (b - t) * v
    f = {"Hold": lambda u: 0.75, "Ramp Up": lambda u: u, "Ramp Down": lambda u: 1 - u,
         "Smooth": lambda u: 0.5 - 0.5 * math.cos(math.pi * u), "Triangle": lambda u: 1 - abs(2 * u - 1),
         "Exp": lambda u: u ** 3}.get(name, lambda u: 0.5)
    pts = [(X(i / 40), Y(f(i / 40))) for i in range(41)]
    d.line(pts, fill=(INK if is_light(fill) else LINE) + (255,), width=int(4 * s), joint="curve")
    return finish(im, w, h).convert("RGB")

def background(page):
    im = Image.new("RGB", (1280, 628), GROUND)
    d = ImageDraw.Draw(im)
    for r, st in enumerate(page["strips"]):
        y = strip_y(r)
        d.rectangle((PAD, y, 1280 - PAD - 1, y + STRIP_H - 1), fill=STRIP, outline=FRAME, width=BORDER)
        tx0 = PAD + BORDER
        d.rectangle((tx0, y + BORDER, tx0 + TAG_W - 1, y + STRIP_H - BORDER - 1), fill=FRAME)
        d.rectangle((tx0 + TAG_W, y + BORDER, tx0 + TAG_W + BORDER - 1, y + STRIP_H - BORDER - 1), fill=GROUND)
        for c in st["cells"]:
            if c["type"] == "env":
                x, yy, w, h = c["rect"]
                d.rectangle((x, yy, x + w - 1, yy + h - 1), fill=SCREEN, outline=BTN_EDGE, width=3)
    return im

# ---------------------------------------------------------------- page description
def K(param, name, bi=False, menu=False, vis=None, w=250):
    return {"type": "knob", "param": param, "name": name, "bi": bi, "menu": menu, "vis": vis, "w": w}
def KS(param, name, bi=False):
    return {"type": "knobS", "param": param, "name": name, "bi": bi, "menu": False, "vis": None}
def SCR(param, kind, w=220, overlay="menu", pos=None, vis=None):
    return {"type": "screen", "param": param, "kind": kind, "w": w, "overlay": overlay, "pos": pos, "vis": vis}
def PIC(kind, w=240):
    return {"type": "picture", "kind": kind, "w": w}
def CH(param, name, w=250):
    return {"type": "choice", "param": param, "name": name, "w": w}
def TG(param, name, w=190):
    return {"type": "toggle", "param": param, "name": name, "w": w}
def SEG(param, name, labels, widths=None):
    return {"type": "seg", "param": param, "name": name, "labels": labels, "widths": widths}
def ENV(a, d, s, r, w=196):
    return {"type": "env", "params": (a, d, s, r), "w": w}
def VAL(param, name, w=230):
    return {"type": "readout", "param": param, "name": name, "w": w}
def PICK(param, caption=None, cols=None, labels=None, icons=False, where="top", bw=None, cap_w=None):
    """Tap-to-choose buttons for a choice parameter (the SDK's radio group: one Button per choice, gestureBehaviour
    Instant). Replaces list pop-ups, which the MPC firmware can open empty. caption: None, or shown "top"/"left"."""
    return {"type": "pick", "param": param, "caption": caption, "cols": cols, "labels": labels, "icons": icons,
            "where": where, "bw": bw, "cap_w": cap_w or PICK_CAP_W}
def S(title, cells, toggle=None):
    return {"title": title, "cells": cells, "toggle": toggle}

def pages():
    P = []
    def page(key, sub, name, strips):
        P.append({"fn": key, "sub": sub, "name": name, "strips": strips})
    SHAPES6 = ["Hold", "Ramp Up", "Ramp Down", "Smooth", "Triangle", "Exp"]

    # ---- OSC
    def osc(x):
        return S("OSC " + x, [SCR(x + " Table", "wtpos", 300, "knob", pos=x + " WT Pos"), K(x + " WT Pos", "Position"),
                              K(x + " Octave", "Octave", bi=True), K(x + " Fine", "Fine", bi=True)], toggle="Osc " + x)
    page(0, 0, "OSC", [osc("A"), osc("B"),
                       S("SUB\nNOISE", [SCR("Sub Shape", "sub"), K("Sub Level", "Sub Level"),
                                        SCR("Noise Type", "noise"), K("Noise Level", "Noise Level")]),
                       S("MACROS", [K("Macro %d" % i, "Macro %d" % i) for i in range(1, 5)])])
    def warp(x):
        return S(x + " WARP", [SCR(x + " Warp 1", "warp"), K(x + " Warp 1 Amt", "Warp 1 Amt"),
                               SCR(x + " Warp 2", "warp"), K(x + " Warp 2 Amt", "Warp 2 Amt")])
    def uni(x):
        return S(x + " UNISON", [SCR(x + " Unison", "unison", overlay="knob"), K(x + " Detune", "Detune"),
                                 K(x + " Blend", "Blend"), K(x + " Spread", "Spread")])
    page(0, 1, "WARP", [warp("A"), uni("A"), warp("B"), uni("B")])
    def mix(x):
        return [S("OSC " + x, [K(x + " Level", "Level"), K(x + " Semi", "Semi", bi=True), K(x + " Pan", "Pan", bi=True),
                               K(x + " Phase", "Phase")], toggle="Osc " + x),
                S(x + " ROUTE", [K(x + " Rnd Phase", "Rnd Phase"), K(x + " > F1", "To Filter 1"), K(x + " > F2", "To Filter 2")])]
    page(0, 2, "OSC MIX", mix("A") + mix("B"))
    page(0, 3, "SUB/NSE", [
        S("SUB", [SEG("Sub Octave", "Octave", ["-1 Oct", "-2 Oct"]), K("Sub Pan", "Pan", bi=True), TG("Sub Direct", "Direct Out")],
          toggle="Sub"),
        S("NOISE", [K("Noise Pan", "Pan", bi=True), TG("Noise Track", "Pitch Track"), TG("Noise Loop", "Loop")], toggle="Noise"),
        S("SENDS", [K("Sub > F1", "Sub > F1"), K("Sub > F2", "Sub > F2"), K("Noise > F1", "Noise > F1"), K("Noise > F2", "Noise > F2")]),
        S("SHAPES", [PICK("Sub Shape", "Sub\nShape", cols=2, where="left"),
                     PICK("Noise Type", "Noise\nType", cols=3, where="left")])])

    # Modulation oscillator (Massive-style): a sine at note pitch + Pitch, routed to the oscillators and filters
    OSCT = ["Off", "Osc A", "Osc B", "A + B"]
    page(0, 4, "MOD OSC", [
        S("MOD", [K("MO Pitch", "Pitch", bi=True),
                      PICK("MO Ring Mod", "Ring\nMod", cols=4, labels=OSCT, where="left", cap_w=80), K("MO RM Amt", "Amount")],
          toggle="Mod Osc"),
        S("PHASE", [PICK("MO Phase", "Target", cols=4, labels=OSCT, where="left", cap_w=80), K("MO Phase Amt", "Amount")]),
        S("POSITION", [PICK("MO Position", "Target", cols=4, labels=OSCT, where="left", cap_w=80), K("MO Pos Amt", "Amount")]),
        S("FILTER FM", [PICK("MO Filter FM", "Target", cols=4, labels=["Off", "F1", "F2", "F1 + F2"], where="left", cap_w=80),
                        K("MO FM Amt", "Amount")])])

    # ---- FILTER
    def filt(n):
        return [S("FILTER %d" % n, [SCR("F%d Type" % n, "filter", 260), K("F%d Cutoff" % n, "Cutoff"), K("F%d Reso" % n, "Reso"),
                                    K("F%d Drive" % n, "Drive")], toggle="Filter %d" % n),
                S("F%d MORE" % n, [K("F%d Key Trk" % n, "Key Track"), K("F%d Fat" % n, "Fat"), K("F%d Level" % n, "Level")]
                  + ([K("Ser/Par", "Ser / Par")] if n == 2 else []))]
    page(1, 0, "FILTER", filt(1) + filt(2))
    # Mega Synth models: Model HELIX = the original filter (F Type above); any other model uses Type + Slope
    def model(n):
        f = "F%d " % n
        return [S("F%d MODEL" % n, [K(f + "Model", "Model", w=320),
                                    PICK(f + "Slope", "Slope", cols=5, labels=["Model", "6", "12", "18", "24"], where="left")]),
                S("F%d TYPE" % n, [PICK(f + "Filter Type", "Type", cols=6, labels=["LP", "HP", "BP", "Notch", "Peak", "AP"],
                                        where="left")])]
    page(1, 1, "MODEL", model(1) + model(2))

    # ---- ENV
    def env(n):
        e = "E%d " % n
        return S("ENV %d" % n + ("\nAMP" if n == 1 else ""), [ENV(e + "Attack", e + "Decay", e + "Sustain", e + "Release"),
                  KS(e + "Attack", "Attack"), KS(e + "Decay", "Decay"), KS(e + "Sustain", "Sustain"), KS(e + "Release", "Release")])
    page(2, 0, "ENV", [env(n) for n in range(1, 5)])
    page(2, 1, "CURVES", [S("ENV %d\nCURVES" % n, [K("E%d A Curve" % n, "Attack", bi=True), K("E%d D Curve" % n, "Decay", bi=True),
                                                    K("E%d R Curve" % n, "Release", bi=True)]) for n in range(1, 5)])

    # ---- LFO
    def lfo(n):
        l = "L%d " % n
        return S("LFO %d" % n, [SCR(l + "Shape", "lfo", 230), K(l + "Rate", "Rate", vis=(l + "Sync", 0, 2)),
                                K(l + "Div", "Sync Div", vis=(l + "Sync", 1, 2)),
                                SEG(l + "Mode", "Mode", ["Retrig", "Free", "1-shot"])])
    page(3, 0, "LFO", [lfo(n) for n in range(1, 5)])
    page(3, 1, "LFO+", [S("LFO %d" % n, [TG("L%d Sync" % n, "Tempo Sync", 170), K("L%d Rise" % n, "Rise"), K("L%d Smooth" % n, "Smooth"),
                                         K("L%d Phase" % n, "Phase")]) for n in range(1, 5)])

    # ---- FX
    page(4, 0, "FX", [
        S("DIST", [SCR("Dist Type", "dist", 230), K("Dist Drive", "Drive"), K("Dist Tone", "Tone"), K("Dist Mix", "Mix")], toggle="Distortion"),
        S("DELAY", [SCR("Delay Mode", "delay", 230), K("Delay Div", "Sync Div", vis=("Delay Sync", 1, 2)),
                    K("Delay Fdbk", "Feedback"), K("Delay Mix", "Mix")], toggle="Delay"),
        S("REVERB", [SCR("Reverb Type", "reverb", 230), K("Rev Size", "Size"), K("Rev Decay", "Decay"), K("Reverb Mix", "Mix")],
          toggle="Reverb"),
        S("OTT", [PIC("ott", 230), K("OTT Depth", "Depth"), K("OTT Time", "Time"), K("OTT Out", "Output")], toggle="OTT")])
    page(4, 1, "FX+", [
        S("CRUSH", [K("Crush Bits", "Bits"), K("Crush Rate", "Rate")]),
        S("DELAY+", [TG("Delay Sync", "Tempo Sync", 170), K("Delay Time", "Time", vis=("Delay Sync", 0, 2)), K("Delay Tone", "Tone")]),
        S("REVERB+", [K("Rev Damp", "Damp"), K("Rev Predelay", "Predelay")]),
        S("OTT+", [K("OTT Upward", "Upward"), K("OTT Down", "Downward")])])
    page(4, 2, "CHO/PHS", [
        S("CHORUS", [SCR("Chorus Mode", "chorus", 230), K("Chorus Rate", "Rate"), K("Chorus Depth", "Depth"), K("Chorus Mix", "Mix")],
          toggle="Chorus"),
        S("PHASER", [K("Phaser Rate", "Rate"), K("Phaser Depth", "Depth"), K("Phaser Freq", "Freq"), K("Phaser Fdbk", "Feedback")],
          toggle="Phaser"),
        S("PHASER\nMIX", [K("Phaser Mix", "Mix")])])
    page(4, 3, "EQ", [
        S("EQ", [K("Low Freq", "Low Freq"), K("Low Gain", "Low Gain", bi=True), K("High Freq", "High Freq"),
                 K("High Gain", "High Gain", bi=True)], toggle="EQ"),
        S("EQ MID", [K("Mid Freq", "Mid Freq"), K("Mid Gain", "Mid Gain", bi=True), K("Mid Q", "Mid Q")])])
    FXS = ["Dist", "Chorus", "Phaser", "Delay", "Reverb", "OTT", "EQ"]
    page(4, 4, "FX ORDER", [S("FX ORDER\n%s" % ("%d-%d" % (i, i + 1) if i < 7 else "7"),
                              [PICK("FX Slot %d" % j, "Slot\n%d" % j, cols=4, labels=FXS, where="left", cap_w=70)
                               for j in range(i, min(i + 2, 8))]) for i in (1, 3, 5, 7)])

    # ---- MOD MATRIX (long lists: knobs, which step through the choices and show the name)
    for b in range(4):
        page(5, b, "M%d-%d" % (b * 4 + 1, b * 4 + 4),
             [S("SLOT %d" % m, [K("M%d Source" % m, "Source", w=300), K("M%d Dest" % m, "Destination", w=420),
                                K("M%d Amount" % m, "Amount", bi=True)])
              for m in range(b * 4 + 1, b * 4 + 5)])
    page(5, 4, "M AUX", [S("AUX\n%d-%d" % (r * 4 + 1, r * 4 + 4), [K("M%d Aux" % m, "Aux %d" % m, w=262)
                                                                for m in range(r * 4 + 1, r * 4 + 5)]) for r in range(4)])
    page(5, 5, "M CURVE", [S("CURVES\n%d-%d" % (r * 4 + 1, r * 4 + 4), [K("M%d Curve" % m, "Curve %d" % m, bi=True)
                                                                    for m in range(r * 4 + 1, r * 4 + 5)]) for r in range(4)])

    # ---- PERFORMERS
    page(6, 0, "PERF", [S("LFO\nSTEPS", [K("L%d Steps" % n, "LFO %d" % n) for n in range(1, 5)]),
                        S("PERF 1", [K("P1 Step", "Step Rate"), K("P1 Length", "Length"),
                                     SEG("P1 Mode", "Mode", ["Retrig", "Free", "1-shot"])]),
                        S("PERF 2", [K("P2 Step", "Step Rate"), K("P2 Length", "Length"),
                                     SEG("P2 Mode", "Mode", ["Retrig", "Free", "1-shot"])])])
    sub = 1
    for p_ in (1, 2):
        page(6, sub, "P%d STEP" % p_, [S("P%d\n%d-%d" % (p_, r * 4 + 1, r * 4 + 4),
                                         [K("P%d Step %d" % (p_, i), "Step %d" % i, bi=True)
                                          for i in range(r * 4 + 1, r * 4 + 5)]) for r in range(4)])
        page(6, sub + 1, "P%d SHAPE" % p_,
             [S("P%d SHAPE\n%d-%d" % (p_, 4 * r + 1, 4 * r + 4),
                [PICK("P%d Shape %d" % (p_, i), "Step\n%d" % i, cols=3, icons=True, where="left", cap_w=64)
                 for i in range(4 * r + 1, 4 * r + 5)]) for r in range(4)])
        sub += 2

    # ---- MACROS
    page(7, 0, "MACROS", [S("MACROS\n1-4", [K("Macro %d" % i, "Macro %d" % i) for i in range(1, 5)]),
                          S("MACROS\n5-8", [K("Macro %d" % i, "Macro %d" % i) for i in range(5, 9)])])

    # ---- BROWSE / GLOBAL
    page(8, 0, "BROWSE", [S("PRESET", [K("Preset", "Preset", w=380), TG("Load", "Load", 150), TG("Mutate 10%", "Mutate 10%", 170),
                                       VAL("CPU", "CPU Load", 170)]),
                          S("PADS", [TG("Pad Browse", "Pad Browse", 190), PICK("Pad Bank", "Pad\nBank", cols=4, where="left")])])
    page(8, 1, "SAMPLE", [S("SAMPLE\n> TABLE", [K("Sample", "Sample", w=380), TG("Make Table", "Make Table", 170)]),
                          S("TABLE\nSETUP", [PICK("Frames", "Frames"), SEG("To Osc", "To Osc", ["Osc A", "Osc B"])])])
    page(8, 2, "VOICE", [S("VOICE", [K("Master", "Master"), SEG("Poly Mode", "Mode", ["Poly", "Mono", "Legato"]),
                                     K("Voices", "Voices"), K("Glide", "Glide")]),
                         S("PLAY", [SEG("Glide Mode", "Glide", ["Always", "Legato"]), SEG("Steal", "Steal", ["Oldest", "Quietest"]),
                                    K("Bend Range", "Bend Range"), K("Vel Sens", "Vel Sens")]),
                         S("ENGINE", [SEG("Quality", "Quality", ["Eco", "Normal", "High"]), TG("CPU Guard", "CPU Guard")])])
    return P

FN_NAMES = ["OSC", "FILTER", "ENV", "LFO", "FX", "MOD", "PERF", "MACRO", "BROWSE"]

# ---------------------------------------------------------------- layout
SEG_PAD = 16
PICK_H = 56
PICK_CAP_W = 100
CHOICES = {}     # parameter name -> choice names, filled by Builder for the layout

def text_w(s, size, weight=600):
    f = pil_font(size, weight)
    return f.getlength(s)

def cell_size(c):
    t = c["type"]
    if t == "knob": return c["w"], 126
    if t == "knobS": return 215, 126
    if t == "screen": return c["w"], 122
    if t == "picture": return c["w"], 122
    if t == "env": return c["w"], 122
    if t in ("choice", "toggle", "readout"): return c["w"], 100
    if t == "pick":
        labels = c["labels"] or CHOICES[c["param"]]
        n = len(labels)
        cols = c["cols"] or n
        rows = -(-n // cols)
        if c["icons"]:
            bw = 66
        else:
            bw = c["bw"] or max(76, int(max(text_w(l, 24, 700) for l in labels) + 28))
        gw, gh = cols * bw - (cols - 1) * 3, rows * PICK_H - (rows - 1) * 3
        c.update(pick_labels=labels, cols_n=cols, bw_n=bw)
        if c["caption"] and c["where"] == "top":
            return max(gw, int(text_w(c["caption"], 24)) + 4), 38 + gh
        if c["caption"]:
            return c["cap_w"] + gw, gh
        return gw, gh
    if t == "seg":
        ws = c["widths"] or [max(76, int(text_w(l, 24) + 2 * SEG_PAD)) for l in c["labels"]]
        c["seg_w"] = ws
        return sum(ws) + 3 * (len(ws) + 1), 100
    raise ValueError(t)

def layout(page):
    for r, st in enumerate(page["strips"]):
        sizes = [cell_size(c) for c in st["cells"]]
        total = sum(w for w, _ in sizes)
        avail = CONTENT_X1 - CONTENT_X0
        n = len(sizes)
        if total > avail:
            raise SystemExit("page %s strip %s: cells need %d px, only %d" % (page["name"], st["title"], total, avail))
        gap = min(48, (avail - total) / (n - 1)) if n > 1 else 0
        x = CONTENT_X0
        y0 = strip_y(r)
        for c, (w, h) in zip(st["cells"], sizes):
            c["rect"] = (int(round(x)), int(y0 + (STRIP_H - h) // 2), w, h)
            if c["type"] == "pick":
                rx, ry = c["rect"][0], c["rect"][1]
                gw = c["cols_n"] * c["bw_n"] - (c["cols_n"] - 1) * 3
                gh = h - (38 if (c["caption"] and c["where"] == "top") else 0)
                if c["caption"] and c["where"] == "top":
                    c["grid"] = (rx, ry + 38, gw, gh)
                elif c["caption"]:
                    c["grid"] = (rx + c["cap_w"], ry, gw, gh)
                else:
                    c["grid"] = (rx, ry, gw, gh)
            x += w + gap

# ---------------------------------------------------------------- self-check
def check_no_labels_over_touch(path):
    """On the MPC a Label drawn over a touch area takes the touch: the control under it can no longer be pressed or
    focused (seen on hardware in 0.2.2). Fail if any tab-level Label overlaps an interactive entry painted before it.
    Labels that only show while that control is hidden (additionalInvalidatingHandles) are allowed."""
    t = json.load(open(path))
    bad = []
    for tab in t["pageData"]["tabs"]:
        seen = []
        for e in tab["componentDefinition"]["componentsData"]:
            typ, b = e["componentData"]["type"], e["bounds"]
            x, y, w, h = map(float, b["bounds"].split())
            if typ == "Label":
                if b.get("additionalInvalidatingHandles"):
                    continue
                for nm, (X, Y, W, H) in seen:
                    if x < X + W and X < x + w and y < Y + H and Y < y + h:
                        bad.append("%s: label '%s' over '%s'" % (tab["tabName"], e["componentData"]["name"], nm))
            elif typ not in ("Image", "Focus"):
                seen.append((e["componentData"]["name"], (x, y, w, h)))
    if bad:
        raise SystemExit("make_skin: labels on top of touch areas:\n  " + "\n  ".join(bad))
    # The focus frame must be drawn underneath everything else in a component: drawn on top, it takes the touches
    # once the component is selected (0.2.6: a second choice in the same group could not be pressed).
    # (Cells whose children are only pictures, flipped by the cell's own Toggle Switch action, are not affected.)
    late = []
    for d in t["pageData"]["componentDefinitions"]["localComponentDefinitions"]:
        kids = d["value"]["componentsData"]
        types = [k["componentData"]["type"] for k in kids]
        pressable = {"Button", "Knob", "Slider", "Envelope"} & set(types)
        if pressable and "Focus" in types and types.index("Focus") > min(types.index(x) for x in pressable):
            late.append(d["key"])
    if late:
        raise SystemExit("make_skin: focus frame not first (it would block touches) in: " + ", ".join(late))
    # A cell that opens an overlay must itself contain a child reading its "Data" handle (a Label or Knob); a cell with
    # only a focus ring opened an empty menu on hardware (0.2.3).
    empty = []
    for d in t["pageData"]["componentDefinitions"]["localComponentDefinitions"]:
        v = d["value"]
        if any(a_["handler"] == "Show Overlay" for a_ in v["actions"]):
            if not any(k["componentData"]["type"] not in ("Focus",) and k["componentData"]["data"].get("handleName") == "Data"
                       for k in v["componentsData"]):
                empty.append(d["key"])
    if empty:
        raise SystemExit("make_skin: overlay cells with nothing reading Data: " + ", ".join(empty))


# ---------------------------------------------------------------- build
class Builder:
    def __init__(self, params, out):
        self.params = params
        self.by_name = {}
        for p in params:
            self.by_name.setdefault(p["name"], p)
        self.out = out
        self.images = {}
        self.replacements = {}
        self.comp_keys = set()
        for p in params:
            if p["kind"] in ("choice", "bool"):
                CHOICES.setdefault(p["name"], p["choices"])

    def p(self, name):
        if name not in self.by_name:
            raise SystemExit("parameter not found in params.json: %r" % name)
        return self.by_name[name]

    def h(self, name):
        return "Parameter %d" % self.p(name)["index"]

    def discrete(self, name):
        p = self.p(name)
        if p["kind"] in ("bool", "choice") and p["choices"] and name not in ("A Table", "B Table"):
            self.replacements[p["index"]] = p["choices"]
        return p

    def save(self, fname, image):
        if fname in self.images and not fname.endswith("#n"):
            old = self.images[fname]
            if old.size != image.size or old.tobytes() != image.convert(old.mode).tobytes():
                raise SystemExit("make_skin: two different images named %s" % fname)
        if fname not in self.images:
            image.save(os.path.join(self.out, fname), optimize=True)
            self.images[fname] = image
        return fname

    def knob_def(self, size, cw, bi):
        key = self.knob_comp(size, cw, bi, False)
        if key not in self.comp_keys:
            self.comp_keys.add(key)
            tx = size + 12
            kid = o(("version", 2), ("knobType", "FilmStrip"), ("filmStrip", self.knob_strip(size, bi)),
                    ("numFrames", FRAMES), ("invert", False), ("handleName", "Data"))
            kids = [focus_entry(),
                    entry("knob", "Knob", kid, bounds((0, (126 - size) // 2, size, size))),
                    entry("value", "Label", label("Value", 32, "Bold"), bounds((tx, 60, cw - tx, 44)))]
            self.defs.append(o(("key", key), ("value", component(kids, actions_for(OVERLAYS["bi" if bi else "knob"])))))
        return key

    def screen_def(self, fname, n, w, h):
        """A waveform screen that is also the control: a Knob widget whose film strip is the screen artwork (drag to change
        the shape / type, like any knob), with the value written in its bottom band. fname None = an invisible knob
        over a picture drawn underneath (the wavetable screen follows Position but edits Table)."""
        key = "hx screen " + (fname or "blank %dx%d" % (w, h))
        if key not in self.comp_keys:
            self.comp_keys.add(key)
            if fname is None:
                fname = self.save("blank_%dx%d.png" % (w, h), Image.new("RGBA", (w, h), (0, 0, 0, 0)))
                n = 1
            kid = o(("version", 2), ("knobType", "FilmStrip"), ("filmStrip", fname), ("numFrames", n), ("invert", False),
                    ("handleName", "Data"))
            val = entry("value", "Label", label("Value", 26, "Bold", just="horizontallyCentred verticallyCentred"),
                        bounds((FM + 6, FM + h - 40, w - 12, 34)))
            self.defs.append(o(("key", key), ("value", component(
                [focus_entry(), entry("knob", "Knob", kid, bounds((FM, FM, w, h))), val], actions_for(OVERLAYS["knob"])))))
        return key

    @staticmethod
    def knob_comp(size, cw, bi, menu):
        return "hx knob%d%s%s%s" % (size, "" if cw in (250, 215) else "_w%d" % cw, "_bi" if bi else "", "_menu" if menu else "")

    # -- shared artwork
    def knob_strip(self, size, bi):
        name = "knob%d%s.png" % (size, "_bi" if bi else "")
        if name not in self.images:
            self.save(name, film([knob_frame(size, i / (FRAMES - 1), bi) for i in range(FRAMES)]))
        return name

    def screen_strip(self, kind, choices, w, h=122):
        name = "scr_%s_%d.png" % (kind, w)
        if name not in self.images:
            frames = screen_film(kind, choices, w, h)
            self.save(name, film(frames))
            self.images[name + "#n"] = len(frames)
        return name, self.images[name + "#n"]

    def build(self):
        os.makedirs(self.out, exist_ok=True)
        defs = []
        for key, bip in (("hx knob overlay", False), ("hx knob overlay bi", True)):
            defs.append(o(("key", key), ("value", overlay_component("KnobOverlay", knob_overlay(bip)))))
        # No MenuOverlay anywhere: the MPC firmware can open it with empty entries (reported by the Open MPC developers
        # on 3.10.1.12, and seen on hardware with HELIX 0.2.3-0.2.5). Choices are buttons (pickers) or knobs instead.
        # Knob cells, screens and pickers are defined on demand (see knob_def, screen cells, picker cells).
        self.defs = defs

        tabs, qmaps = [], []
        for page in pages():
            layout(page)
            tabs.append(self.tab(page))
            qmaps.append(self.qmap(page))

        repl = [o(("key", "Parameter %d" % i), ("value", ch)) for i, ch in sorted(self.replacements.items())]
        tui = o(("pageData", o(("version", 1),
                               ("componentDefinitions", o(("version", 2), ("importFiles", []), ("localComponentDefinitions", defs))),
                               ("replacementStrings", repl),
                               ("info", o(("version", 1), ("type", "CompleteDescription"))),
                               ("tabs", tabs))))
        with open(os.path.join(self.out, "TUI.json"), "w") as f:
            f.write(dump(tui) + "\n")
        check_no_labels_over_touch(os.path.join(self.out, "TUI.json"))
        prog = [self.p(n)["index"] for n in ("Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8",
                                             "F1 Cutoff", "F1 Reso", "F1 Drive", "A WT Pos", "B WT Pos", "Delay Mix", "Reverb Mix", "Master")]
        ql = o(("version", 4), ("info", o(("version", 1), ("type", "CompleteDescription"))),
               ("Screen Mode Q-Links", o(("version", 4), ("map", qmaps))),
               ("Program Mode Q-Links", o(*[("Q-Link %d" % (i + 1), prog[i]) for i in range(16)])))
        with open(os.path.join(self.out, "Q-Links.json"), "w") as f:
            f.write(dump(ql) + "\n")

    # -- one tab
    def tab(self, page):
        bgname = "bg_%d_%d.png" % (page["fn"], page["sub"])
        page["bg"] = background(page)
        self.save(bgname, page["bg"])
        kids = [entry("background", "Image", o(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", bgname)),
                      bounds((0, 0, 1280, 628)))]
        for r, st in enumerate(page["strips"]):
            kids += self.tag(r, st)
            for c in st["cells"]:
                kids += self.cell(c)
        root = component(kids, colour=argb(GROUND))
        qb = ["%d %d %d %d" % (PAD, strip_y(r), 1280 - 2 * PAD, STRIP_H) for r in range(len(page["strips"]))]
        return o(("version", 1), ("tabName", page["name"]), ("fnKeyIndex", page["fn"]), ("fnKeySubIndex", page["sub"]),
                 ("componentDefinition", root), ("qlinkBoundsData", qb))

    def tag(self, r, st):
        y = strip_y(r) + BORDER
        x = PAD + BORDER
        h = STRIP_H - 2 * BORDER
        out = []
        lines = st["title"].split("\n")
        cells = [c for c in st["cells"] if c.get("param")]
        anchor = st["toggle"] or (cells[0]["param"] if cells else st["cells"][0]["params"][0])
        if st["toggle"]:
            # The whole tag is one touch cell: title and On/Off are drawn into its 2-frame film strip (no labels on top).
            ch = self.discrete(st["toggle"])["choices"]
            key = "hx tag " + st["toggle"]
            if key not in self.comp_keys:
                self.comp_keys.add(key)
                strip = self.save("tag_%s.png" % slug(st["toggle"]), film([tag_frame(lines, ch[0], False), tag_frame(lines, ch[-1], True)]))
                self.defs.append(o(("key", key), ("value", component([
                    entry("state", "Image", o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", strip),
                                              ("numFrames", 2), ("handleName", "Data")), bounds((0, 0, TAG_W, h))),
                    focus_entry()], [act("Mouse Down", "Toggle Switch", "Data")]))))
            out.append(entry("tag " + st["toggle"], key, o(("version", 1), ("handleName", "Data")),
                             bounds((x, y, TAG_W, h), focus="Yes", invalid="Hide"), remap=[("Data", self.h(st["toggle"]))]))
        else:
            ty = y + h / 2 - 17 * len(lines)
            for i, line in enumerate(lines):
                out.append(self.caption(line, anchor, (x, ty + i * 34, TAG_W, 34), 28, "Bold", TAG_TEXT,
                                        "horizontallyCentred verticallyCentred"))
        return out

    def vis(self, c):
        if not c.get("vis"):
            return None
        pname, i, n = c["vis"]
        self.discrete(pname)
        return ["IndexedEnabling/%d/%d/%s" % (i, n, self.h(pname))]

    def named(self, text, param):
        """A handle that shows `text` as the name of a real parameter. Captions are Name labels bound to a parameter
        (like the SDK's generator does), not Text labels on an unbound handle; if the MPC ignores WithName, the label
        still shows the parameter's own name."""
        clean = text.replace("[", "(").replace("]", ")").replace("/", "-")
        return "WithName[%s]/%s" % (clean, self.h(param))

    def caption(self, text, param, rect, size=24, style="SemiBold", colour=MUTED, just="left verticallyCentred", inval=None):
        return entry("caption", "Label", label("Name", size, style, colour, just, handle=self.named(text, param)),
                     bounds(rect, inval=inval))

    def name_label(self, text, param, x, y, w, inval=None):
        return self.caption(text, param, (x, y, w, 32), inval=inval)

    def cell(self, c):
        t = c["type"]
        x, y, w, h = c["rect"]
        out = []
        if t in ("knob", "knobS"):
            p = self.p(c["param"])
            if p["kind"] in ("choice", "bool"):
                self.discrete(c["param"])       # long lists (mod source / dest, divisions) are knobs that step through
            size = KNOB if t == "knob" else KNOB_S
            comp = self.knob_def(size, w, c["bi"])
            inval = self.vis(c)
            out.append(self.name_label(c["name"], c["param"], x + size + 12, y + 26, w - size - 12, inval))
            out.append(entry(c["param"], comp, o(("version", 1), ("handleName", "Data")),
                             bounds((x, y, w, h), focus="Yes", invalid="Hide", inval=inval), remap=[("Data", self.h(c["param"]))]))
            if inval:
                # what shows in the slot while the control is hidden
                pname, i, n = c["vis"]
                msg = "Tempo sync on" if i == 0 else "Sync off"
                other = ["IndexedEnabling/%d/%d/%s" % (1 - i, n, self.h(pname))]
                out.append(self.caption(c["name"], c["param"], (x, y + 26, w, 32), inval=other))
                out.append(self.caption(msg, c["param"], (x, y + 62, w, 32), style="Regular", inval=other))
        elif t == "screen":
            p = self.p(c["param"])
            if p["kind"] in ("choice", "bool"):
                self.discrete(c["param"])
            fname, n = self.screen_strip(c["kind"], p["choices"], w, h)
            if c["pos"]:
                # the picture follows Position; the control on top (an invisible knob) edits the Table
                out.append(entry("screen", "Image", o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", fname),
                                                      ("numFrames", n), ("handleName", "Data")),
                                 bounds((x, y, w, h), invalid="Hide"), remap=[("Data", self.h(c["pos"]))]))
                comp = self.screen_def(None, 1, w, h)
            else:
                comp = self.screen_def(fname, n, w, h)
            out.append(entry(c["param"], comp, o(("version", 1), ("handleName", "Data")),
                             bounds((x - FM, y - FM, w + 2 * FM, h + 2 * FM), focus="Yes", invalid="Hide"),
                             remap=[("Data", self.h(c["param"]))]))
        elif t == "picture":
            fname, n = self.screen_strip(c["kind"], [], w, h)
            out.append(entry("picture", "Image", o(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", fname)),
                             bounds((x, y, w, h))))
        elif t == "env":
            a, d, s, r = c["params"]
            ed = o(("version", 1), ("envelopeType", "ADSR"), ("envelopeStyle", "WithoutHeader"), ("envelopeName", ""),
                   ("backgroundColour", argb(SCREEN)), ("scaleColour", argb(GRID)), ("lineColour", argb(LINE)),
                   ("fillColour", argb(ACCENT, 0x55)), ("highlightColour", argb(ACCENT)), ("lineWidth", 3),
                   ("handleWidth", 15), ("handleHeight", 10), ("handleShape", "Circle"), ("handleLineWidth", 2),
                   ("handleLineColour", argb(LINE)), ("handleFillColour", argb(ACCENT)), ("handleTextColour", TEXT),
                   ("handleLineWidthOnPressed", 2), ("handleLineColourOnPressed", argb(LINE)),
                   ("handleFillColourOnPressed", argb(ACCENT)), ("handleTextColourOnPressed", TEXT),
                   ("handleArrowLineColourOnPressed", argb(LINE)), ("handleArrowFillColourOnPressed", argb(ACCENT)),
                   ("attackHandle", self.h(a)), ("decayHandle", self.h(d)), ("sustainHandle", self.h(s)),
                   ("releaseHandle", self.h(r)), ("attackShapeHandle", "Invalid"), ("decayShapeHandle", "Invalid"),
                   ("releaseShapeHandle", "Invalid"), ("forceHideShapeHandles", True), ("forceCurveShape", 0.5),
                   ("curveType", "Exponential"))
            out.append(entry("envelope", "Envelope", ed, bounds((x + 3, y + 3, w - 6, h - 6))))
        elif t in ("choice", "toggle", "readout"):
            box_h = 60
            by = y + h - box_h
            out.append(self.name_label(c["name"], c["param"], x, y, w))
            if t == "choice":
                self.discrete(c["param"])
                bx = self.save("box_%d.png" % w, box_image(w, box_h, SCREEN, chevron=True).convert("RGB"))
                out.append(entry("box", "Image", o(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", bx)),
                                 bounds((x, by, w, box_h))))
                out.append(entry(c["param"], "hx menu box", o(("version", 1), ("handleName", "Data")),
                                 bounds((x, by, w, box_h), focus="Yes", invalid="Hide"), remap=[("Data", self.h(c["param"]))]))
            elif t == "toggle":
                ch = self.discrete(c["param"])["choices"]
                strip = self.save("toggle_%s.png" % slug(c["param"]), film([labelled_box(w, box_h, BTN_OFF, ch[0]),
                                                                            labelled_box(w, box_h, ORANGE, ch[-1])]))
                key = "hx toggle " + c["param"]
                if key not in self.comp_keys:
                    self.comp_keys.add(key)
                    # same as the tag: film strip for the state, Toggle Switch on Mouse Down (then bank the Q-Links)
                    self.defs.append(o(("key", key), ("value", component([
                        entry("state", "Image", o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", strip),
                                                  ("numFrames", 2), ("handleName", "Data")), bounds((0, 0, w, box_h))),
                        focus_entry()], [act("Mouse Down", "Toggle Switch", "Data"), act("Mouse Down", "Q-Link", "Data")]))))
                out.append(entry(c["param"], key, o(("version", 1), ("handleName", "Data")),
                                 bounds((x, by, w, box_h), focus="Yes", invalid="Hide"), remap=[("Data", self.h(c["param"]))]))
            else:
                bx = self.save("box_plain_%d.png" % w, box_image(w, box_h, SCREEN).convert("RGB"))
                out.append(entry("box", "Image", o(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", bx)),
                                 bounds((x, by, w, box_h))))
                out.append(entry("value", "Label", label("Value", 32, "Bold", just="horizontallyCentred verticallyCentred"),
                                 bounds((x, by, w, box_h)), remap=[("Data", self.h(c["param"]))]))
        elif t == "seg":
            p = self.discrete(c["param"])
            n = len(p["choices"])
            if n != len(c["labels"]):
                raise SystemExit("%s has %d choices, segment lists %d" % (c["param"], n, len(c["labels"])))
            box_h = 60
            by = y + h - box_h
            out.append(self.name_label(c["name"], c["param"], x, y, w))
            key = "hx seg %s" % c["param"]
            total = sum(sw + 6 for sw in c["seg_w"]) - 3 * (n - 1)
            if n == 2:
                # Two choices: film strip with the current one lit, flipped by Toggle Switch on touch (as the on/off
                # switches, which work on hardware).
                frames = []
                for cur in range(n):
                    im = Image.new("RGB", (total, box_h), SCREEN)
                    bx = 0
                    for i, (lab, sw) in enumerate(zip(c["labels"], c["seg_w"])):
                        bw = sw + 6
                        im.paste(labelled_box(bw, box_h, ORANGE if i == cur else BTN_OFF, lab, 24), (bx, 0))
                        bx += bw - 3
                    frames.append(im)
                strip = self.save("seg_%s.png" % slug(c["param"]), film(frames))
                kids = [entry("state", "Image", o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", strip),
                                                  ("numFrames", n), ("handleName", "Data")), bounds((0, 0, total, box_h))),
                        focus_entry()]
                acts = [act("Mouse Down", "Toggle Switch", "Data"), act("Mouse Down", "Q-Link", "Data")]
            else:
                # Three or more: the SDK's radio group (one Button per choice, buttonId = its step, tap acts at once).
                # Each caption is drawn into its button's images; nothing is placed on top of the buttons.
                kids, bx = [focus_entry()], FM      # focus frame underneath (see pickers)
                for i, (lab, sw) in enumerate(zip(c["labels"], c["seg_w"])):
                    bw = sw + 6
                    on = self.save("seg_%d_%s_on.png" % (bw, slug(lab)), labelled_box(bw, box_h, ORANGE, lab, 24))
                    off = self.save("seg_%d_%s_off.png" % (bw, slug(lab)), labelled_box(bw, box_h, BTN_OFF, lab, 24))
                    kids.append(entry("seg %d" % i, "Button",
                                      o(("version", 2), ("onImage", on), ("offImage", off), ("buttonId", i), ("numButtonsInGroup", n),
                                        ("handleName", "Data"), ("gestureBehaviour", "Instant")), bounds((bx, FM, bw, box_h))))
                    bx += bw - 3
                acts = [act("Mouse Down", "Q-Link", "Data")]
            if key not in self.comp_keys:
                self.comp_keys.add(key)
                self.defs.append(o(("key", key), ("value", component(kids, acts))))
            m = FM if n > 2 else 0
            out.append(entry(c["param"], key, o(("version", 1), ("handleName", "Data")),
                             bounds((x - m, by - m, total + 2 * m, box_h + 2 * m), focus="Yes", invalid="Hide"),
                             remap=[("Data", self.h(c["param"]))]))
        elif t == "pick":
            p = self.discrete(c["param"])
            labels = c["pick_labels"]
            gx, gy, gw, gh = c["grid"]
            if c["caption"] and c["where"] == "top":
                out.append(self.name_label(c["caption"].replace("\n", " "), c["param"], x, y, w))
            elif c["caption"]:
                lines = c["caption"].split("\n")
                for i, line in enumerate(lines):
                    out.append(self.caption(line, c["param"], (x, gy + gh / 2 - 15 * len(lines) + i * 30, c["cap_w"] - 6, 30)))
            key = "hx pick " + c["param"]
            if key not in self.comp_keys:
                self.comp_keys.add(key)
                # The focus frame goes FIRST, i.e. underneath the buttons. Drawn on top (0.2.6) it caught every touch once
                # the group was selected, so a second choice in the same group could not be pressed.
                kids = [focus_entry()]
                for i, lab in enumerate(labels):
                    r_, c_ = divmod(i, c["cols_n"])
                    bx, by = FM + c_ * (c["bw_n"] - 3), FM + r_ * (PICK_H - 3)
                    if c["icons"]:
                        on = self.save("pick_%d_%s_on.png" % (c["bw_n"], slug(lab)), icon_box(c["bw_n"], PICK_H, ORANGE, lab))
                        off = self.save("pick_%d_%s_off.png" % (c["bw_n"], slug(lab)), icon_box(c["bw_n"], PICK_H, BTN_OFF, lab))
                    else:
                        on = self.save("seg_%d_%s_on.png" % (c["bw_n"], slug(lab)), labelled_box(c["bw_n"], PICK_H, ORANGE, lab, 24))
                        off = self.save("seg_%d_%s_off.png" % (c["bw_n"], slug(lab)), labelled_box(c["bw_n"], PICK_H, BTN_OFF, lab, 24))
                    kids.append(entry("choice %d" % i, "Button",
                                      o(("version", 2), ("onImage", on), ("offImage", off), ("buttonId", i),
                                        ("numButtonsInGroup", len(labels)), ("handleName", "Data"), ("gestureBehaviour", "Instant")),
                                      bounds((bx, by, c["bw_n"], PICK_H))))
                self.defs.append(o(("key", key), ("value", component(kids, [act("Mouse Down", "Q-Link", "Data")]))))
            out.append(entry(c["param"], key, o(("version", 1), ("handleName", "Data")),
                             bounds((gx - FM, gy - FM, gw + 2 * FM, gh + 2 * FM), focus="Yes", invalid="Hide"),
                             remap=[("Data", self.h(c["param"]))]))
        return out

    def qmap(self, page):
        q = {}
        for r, st in enumerate(page["strips"]):
            col = 0
            for c in st["cells"]:
                if c["type"] in ("env", "picture", "readout"):
                    continue
                if col > 3:
                    raise SystemExit("page %s strip %s has more than four Q-Link controls" % (page["name"], st["title"]))
                q[13 + col - 4 * r] = self.p(c["param"])["index"]
                col += 1
        return o(("Tab", page["fn"] + 1), ("SubTab", page["sub"] + 1), ("Bank Direction", "RowTopDown"),
                 ("Q-Links", o(*[("Q-Link %d" % i, q.get(i, -1)) for i in range(1, 17)])))


# ---------------------------------------------------------------- approximate preview (for checking the layout)
def preview(builder, outdir):
    os.makedirs(outdir, exist_ok=True)
    defaults = {p["index"]: p["default"] for p in builder.params}
    disp = {p["index"]: p["display"] for p in builder.params}
    def val(name):
        return defaults[builder.p(name)["index"]]
    def shown(name):
        p = builder.p(name)
        if p["index"] in builder.replacements:
            ch = builder.replacements[p["index"]]
            return ch[min(len(ch) - 1, int(round(p["default"] * (len(ch) - 1))))]
        return disp[p["index"]]
    def step(name, n):
        return min(n - 1, int(round(val(name) * (n - 1))))
    files = []
    for page in pages():
        layout(page)
        canvas = Image.new("RGB", (1280, 800), (60, 60, 60))
        top = Image.new("RGB", (1280, 110), (40, 40, 40))
        canvas.paste(top, (0, 0))
        tab = background(page).copy()
        d = ImageDraw.Draw(tab)
        F = lambda s, wgt=700: pil_font(s, wgt)
        def txt(x, y, w, h, s, size, wgt=700, col=(255, 255, 255), center=False):
            f = F(size, wgt)
            tw = f.getlength(s)
            tx = x + (w - tw) / 2 if center else x
            d.text((tx, y + h / 2), s, font=f, fill=col, anchor="lm")
        for r, st in enumerate(page["strips"]):
            y = strip_y(r) + BORDER
            x = PAD + BORDER
            h = STRIP_H - 2 * BORDER
            lines = st["title"].split("\n")
            if st["toggle"]:
                on = step(st["toggle"], 2) == 1
                st_img = Image.open(os.path.join(builder.out, "tag_%s.png" % slug(st["toggle"])))
                tab.paste(st_img.crop((0, h if on else 0, TAG_W, 2 * h if on else h)), (x, y))
            else:
                ty = y + h / 2 - 17 * len(lines)
                for i, line in enumerate(lines):
                    txt(x, ty + i * 34, TAG_W, 34, line, 28, col=INK, center=True)
            for c in st["cells"]:
                cx, cy, w, hh = c["rect"]
                t = c["type"]
                hidden = False
                if c.get("vis"):
                    pn, i, n = c["vis"]
                    hidden = step(pn, n) != i
                if t in ("knob", "knobS"):
                    size = KNOB if t == "knob" else KNOB_S
                    if hidden:
                        txt(cx, cy + 26, w, 32, c["name"], 24, 600, (198, 204, 218))
                        txt(cx, cy + 62, w, 32, "Tempo sync on" if c["vis"][1] == 0 else "Sync off", 24, 400, (198, 204, 218))
                        continue
                    strip = Image.open(os.path.join(builder.out, builder.knob_strip(size, c["bi"])))
                    fi = int(round(val(c["param"]) * (FRAMES - 1)))
                    fr = strip.crop((0, fi * size, size, (fi + 1) * size))
                    tab.paste(fr, (cx, cy + (126 - size) // 2), fr)
                    txt(cx + size + 12, cy + 26, w, 32, c["name"], 24, 600, (198, 204, 218))
                    txt(cx + size + 12, cy + 60, w, 44, shown(c["param"]), 32)
                elif t == "screen":
                    fname = "scr_%s_%d.png" % (c["kind"], w)
                    strip = Image.open(os.path.join(builder.out, fname))
                    n = strip.size[1] // hh
                    fi = min(n - 1, int(round(val(c["pos"] or c["param"]) * (n - 1))))
                    tab.paste(strip.crop((0, fi * hh, w, (fi + 1) * hh)), (cx, cy))
                    txt(cx + 6, cy + hh - 40, w - 12, 34, shown(c["param"]), 26, center=True)
                elif t == "picture":
                    tab.paste(Image.open(os.path.join(builder.out, "scr_%s_%d.png" % (c["kind"], w))), (cx, cy))
                elif t == "env":
                    a, dd, s, rr = (val(p) for p in c["params"])
                    ex, ey, ew, eh = cx + 3, cy + 3, w - 6, hh - 6
                    pts = [(0, 0), (a * 0.25, 1), (a * 0.25 + dd * 0.25, s), (0.75, s), (0.75 + rr * 0.25, 0)]
                    P_ = [(ex + 10 + (ew - 20) * u, ey + eh - 10 - (eh - 20) * v) for u, v in pts]
                    d.polygon(P_ + [(P_[-1][0], ey + eh - 10), (P_[0][0], ey + eh - 10)], fill=(40, 90, 130))
                    d.line(P_, fill=LINE, width=3)
                    for px, py in P_[1:]:
                        d.ellipse((px - 7, py - 7, px + 7, py + 7), fill=ACCENT, outline=LINE, width=2)
                elif t in ("choice", "toggle", "readout"):
                    by = cy + hh - 60
                    txt(cx, cy, w, 32, c["name"], 24, 600, (198, 204, 218))
                    if t == "choice":
                        tab.paste(Image.open(os.path.join(builder.out, "box_%d.png" % w)), (cx, by))
                        txt(cx + 14, by, w - 50, 60, shown(c["param"]), 28)
                    elif t == "toggle":
                        on = step(c["param"], 2) == 1
                        st_img = Image.open(os.path.join(builder.out, "toggle_%s.png" % slug(c["param"])))
                        tab.paste(st_img.crop((0, 60 if on else 0, w, 120 if on else 60)), (cx, by))
                    else:
                        tab.paste(Image.open(os.path.join(builder.out, "box_plain_%d.png" % w)), (cx, by))
                        txt(cx, by, w, 60, shown(c["param"]), 32, center=True)
                elif t == "seg":
                    by = cy + hh - 60
                    txt(cx, cy, w, 32, c["name"], 24, 600, (198, 204, 218))
                    cur = step(c["param"], len(c["labels"]))
                    if len(c["labels"]) == 2:
                        st_img = Image.open(os.path.join(builder.out, "seg_%s.png" % slug(c["param"])))
                        tab.paste(st_img.crop((0, cur * 60, st_img.size[0], (cur + 1) * 60)), (cx, by))
                    else:
                        bx = cx
                        for i, (lab, sw) in enumerate(zip(c["labels"], c["seg_w"])):
                            bw = sw + 6
                            tab.paste(Image.open(os.path.join(builder.out, "seg_%d_%s_%s.png" % (bw, slug(lab), "on" if i == cur else "off"))), (bx, by))
                            bx += bw - 3
                elif t == "pick":
                    labels = c["pick_labels"]
                    cur = step(c["param"], len(labels))
                    gx, gy, gw, gh = c["grid"]
                    gy -= 110 * 0
                    if c["caption"] and c["where"] == "top":
                        txt(cx, cy, w, 32, c["caption"].replace("\n", " "), 24, 600, (198, 204, 218))
                    elif c["caption"]:
                        lines = c["caption"].split("\n")
                        for i, line in enumerate(lines):
                            txt(cx, gy + gh / 2 - 15 * len(lines) + i * 30, c["cap_w"] - 6, 30, line, 24, 600, (198, 204, 218))
                    for i, lab in enumerate(labels):
                        r_, c_ = divmod(i, c["cols_n"])
                        bx, by = gx + c_ * (c["bw_n"] - 3), gy + r_ * (PICK_H - 3)
                        fn = ("pick_%d_%s_%s.png" if c["icons"] else "seg_%d_%s_%s.png") % (c["bw_n"], slug(lab), "on" if i == cur else "off")
                        tab.paste(Image.open(os.path.join(builder.out, fn)), (bx, by))
        canvas.paste(tab, (0, 110))
        dc = ImageDraw.Draw(canvas)
        f = pil_font(22, 600)
        dc.text((20, 55), "HELIX  ·  %s   (MPC header and function keys are drawn by the MPC)" % page["name"], font=f,
                fill=(230, 230, 230), anchor="lm")
        keys = FN_NAMES
        kw = 1280 / len(keys)
        for i, k in enumerate(keys):
            on = i == page["fn"]
            dc.rectangle((i * kw, 738, (i + 1) * kw - 2, 799), fill=(80, 80, 80) if on else (50, 50, 50))
            dc.text((i * kw + kw / 2, 769), k, font=f, fill=(255, 255, 255) if on else (190, 190, 190), anchor="mm")
        fn = os.path.join(outdir, "%d_%d_%s.png" % (page["fn"], page["sub"], page["name"].replace("/", "-").replace(" ", "_")))
        canvas.save(fn)
        files.append(fn)
    return files

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--params", default=os.path.join(MPC, "build", "params.json"))
    ap.add_argument("--out", default=os.path.join(MPC, "skins"))
    ap.add_argument("--preview", default=None)
    a = ap.parse_args()
    params = json.load(open(a.params))["params"]
    if os.path.isdir(a.out):
        for f in os.listdir(a.out):
            if f.endswith((".png", ".json")):
                os.remove(os.path.join(a.out, f))
    b = Builder(params, a.out)
    b.build()
    n_img = len([k for k in b.images if not k.endswith("#n")])
    print("make_skin: %d tabs, %d images -> %s" % (len(pages()), n_img, a.out))
    if a.preview:
        for f in preview(b, a.preview):
            print("  preview", f)

if __name__ == "__main__":
    main()
