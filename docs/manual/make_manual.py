#!/usr/bin/env python3
"""Builds docs/HELIX-Manual.pdf.

The reference sections (parameters, choice lists, modulation destinations, MPC pages, factory presets) are read
from the source tree, so the manual always matches the code:
  helix-core/include/helix/ParamList.inc, helix-core/src/Params.cpp, helix-mpc/tools/make_skin.py (MPC pages),
  Presets/*/*.json
Screenshots live in docs/manual/img/ (taken from the desktop plugin; see the HELIX_START_PAGE / _PROGRAM hooks).

    python3 docs/manual/make_manual.py            # needs reportlab
"""
import glob
import json
import os
import re
import sys
from datetime import date

from reportlab.graphics.shapes import Drawing, Line, Polygon, Rect, String
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, CondPageBreak, Frame, Image, KeepTogether, NextPageTemplate,
                                PageBreak, PageTemplate, Paragraph, Spacer, Table, TableStyle)
from reportlab.platypus.tableofcontents import TableOfContents

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
IMG = os.path.join(ROOT, "docs", "manual", "img")
OUT = os.path.join(ROOT, "docs", "HELIX-Manual.pdf")
VERSION = json.load(open(os.path.join(ROOT, "helix-mpc", "plugin.json")))["version"]

# ------------------------------------------------------------------------------------------ fonts and colours

FONT_DIR = "/usr/share/fonts/truetype/dejavu"
for name, file in [("Sans", "DejaVuSans.ttf"), ("Sans-Bold", "DejaVuSans-Bold.ttf"), ("Mono", "DejaVuSansMono.ttf"),
                   ("Sans-Oblique", "DejaVuSans-Oblique.ttf")]:
    path = os.path.join(FONT_DIR, file)
    if not os.path.exists(path):
        path = os.path.join(FONT_DIR, "DejaVuSans.ttf")
    pdfmetrics.registerFont(TTFont(name, path))
pdfmetrics.registerFontFamily("Sans", normal="Sans", bold="Sans-Bold", italic="Sans-Oblique", boldItalic="Sans-Bold")

INK = colors.HexColor("#15171a")
MUTED = colors.HexColor("#5d6168")
LINE = colors.HexColor("#c9c5bc")
PAPER = colors.HexColor("#f3f1ec")
ACCENT = colors.HexColor("#d6402b")
MOD = colors.HexColor("#1f6fd1")
NOTE_BG = colors.HexColor("#fbeee9")

ss = {}
ss["body"] = ParagraphStyle("body", fontName="Sans", fontSize=9.6, leading=14, textColor=INK, spaceAfter=6)
ss["small"] = ParagraphStyle("small", parent=ss["body"], fontSize=8.2, leading=11, spaceAfter=0)
ss["cell"] = ParagraphStyle("cell", parent=ss["body"], fontSize=8.2, leading=10.6, spaceAfter=0)
ss["cellb"] = ParagraphStyle("cellb", parent=ss["cell"], fontName="Sans-Bold")
ss["h1"] = ParagraphStyle("h1", fontName="Sans-Bold", fontSize=20, leading=25, textColor=INK, spaceBefore=4, spaceAfter=10, keepWithNext=1)
ss["h2"] = ParagraphStyle("h2", fontName="Sans-Bold", fontSize=13, leading=17, textColor=INK, spaceBefore=12, spaceAfter=6, keepWithNext=1)
ss["h3"] = ParagraphStyle("h3", fontName="Sans-Bold", fontSize=10.5, leading=14, textColor=ACCENT, spaceBefore=8, spaceAfter=3, keepWithNext=1)
ss["bullet"] = ParagraphStyle("bullet", parent=ss["body"], leftIndent=12, bulletIndent=2, spaceAfter=3)
ss["num"] = ParagraphStyle("num", parent=ss["body"], leftIndent=16, bulletIndent=0, spaceAfter=3)
ss["caption"] = ParagraphStyle("caption", parent=ss["small"], textColor=MUTED, alignment=TA_CENTER, spaceBefore=3, spaceAfter=10)
ss["note"] = ParagraphStyle("note", parent=ss["body"], fontSize=9, leading=13, spaceAfter=0)
ss["toc1"] = ParagraphStyle("toc1", fontName="Sans-Bold", fontSize=10.5, leading=16, textColor=INK, leftIndent=0)
ss["toc2"] = ParagraphStyle("toc2", fontName="Sans", fontSize=9, leading=12.5, textColor=MUTED, leftIndent=14)


def P(text, style="body"):
    return Paragraph(text, ss[style])


def bullets(items, style="bullet"):
    return [Paragraph(t, ss[style], bulletText="•") for t in items]


def steps(items):
    return [Paragraph(t, ss["num"], bulletText="%d." % (i + 1)) for i, t in enumerate(items)]


def note(text, title="Note"):
    t = Table([[Paragraph("<b>%s</b>  %s" % (title, text), ss["note"])]], colWidths=[170 * mm])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), NOTE_BG), ("LINEBEFORE", (0, 0), (0, -1), 3, ACCENT),
                           ("LEFTPADDING", (0, 0), (-1, -1), 9), ("RIGHTPADDING", (0, 0), (-1, -1), 9),
                           ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 7)]))
    return [Spacer(1, 3), t, Spacer(1, 8)]


def table(rows, widths, header=True, zebra=True):
    data = []
    for r, row in enumerate(rows):
        data.append([c if not isinstance(c, str) else Paragraph(c, ss["cellb" if header and r == 0 else "cell"]) for c in row])
    t = Table(data, colWidths=[w * mm for w in widths], repeatRows=1 if header else 0)
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.4, LINE),
             ("TOPPADDING", (0, 0), (-1, -1), 3.2), ("BOTTOMPADDING", (0, 0), (-1, -1), 3.2),
             ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4)]
    if header:
        style += [("BACKGROUND", (0, 0), (-1, 0), PAPER), ("LINEBELOW", (0, 0), (-1, 0), 0.9, INK)]
    if zebra:
        for r in range(1 if header else 0, len(data)):
            if r % 2 == 0:
                style.append(("BACKGROUND", (0, r), (-1, r), colors.HexColor("#fafaf8")))
    t.setStyle(TableStyle(style))
    return t


def shot(name, caption, width=170):
    path = os.path.join(IMG, name)
    if not os.path.exists(path):
        return [P("<i>[screenshot %s missing]</i>" % name, "small")]
    from PIL import Image as PILImage
    w, h = PILImage.open(path).size
    img = Image(path, width=width * mm, height=width * mm * h / w)
    frame = Table([[img]], colWidths=[width * mm + 2])
    frame.setStyle(TableStyle([("BOX", (0, 0), (-1, -1), 0.6, LINE), ("LEFTPADDING", (0, 0), (-1, -1), 1),
                               ("RIGHTPADDING", (0, 0), (-1, -1), 1), ("TOPPADDING", (0, 0), (-1, -1), 1),
                               ("BOTTOMPADDING", (0, 0), (-1, -1), 1)]))
    return [KeepTogether([frame, P(caption, "caption")])]


# ------------------------------------------------------------------------------------------ data from the source tree

def load_params():
    inc = open(os.path.join(ROOT, "helix-core", "include", "helix", "ParamList.inc")).read()
    rx = re.compile(r'HP\(\s*(\w+)\s*,\s*"([^"]*)",\s*"([^"]*)",\s*ParamKind::(\w+),\s*([-\d.]+)f,\s*([-\d.]+)f,\s*([-\d.]+)f,\s*"([^"]*)",\s*(nullptr|&choices::(\w+)),\s*(true|false)\)')
    params = []
    for m in rx.finditer(inc):
        params.append(dict(enum=m.group(1), id=m.group(2), name=m.group(3), kind=m.group(4), min=float(m.group(5)),
                           max=float(m.group(6)), dflt=float(m.group(7)), unit=m.group(8), choices=m.group(10),
                           mod=m.group(11) == "true"))
    return params


def load_choices():
    src = open(os.path.join(ROOT, "helix-core", "src", "Params.cpp")).read()
    out = {}
    for m in re.finditer(r'const std::vector<std::string> (\w+) = \{(.*?)\};', src, re.S):
        out[m.group(1)] = re.findall(r'"([^"]*)"', m.group(2))
    return out


PARAMS = load_params()
CHOICES = load_choices()
BYENUM = {p["enum"]: p for p in PARAMS}
MOD_DEST = ["None", "Pitch", "A Pitch", "B Pitch", "Amp", "Pan"] + [p["name"] for p in PARAMS if p["mod"]]


def load_mpc_pages():
    """The MPC page list, straight from the skin generator (helix-mpc/tools/make_skin.py)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("make_skin", os.path.join(ROOT, "helix-mpc", "tools", "make_skin.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.pages(), mod.FN_NAMES


MPC_PAGES, MPC_KEYS = load_mpc_pages()
PRESETS = []
for f in sorted(glob.glob(os.path.join(ROOT, "Presets", "*", "*.json"))):
    j = json.load(open(f))
    PRESETS.append(j)
ORDER = ["Bass", "Lead", "Pad", "Atmos", "Stab", "FX"]
PRESETS.sort(key=lambda j: (ORDER.index(j["category"]) if j["category"] in ORDER else 99, j["name"]))
assert len(PARAMS) > 300, "ParamList.inc parse failed"


def fmt_num(v):
    if abs(v - round(v)) < 1e-6:
        return "%d" % round(v)
    s = "%.3f" % v
    return s.rstrip("0").rstrip(".")


def fmt_value(p, v):
    if p["kind"] == "Bool":
        return "On" if v >= 0.5 else "Off"
    if p["kind"] == "Choice":
        lst = CHOICES.get(p["choices"], [])
        if p["choices"] == "modDest":
            lst = MOD_DEST
        i = int(round(v))
        return lst[i] if 0 <= i < len(lst) else str(i)
    u = p["unit"]
    if u == "Hz" and abs(v) >= 1000:
        return fmt_num(v / 1000) + " kHz"
    if u == "ms" and abs(v) >= 1000:
        return fmt_num(v / 1000) + " s"
    if not u and p["min"] >= -1 and p["max"] <= 1 and p["kind"] != "Int":
        return fmt_num(v * 100) + "%"
    return fmt_num(v) + ((" " + u) if u else "")


def fmt_range(p):
    if p["kind"] == "Bool":
        return "Off / On"
    if p["kind"] == "Choice":
        lst = MOD_DEST if p["choices"] == "modDest" else CHOICES.get(p["choices"], [])
        if len(lst) > 8:
            return "%d choices (see the lists)" % len(lst)
        return " · ".join(lst)
    lo, hi = fmt_value(p, p["min"]), fmt_value(p, p["max"])
    curve = {"Log": ", log", "Squared": ", fine at low end"}.get(p["kind"], "")
    return "%s … %s%s" % (lo, hi, curve)


# ------------------------------------------------------------------------------------------ diagrams

def signal_flow():
    W, H = 170 * mm, 84 * mm          # 482 x 238 pt
    d = Drawing(W, H)
    import math

    def box(x, y, w, h, label, sub=None, fill=colors.white):
        d.add(Rect(x, y, w, h, rx=5, ry=5, fillColor=fill, strokeColor=INK, strokeWidth=0.9))
        d.add(String(x + w / 2, y + h / 2 + (3 if sub else -3), label, fontName="Sans-Bold", fontSize=8.5,
                     fillColor=INK, textAnchor="middle"))
        if sub:
            d.add(String(x + w / 2, y + h / 2 - 8, sub, fontName="Sans", fontSize=6.2, fillColor=MUTED, textAnchor="middle"))

    def arrow(pts, col=INK, dash=None):
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            d.add(Line(x1, y1, x2, y2, strokeColor=col, strokeWidth=0.9, strokeDashArray=dash))
        (x1, y1), (x2, y2) = pts[-2], pts[-1]
        a = math.atan2(y2 - y1, x2 - x1)
        k = 4.5
        d.add(Polygon([x2, y2, x2 - k * math.cos(a - 0.4), y2 - k * math.sin(a - 0.4),
                       x2 - k * math.cos(a + 0.4), y2 - k * math.sin(a + 0.4)], fillColor=col, strokeColor=col))

    # sources (left column)
    sw, sh = 104, 30
    names = [("Osc A", "table · 2 warps · unison 1-7"), ("Osc B", "table · 2 warps · unison 1-7"),
             ("Sub", "sine / tri / square / saw"), ("Noise", "6 textures · loops")]
    ys = [H - 40 - i * 38 for i in range(4)]          # 198, 160, 122, 84
    for (n, sub), y in zip(names, ys):
        box(0, y, sw, sh, n, sub)
    # filters
    fx, fw = 152, 80
    f1y, f2y = 170, 92
    box(fx, f1y, fw, 32, "Filter 1", "18 models")
    box(fx, f2y, fw, 32, "Filter 2", "18 models")
    for y in ys:
        arrow([(sw, y + sh / 2), (fx, f1y + 16)], MUTED)
        arrow([(sw, y + sh / 2), (fx, f2y + 16)], MUTED)
    d.add(String(128, H - 6, "sends > F1 / > F2", fontName="Sans", fontSize=6.4, fillColor=MUTED, textAnchor="middle"))
    arrow([(fx + fw / 2, f1y), (fx + fw / 2, f2y + 32)], ACCENT)
    d.add(String(fx + fw / 2 + 4, 147, "serial", fontName="Sans", fontSize=6.4, fillColor=ACCENT))
    # blend, amp, fx
    by, bh = 131, 32
    box(262, by, 58, bh, "Ser/Par", "blend", fill=PAPER)
    arrow([(fx + fw, f1y + 16), (262, by + 22)])
    arrow([(fx + fw, f2y + 16), (262, by + 10)])
    box(340, by, 52, bh, "Amp", "Env 1 · vel")
    arrow([(320, by + 16), (340, by + 16)])
    box(412, by, 46, bh, "FX", "7 effects")
    arrow([(392, by + 16), (412, by + 16)])
    arrow([(458, by + 16), (W - 1, by + 16)])
    d.add(String(W - 2, by + 22, "out", fontName="Sans-Bold", fontSize=7, fillColor=INK, textAnchor="end"))
    # sub direct-out, routed under the filters
    arrow([(sw, ys[2] + 6), (132, 62), (366, 62), (366, by)], MOD, dash=[3, 2])
    d.add(String(250, 66, "Sub Direct: straight to the amp, past both filters", fontName="Sans", fontSize=6.4,
                 fillColor=MOD, textAnchor="middle"))
    # modulation strip
    d.add(Rect(0, 4, W, 26, rx=5, ry=5, fillColor=colors.HexColor("#eaf1fb"), strokeColor=MOD, strokeWidth=0.7))
    d.add(String(W / 2, 19.5, "Modulation: 4 envelopes · 4 LFOs · 2 performers · 8 macros · velocity, pressure, wheel, "
                 "bend, note, random", fontName="Sans", fontSize=6.8, fillColor=MOD, textAnchor="middle"))
    d.add(String(W / 2, 9.5, "→ 16-slot matrix → almost every knob", fontName="Sans-Bold", fontSize=6.8, fillColor=MOD,
                 textAnchor="middle"))
    return d


def envelope_diagram():
    W, H = 170 * mm, 30 * mm
    d = Drawing(W, H)
    x0, y0, h = 10, 6, H - 16
    pts = [(x0, y0), (x0 + 60, y0 + h), (x0 + 140, y0 + h * 0.55), (x0 + 320, y0 + h * 0.55), (x0 + 420, y0)]
    import math
    path = []
    for i in range(4):
        (ax, ay), (bx_, by) = pts[i], pts[i + 1]
        for k in range(21):
            t = k / 20
            if i == 1 or i == 3:
                e = (1 - math.exp(-4 * t)) / (1 - math.exp(-4))
            elif i == 0:
                e = 1 - (1 - t) ** 1.6
            else:
                e = t
            path.append((ax + (bx_ - ax) * t, ay + (by - ay) * e))
    for (a, b) in zip(path, path[1:]):
        d.add(Line(a[0], a[1], b[0], b[1], strokeColor=ACCENT, strokeWidth=1.6))
    for lab, x in [("Attack", 40), ("Decay", 110), ("Sustain (level)", 240), ("Release", 380)]:
        d.add(String(x0 + x, H - 6, lab, fontName="Sans-Bold", fontSize=7.5, fillColor=INK, textAnchor="middle"))
    d.add(Line(x0 + 320, y0, x0 + 320, y0 + h, strokeColor=LINE, strokeDashArray=[2, 2]))
    d.add(String(x0 + 322, y0 + 2, "key released", fontName="Sans", fontSize=6.5, fillColor=MUTED))
    return d


# ------------------------------------------------------------------------------------------ document template

class Doc(BaseDocTemplate):
    def __init__(self, path):
        super().__init__(path, pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm, topMargin=20 * mm,
                         bottomMargin=18 * mm, title="HELIX User Manual", author="Twisted Genetics",
                         subject="HELIX wavetable synthesizer for Akai MPC and desktop")
        frame = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, id="f")
        self.addPageTemplates([PageTemplate("cover", [frame], onPage=self.cover),
                               PageTemplate("body", [frame], onPage=self.decorate)])

    def cover(self, c, doc):
        w, h = A4
        c.saveState()
        c.setFillColor(PAPER)
        c.rect(0, 0, w, h, fill=1, stroke=0)
        c.setFillColor(ACCENT)
        c.rect(0, h - 14 * mm, w, 14 * mm, fill=1, stroke=0)
        # a stack of wavetable frames as cover art
        import math
        for k in range(26):
            t = k / 25
            ox, oy = 28 * mm + t * 40 * mm, 120 * mm + t * 52 * mm
            c.setStrokeColor(ACCENT if k == 17 else LINE)
            c.setLineWidth(2.2 if k == 17 else 0.8)
            p = c.beginPath()
            for i in range(121):
                ph = i / 120
                saw = 2 * ph - 1
                sine = math.sin(2 * math.pi * ph)
                v = sine + (saw - sine) * t
                x, y = ox + ph * 105 * mm, oy + v * 15 * mm
                p.moveTo(x, y) if i == 0 else p.lineTo(x, y)
            c.drawPath(p, stroke=1, fill=0)
        c.restoreState()

    def decorate(self, c, doc):
        w, h = A4
        c.saveState()
        c.setStrokeColor(LINE)
        c.setLineWidth(0.5)
        c.line(20 * mm, h - 13 * mm, w - 20 * mm, h - 13 * mm)
        c.setFont("Sans-Bold", 8)
        c.setFillColor(INK)
        c.drawString(20 * mm, h - 11 * mm, "HELIX")
        c.setFont("Sans", 8)
        c.setFillColor(MUTED)
        c.drawRightString(w - 20 * mm, h - 11 * mm, "User Manual · version %s" % VERSION)
        c.drawCentredString(w / 2, 10 * mm, str(doc.page))
        c.restoreState()

    def afterFlowable(self, f):
        if isinstance(f, Paragraph):
            st = f.style.name
            if st in ("h1", "h2"):
                text = f.getPlainText()
                key = "sec-%d" % id(f)
                self.canv.bookmarkPage(key)
                self.canv.addOutlineEntry(text, key, level=0 if st == "h1" else 1, closed=st == "h1")
                self.notify("TOCEntry", (0 if st == "h1" else 1, text, self.page, key))


def H1(t, need=60):
    # need: space (mm) the chapter's first block takes, so a heading never ends up alone at the bottom of a page
    return [CondPageBreak(need * mm), Paragraph(t, ss["h1"])]


def H2(t):
    return [CondPageBreak(30 * mm), Paragraph(t, ss["h2"])]


def H3(t):
    return Paragraph(t, ss["h3"])


# ------------------------------------------------------------------------------------------ content

def cover_page():
    title = ParagraphStyle("t", fontName="Sans-Bold", fontSize=44, leading=50, textColor=INK)
    sub = ParagraphStyle("s", fontName="Sans", fontSize=14, leading=20, textColor=INK)
    small = ParagraphStyle("m", fontName="Sans", fontSize=9.5, leading=14, textColor=MUTED)
    return [Spacer(1, 6 * mm), Paragraph("HELIX", title),
            Paragraph("Wavetable synthesizer for jungle, drum &amp; bass and dark music", sub), Spacer(1, 4 * mm),
            Paragraph("User Manual · version %s · %s" % (VERSION, date.today().strftime("%B %Y")), small),
            Spacer(1, 150 * mm),
            Paragraph("For Akai standalone MPCs (MPC OS 3.10.1 or later, 32-bit and 64-bit units, including the MPC One "
                      "and MPC One Retro) and for the desktop as VST3 / AU / Standalone.", small),
            Spacer(1, 3 * mm),
            Paragraph("Twisted Genetics · github.com/TwistedGenetics/Twisted-Helix", small),
            NextPageTemplate("body"), PageBreak()]


def contents():
    toc = TableOfContents()
    toc.levelStyles = [ss["toc1"], ss["toc2"]]
    toc.dotsMinLevel = 0
    return [Paragraph("Contents", ss["h1"]), toc, PageBreak()]


def chapter_welcome():
    s = H1("1  Welcome")
    s += [P("HELIX is a wavetable synthesizer built for the sounds of jungle and drum &amp; bass: Reese basses, hoovers, "
            "growls, neuro, wobbles, donks, and lush 90s pads. It takes its ideas from Serum and NI Massive: two wavetable "
            "oscillators that can be bent and warped, two filters you can run in series or in parallel, a deep modulation "
            "matrix, and a rack of effects."),
          P("It runs in two places from one sound engine:"),
          *bullets(["<b>On your MPC</b>, as a native plugin through inMusic's official MPC Plugin SDK. Play it from the pads, "
                    "the keys or the sequencer, turn it with the Q-Links, and it is saved with your project.",
                    "<b>On your computer</b>, as a VST3 / AU plugin or a standalone app, for example in Ableton Live. Presets move "
                    "freely between the two."]),
          P("This manual covers both. Where the MPC and the desktop differ, it says so.")]
    s += note("Version %s has been built and tested on computers and in inMusic's MPC host simulator, but has <b>not yet "
              "been played on a real MPC</b>. Save your MPC projects often while you try it: third-party plugins run inside "
              "the MPC software without protection, so a fault could restart it." % VERSION, "Before you start")
    s += H2("What's inside")
    s += [table([["Section", "In short"],
                 ["Oscillators", "2 wavetable oscillators (up to 256 frames each), 14 warp modes in two slots each, up to 7 unison voices"],
                 ["Sub and noise", "A clean sub oscillator that can bypass the filters, and 6 noise / vinyl / tape textures"],
                 ["Mod oscillator", "A Massive-style sine that ring-modulates, phase-modulates or sweeps the wavetables, and frequency-modulates the filters"],
                 ["Filters", "2 stereo filters: 13 HELIX types plus the 17 Mega Synth models, drive, key tracking and 'fat', serial ↔ parallel routing"],
                 ["Modulation", "4 envelopes, 4 LFOs, 2 step performers, 8 macros, a 16-slot matrix"],
                 ["Voices", "8 voices, poly / mono / legato, glide, click-free voice stealing"],
                 ["Effects", "Distortion, chorus / ensemble, phaser, delay, reverb, OTT, EQ, in any order"],
                 ["Wavetables", "8 built-in tables, Serum tables and single cycles, Sample → Table from your own audio"],
                 ["Presets", "24 factory sounds, your own presets, Mutate"]], [38, 132])]
    return s


def chapter_install():
    s = H1("2  Installing")
    s += H2("On the MPC")
    s += [P("You need an MPC running <b>MPC OS 3.10.1 or later</b>. The same download works on the 32-bit MPCs (MPC One, "
            "One+, One Retro, Live, Live II, X, X SE, Key 61, Key 37, Force) and the 64-bit ones (MPC Live III and later): "
            "the MPC picks the right version itself.")]
    s += steps(["Download <b>helix-%s-mpc.zip</b> from the Releases page of the HELIX repository on GitHub and unzip it on "
                "your computer." % VERSION,
                "On a USB stick, an SD card or the MPC's internal drive, make a folder named exactly <b>VST2 Plugins</b> at the "
                "top level (if it is not already there).",
                "Copy the folder <b>Twisted Genetics - VST - HELIX</b> into <b>VST2 Plugins</b>. Copy the whole folder, not "
                "just the files inside it.",
                "Put the drive in the MPC. Go to <b>Preferences → Enhancements (Beta)</b> and turn on <b>Enable "
                "Enhancements</b> and <b>External Plugin Support</b>. Read the warning the MPC shows.",
                "Make a plugin track (or plugin program) and open the plugin browser. HELIX is listed under <b>External "
                "Plugins</b>.",
                "To play HELIX from the pads, set the pads to a chromatic layout (for example <b>Pad Perform → "
                "Chromatic</b>), or play it from a keyboard or the sequencer."])
    s += [H3("Your own files on the MPC"),
          P("Inside the HELIX folder there is a <b>Content</b> folder with three sub-folders. Copy files there from your "
            "computer (or with the MPC's file browser):"),
          table([["Folder", "Put here", "Where it shows up"],
                 ["Content/Wavetables", "Serum-style wavetables, single-cycle WAVs, or folders of single cycles", "After the "
                  "8 built-in tables when you turn A Table / B Table"],
                 ["Content/Samples", "Any WAVs you want to turn into wavetables (breaks, vocals, one-shots)",
                  "The Sample control on the TABLES page under BROWSE (Sample → Table)"],
                 ["Content/Presets", "HELIX presets (.json), in sub-folders by category", "After the 24 factory presets "
                  "in the Preset list"]], [36, 70, 64])]
    s += [Spacer(1, 4), P("To update HELIX later, copy the new folder over the old one, then remove HELIX from the track and "
                          "add it again (restart the MPC if the screen layout changed). Your Content folder is part of the "
                          "plugin folder, so keep a copy of anything you put there.")]
    s += H2("On your computer")
    s += [P("The desktop version is a VST3 and AU plugin plus a standalone app. Each release includes macOS builds when "
            "they could be made; otherwise build it from the source (the README in the repository has the commands)."),
          table([["Format", "Where it goes (macOS)"],
                 ["VST3", "~/Library/Audio/Plug-Ins/VST3/HELIX.vst3"],
                 ["AU", "~/Library/Audio/Plug-Ins/Components/HELIX.component"],
                 ["Standalone", "Applications (HELIX.app)"]], [36, 134])]
    s += note("The macOS builds are not code-signed. If Ableton (or macOS) refuses to load HELIX, open Terminal and run "
              "<font name='Mono'>xattr -dr com.apple.quarantine</font> followed by the path of the plugin, then rescan "
              "plug-ins in Ableton.", "Tip")
    s += [P("On the computer, HELIX keeps your files in your Documents folder:"),
          *bullets(["<b>Documents/HELIX/Wavetables</b>: your wavetables (scanned when HELIX opens).",
                    "<b>Documents/HELIX/Presets/&lt;Category&gt;</b>: presets you save with <b>Save</b> at the top of the window."])]
    return s


def chapter_quickstart():
    s = H1("3  Quick start")
    s += steps(["Load HELIX on a track. You hear the <b>Init</b> sound: a single sine-to-square wavetable through an open "
                "filter.",
                "Pick a factory preset. On the MPC use the MPC's own preset menu (the 24 factory presets) or the <b>Preset</b> "
                "control and <b>Load</b> on the BROWSE page. On the computer, click the preset name at the top of the window (or "
                "use the &lt; and &gt; buttons next to it).",
                "Play a few low notes with <b>Classic Reese</b> and turn <b>Macro 1</b> (filter) and <b>Macro 2</b> (detune). "
                "Many presets put their most useful moves on the first macros, and the preset list in chapter 14 tells you "
                "what they do.",
                "Press <b>Mutate</b> (<b>Mutate 10%</b> on the MPC) a few times to hear variations of the sound, and save one you like.",
                "Turn <b>WT Pos</b> (wavetable position) on oscillator A to sweep through the table, then try a <b>Warp</b> mode."])
    s += H2("How HELIX makes a sound")
    s += [signal_flow(), P("Signal flow: every sound source has a send to each filter; the two filters meet in the Ser/Par blend; "
                           "the amp envelope shapes the result; then the effects.", "caption")]
    s += [P("Two oscillators, the sub and the noise are mixed into the two filters. Each source decides how much it sends "
            "to <b>Filter 1</b> and to <b>Filter 2</b>. The <b>Ser/Par</b> control decides whether the filters work side by "
            "side (parallel) or one after the other (serial). The sub oscillator can skip both filters (<b>Sub Direct</b>), "
            "which keeps a Reese's low end clean while you filter the top. Envelope 1 shapes the volume of every note. Then "
            "the effects run, in the order you choose, and the Master level sets the output.")]
    return s


def chapter_desktop():
    s = H1("4  The desktop window", need=120)
    s += shot("window.jpg", "The OSC tab with Neuro Reese. Top: oscillators A and B with their 3D wavetables, the two filters. "
                            "Middle: sub, noise and routing. Bottom: the modulation sources and the macros.")
    s += [P("On the computer, HELIX lives in one window. Everything that shapes the sound is on the <b>OSC</b> tab; the other "
            "tabs hold the effects, the full modulation matrix and the global settings."),
          table([["Part", "What's there"],
                 ["Top bar", "The preset name (click it for the preset menu, by category), &lt; and &gt; to step through "
                  "presets, <b>Save</b>, <b>Mutate</b>, <b>Init</b>, the <b>Master</b> level and the CPU readout (it adds "
                  "<b>OTT</b> while the OTT effect runs and <b>ECO1-4</b> when the CPU guard has lowered the quality)."],
                 ["OSC", "Oscillators A and B, Filter 1 and 2 with their response curves, sub, noise, routing (the sends "
                  "and the Par / Ser blend), the modulation sources (envelopes, LFOs, performers) and the macros."],
                 ["FX", "The effects rack: the order from top to bottom (^ and v move an effect), a lamp to switch each one "
                  "on, and the selected effect's controls."],
                 ["MATRIX", "All 16 modulation slots."],
                 ["GLOBAL", "Voices (poly mode, glide, stealing, bend range, velocity), Quality, CPU Guard, the colour "
                  "theme, the CPU used by each voice, and the preset list."]], [26, 144])]
    s += H2("Knobs")
    s += [*bullets(["Drag up / down (or left / right) to turn a knob. Hold the mouse over it to read its value; the name "
                    "under the knob changes to the value.",
                    "Double-click a knob to return it to its starting value.",
                    "Right-click a knob for its menu: <b>Reset to default</b>, and for each modulation aimed at it: change the "
                    "amount or <b>Remove</b> it.",
                    "Every control can be automated in your DAW, and every knob with a modulation destination can be "
                    "modulated."])]
    s += H2("Modulation by drag and drop")
    s += [P("Along the top of the modulation area are the <b>source chips</b>: ENV 1-4, LFO 1-4, PERF 1-2, and VEL (velocity), "
            "PRS (pressure), AT (channel aftertouch), MW (mod wheel), NOTE and RND (random). The macros have chips too "
            "(M1-M8). Click an ENV, LFO or PERF chip to see and edit that source below."),
          *steps(["Drag a chip (grab it anywhere) onto a knob. The knob lights up in the modulation colour while you are over "
                  "a knob that can be modulated.",
                  "Let go. HELIX puts the route in the first free matrix slot with an amount of 25%.",
                  "An outer ring on the knob shows how far the modulation can move it; while a note plays, a dot shows "
                  "where it is right now.",
                  "Right-click the knob to change the amount or remove the route, or fine-tune it on the MATRIX tab."])]
    s += H2("The wavetable display")
    s += [P("Each oscillator shows its wavetable in <b>3D</b>: every frame of the table stacked in perspective, from the first "
            "frame at the front to the last at the back. The lit frame is where <b>WT Pos</b> is (including modulation, "
            "while a note plays), and it is drawn through the oscillator's warps, so you see what Bend, PWM, Sync and the "
            "other shape warps do. Click <b>3D</b> in the corner to switch to a large <b>2D</b> view of the current "
            "frame. The &lt; &gt; buttons and the menu in the title bar choose the table; <b>…</b> loads WAVs and makes tables "
            "from samples.")]
    s += H2("Themes and size")
    s += [P("HELIX has three colour themes: <b>Massive</b> (navy panels in light grey frames, white knob caps, the default "
            "from version 0.3, matching the MPC screen), <b>Copper</b> (dark charcoal) and <b>Paper</b> (light, dark text). "
            "Choose one on the GLOBAL tab. Drag the bottom-right corner of the window to make it bigger or "
            "smaller; HELIX remembers the theme and the size.")]
    s += shot("paper.jpg", "The Paper theme (90s Pad).", 150)
    return s


def chapter_osc():
    s = H1("5  The oscillators", need=160)
    s += shot("oscA.jpg", "Oscillator A on the computer (Hoover 91): the 3D view shows every frame of the table; the lit frame is "
                          "where WT Pos is, drawn through the warp (here PWM).", 120)
    s += [P("HELIX has two identical wavetable oscillators, A and B. A wavetable is a stack of single-cycle waveforms called "
            "<b>frames</b>. <b>WT Pos</b> (wavetable position) chooses where in the stack you are; HELIX blends smoothly "
            "between neighbouring frames, so sweeping the position (by hand or with modulation) morphs the sound.")]
    s += H2("Table, position, level")
    s += [table([["Control", "What it does"],
                 ["Osc A / Osc B", "Turns the oscillator on or off. A new preset starts with A on and B off."],
                 ["Table", "Chooses the wavetable: first the 8 built-in tables, then the ones in your Wavetables folder. "
                  "The control wraps around, so every position names a table."],
                 ["WT Pos", "Position in the table, 0% = first frame, 100% = last frame."],
                 ["Level / Pan", "Volume and stereo position of the oscillator."],
                 ["Octave / Semi / Fine", "Tuning: ±4 octaves, ±24 semitones, ±100 cents."],
                 ["Phase / Rnd Phase", "Start point of the waveform at each note, and how much of it is random. 100% random "
                  "phase (the default) makes each note sound a little different, like an analogue synth; 0% makes every note "
                  "start the same, which gives tighter, punchier basses."],
                 ["&gt; F1 / &gt; F2", "How much of this oscillator goes to Filter 1 and to Filter 2."]], [36, 134])]
    s += H2("Warp modes")
    s += [P("Each oscillator has two warp slots (<b>Warp 1</b> and <b>Warp 2</b>) with an amount each. Warps reshape the "
            "waveform as it plays; the two slots run one after the other, so you can combine them. All amounts can be "
            "modulated.")]
    warps = [["Warp", "What it does", "Good for"],
             ["Bend + / Bend -", "Squeezes the cycle towards the middle (+) or towards the edges (-).", "Brighter / hollower "
              "tones from any table"],
             ["Asym", "Pushes the waveform's energy towards the end of the cycle.", "Nasal, reedy shapes"],
             ["PWM", "Pulse-width modulation for any waveform: the first half of the cycle gets shorter (down to 2%).",
              "Classic PWM pads, hoovers"],
             ["Mirror", "Plays the cycle forwards then backwards.", "Hollow, octave-up character"],
             ["Sync", "Hard sync (up to 16× the pitch) with a smoothing window, so it stays clean.", "Screaming leads, "
              "neuro movement"],
             ["Formant", "Squeezes the whole cycle into the first part of the period and leaves silence after it: the "
              "pitch stays, the formant moves up.", "Vocal, talking basses"],
             ["FM Other", "Frequency modulation from the other oscillator (A from B, B from A).", "Metallic and neuro basses"],
             ["FM Sub / FM Noise", "FM from the sub oscillator / from the noise source.", "Growl, grit, breath"],
             ["AM / RM", "Amplitude / ring modulation by the other oscillator.", "Bell, robot and dirty tones"],
             ["Quantize", "Steps the waveform's playback position (2048 down to 4 steps).", "Old-sampler grit"],
             ["Bit Red", "Reduces the bit depth of the oscillator (down to 1 bit).", "Lo-fi, jungle crunch"]]
    s += [table(warps, [30, 92, 48])]
    s += note("HELIX keeps warps band-limited where it can, and automatically limits a warp's amount on very high notes so "
              "it doesn't fold over into harsh aliasing. Bend, Asym, PWM and Mirror can still add a little aliasing on very "
              "high notes; switch <b>Quality</b> to <b>High</b> (GLOBAL tab; VOICE page under BROWSE on the MPC) for leads, which runs the oscillators at "
              "double rate.", "Clean warps")
    s += H2("Unison")
    s += [table([["Control", "What it does"],
                 ["Unison", "1 to 7 copies of the oscillator per note."],
                 ["Detune", "Spreads the copies' pitch, up to about ±50 cents at 100%. Small amounts (10-25%) give the slow "
                  "beating of a Reese; large amounts give supersaws and hoovers."],
                 ["Blend", "Level of the outer copies against the centre one."],
                 ["Spread", "Stereo width of the copies."]], [36, 134])]
    s += [P("Unison is what makes HELIX's basses and pads big, and it is also what costs the most CPU. On a busy MPC "
            "project, 2-4 unison voices usually give 90% of the effect.")]
    return s


def chapter_tables():
    s = H1("6  Wavetables")
    s += H2("Built-in tables")
    s += [table([["Table", "Frames", "What's in it"],
                 ["Basic Shapes", "64", "Sine → triangle → saw → square (0%, 33%, 67%, 100%)"],
                 ["Harmonic Sweep", "64", "From 1 harmonic to a full saw: a filter sweep without a filter"],
                 ["PWM Sweep", "64", "Pulse wave, 50% down to 2% width"],
                 ["Vowels", "64", "Sung vowels A → E → I → O → U"],
                 ["Hoover", "64", "Phase-modulated saws with a sub dip: the 'Mentasm' family"],
                 ["Growl", "64", "Vowels pushed through rising sync and saturation"],
                 ["Sync Sweep", "64", "Hard-synced saw, ratio 1 to 8"],
                 ["Digital Grit", "32", "Sine → saw at falling bit depth and sample rate (SP-1200 / S950 flavour)"]],
                [36, 18, 116])]
    s += H2("Your own wavetables")
    s += [P("Put WAV files in your Wavetables folder (on the MPC: <b>Content/Wavetables</b>; on the computer: "
            "<b>Documents/HELIX/Wavetables</b>). HELIX reads them like this:"),
          table([["What you put there", "How HELIX reads it"],
                 ["A Serum wavetable", "The frame size comes from the file's 'clm' information (usually 2048 samples); up to "
                  "256 frames."],
                 ["A WAV whose length is a multiple of 2048 samples", "Frames of 2048 samples, one after the other (the "
                  "common wavetable format)."],
                 ["A short WAV (up to 4096 samples)", "One single cycle, stretched to fit."],
                 ["A folder of short WAVs", "One table: each file is one frame, in alphabetical order. Name the files "
                  "01.wav, 02.wav… to control the order."],
                 ["Any longer WAV (a break, a vocal)", "Turned into a 64-frame table with Sample → Table (below)."]], [58, 112]),
          Spacer(1, 4),
          P("HELIX reads 8, 16, 24 and 32-bit PCM and 32/64-bit float WAVs, mono or stereo (stereo files are mixed to mono). "
            "Tables load in the background, so the sound never stops while you browse.")]
    s += H2("Sample → Table")
    s += [P("Sample → Table cuts any sample into a wavetable. HELIX finds the pitch of the sample, then takes one cycle at "
            "a time from evenly spaced points through the sample, lines each cycle up on a zero crossing so it loops without "
            "a click, and stacks the cycles as frames. Sweeping <b>WT Pos</b> then plays through the sample's changing tone: "
            "a vocal becomes a talking table, a break becomes a gritty, evolving one. Unpitched audio (drums, noise) is cut "
            "into plain slices instead."),
          H3("On the MPC"),
          *steps(["Copy WAVs into <b>Content/Samples</b>.",
                  "On the TABLES page (under BROWSE), choose the file with <b>Sample</b>, the number of frames with <b>Frames</b> (8-256) and "
                  "the oscillator with <b>To Osc</b>.",
                  "Press <b>Make Table</b>."]),
          H3("On the computer"),
          P("Click <b>…</b> in an oscillator's title bar and choose <b>Sample &gt; Table</b> with the number of frames "
            "(8-256), then pick a WAV. <b>Load WAV as wavetable</b> loads a WAV as a table instead. The same menu opens your "
            "Wavetables folder and rescans it."),
          P("Tables made with Sample → Table are remembered in your project and presets as long as the original file is "
            "still in the same place.")]
    s += H2("Print to wavetable")
    s += [P("Print renders the sound you are playing back into a new wavetable: neuro resampling without leaving HELIX. "
            "It holds one note, sweeps one control from its lowest to its highest setting while the note plays, and takes "
            "a cycle at evenly spaced moments as the frames. Everything that shapes the sound goes in: both oscillators, "
            "unison, Reese drift, the filters, the mod oscillator, envelopes, LFOs, performers and (if you like) the "
            "effects. Load the result into an oscillator and scan it with <b>WT Pos</b>, push it through the warps and "
            "filters again, and print again: that is how the layered, re-resampled neuro basses are made."),
          P("The note is rendered at a sample rate chosen so one cycle is exactly 2048 samples, so every frame is a whole, "
            "phase-aligned cycle and the table plays cleanly at any pitch."),
          table([["Control", "What it does"],
                 ["Sweep", "What moves from 0 to 100% during the print: <b>Time</b> (nothing: the patch's own envelopes, "
                  "LFOs and performers make the changes), Macro 1-4, Growl, Pos A, Pos B, Cutoff 1 or Warp A (Osc A's "
                  "first warp amount)."],
                 ["Length", "How long the note is held (0.25-8 s). Longer catches more of slow LFOs and envelopes."],
                 ["Note", "The note held, C1 to F#2. Key tracking and pitch envelopes follow it; C2 suits basses."],
                 ["Frames", "8 to 256 frames, spread evenly over the length."],
                 ["To Osc", "Which oscillator gets the new table."],
                 ["Through FX", "On: the insert and FX slots (distortion, OTT ...) are printed too. Off: the dry voices only."],
                 ["Print / Made", "Press Print. It takes a second or two (longer on the MPC); <b>Made</b> then shows the "
                  "new table's name, or what went wrong."]], [32, 138]),
          P("Prints are saved as <b>Wavetables/Prints/Print 001.wav</b>, 002 and so on (on the MPC in Content/Wavetables; "
            "on the computer in Documents/HELIX/Wavetables), as Serum-compatible tables, so they stay in the table list "
            "and your presets find them again. They load into Serum and other wavetable synths as well."),
          H3("On the MPC"),
          P("The <b>PRINT</b> strips on the TABLES page (BROWSE key)."),
          H3("On the computer"),
          P("Click <b>…</b> in the oscillator's title bar, open <b>Print patch to table</b>, set Frames, Length, Note and "
            "Through the effects, then choose <b>Print, sweeping</b> and the control. The table goes into that oscillator."),
          P("Tip: a Reese with Growl on a slow LFO, printed with Sweep: Time over 4 s, gives a table that growls on its own "
            "when you sweep its position with a fast envelope or the performer.")]
    return s


def chapter_subnoise():
    s = H1("7  Sub, noise and the mod oscillator", need=135)
    s += shot("subnoise.jpg", "Sub, noise and routing on the OSC tab (Dark Atmos).")
    s += H2("Sub oscillator")
    s += [table([["Control", "What it does"],
                 ["Sub", "On / off."],
                 ["Sub Shape", "Sine, Triangle, Square or Saw."],
                 ["Sub Octave", "One or two octaves below the note."],
                 ["Sub Level / Sub Pan", "Volume and position."],
                 ["Sub Direct", "On (the default): the sub goes straight to the output, past both filters, so filtering "
                  "and filter drive never touch your low end. Off: it goes through the filters using the sends below."],
                 ["Sub &gt; F1 / &gt; F2", "Sends to the filters (only when Sub Direct is off)."]], [36, 134])]
    s += H2("Noise")
    s += [table([["Control", "What it does"],
                 ["Noise Type", "White, Pink, Brown, Vinyl (rumble, hiss and crackle), Tape Hiss (with flutter), Crackle "
                  "(clicks only)."],
                 ["Noise Level / Pan", "Volume and position."],
                 ["Noise Track", "On: the noise plays faster on higher notes (pitch tracking). Off: always the same."],
                 ["Noise Loop", "On: the texture loops for as long as the note lasts, starting at a random point each time. "
                  "Off: it plays once from the start (useful for clicks and transients)."],
                 ["Noise &gt; F1 / &gt; F2", "Sends to the filters."]], [36, 134])]
    s += H2("Reese")
    s += [P("A Reese is two (or more) saws detuned against each other; what makes the classic jungle ones move is that the "
            "detune never sits still. The <b>REESE</b> page (OSC key; GLOBAL tab on the computer) adds three things:"),
          table([["Control", "What it does"],
                 ["Drift / Drift Rate", "Osc A and Osc B pull apart and back by up to 25 cents (Drift), wandering at Drift "
                  "Rate, so the beating speeds up and slows down by itself. Each note wanders its own way. Turn Osc B on "
                  "(a saw table, same octave) to hear it; with unison as well it gets huge."],
                 ["Growl", "One knob, and a matrix destination, that moves Filter 1's cutoff, its drive, both wavetable "
                  "positions and both first warps together, by the four <b>Growl</b> amounts below it (Cutoff in octaves, "
                  "Drive, Position, Warp). Put an LFO or the performer on Growl for talking, wobbling neuro."],
                 ["Phase Lock", "Every note starts both oscillators and all unison voices at the same phase, so every hit "
                  "has the same punch (the random phase spread is skipped). Off: each note starts differently, which is "
                  "warmer for pads."]], [32, 138])]
    s += H2("Character oscillator")
    s += [P("A third sound source in every voice that plays one of Twisted Genetics' own bass and rave engines, the same "
            "DSP as the microKORG 2 / NTS-1 units <b>Hoover</b>, <b>Dread Bass</b> and <b>Rollers</b>. Each is a "
            "finished sound on its own, so the character oscillator is the quickest way to a hoover, a donk or a rolling "
            "sub; layer it under the wavetables or use it alone (switch Osc A off). On the MPC it is the <b>CHAR OSC</b> "
            "page (OSC key); on the computer the <b>Character Osc</b> panel on the GLOBAL tab."),
          table([["Engine", "Sound"],
                 ["Hoover, Mentasm, Hoover Reese, Rave", "The Hoover unit: a stack of PWM-saws (the Alpha Juno trick) with "
                  "a pitch dive on each note, chorus smear and drift. Mentasm adds an octave-down layer, Rave an octave-up "
                  "scream, Hoover Reese is two plain beating saws."],
                 ["Dread, Donk, Growl, Dread Hoover", "Dread Bass: Dread is a 303-style square played backwards (the "
                  "filter sweeps open), Donk a resonance ringing on a harmonic, Growl 2-op FM, Dread Hoover two detuned "
                  "pulses."],
                 ["Rollers Sub, Reese, Sub+Reese, Hoover", "Rollers: a clean sine sub and/or detuned drifting saws; drive "
                  "and crunch only ever touch the top, so the sub stays clean."]], [44, 126]),
          table([["Control", "What it does"],
                 ["Engine", "Off or one of the twelve engines above."],
                 ["Level / Semi / Pan", "Volume, transpose in semitones, position."],
                 ["Timbre, Character, Drive, Sub", "The engine's four sound knobs; their names change with the engine "
                  "(Hoover: Detune, PWM, Drive, Sub. Dread: Sweep, Reso, Crunch, Sub. Donk: Harmonic, Reso ... Rollers: "
                  "Detune, Blend, Drive, Crunch). Timbre, Character and Drive (and Level) are matrix destinations."],
                 ["Env / Time", "The engine's pitch envelope: Hoover dive (up to two octaves), Dread Bass pitch envelope "
                  "(-24 to +48 semitones), Rollers pitch drop; Time is how long it takes."],
                 ["To Filter 1 / 2", "Sends into the filters (Filter 1 at 100% by default). Both at 0: silent."]], [36, 134]),
          P("The character oscillator follows glide and pitch bend and goes through HELIX's amp envelope, filters and "
            "effects like the other sources.")]
    s += H2("Chord memory")
    s += [P("<b>Chord</b> (VOICE and CHAR OSC pages on the MPC, the Voices panel on the computer) makes every key play a chord: Octave, "
            "Fifth, Power, Minor, Major, Minor 7, Major 7, Sus 4, Minor 9, Minor 11, Rave Stab (minor with octave and "
            "tenth), Dub Stab and Hoover Oct (an octave below and above). One finger plays rave stabs; with the "
            "Hoover engine it is the classic hoover chord. Chord works in Poly mode; each note keeps the chord it "
            "started with, so changing Chord while holding keys never leaves notes hanging. Pads count as keys, so a "
            "chord uses up to six voices per pad: raise Voices if stabs cut each other off.")]
    s += H2("Modulation oscillator")
    s += [P("The mod oscillator works like Massive's. It is a sine wave you never hear directly. It plays at the "
            "note's pitch plus <b>Pitch</b> and follows glide and pitch bend. It changes the other sounds in four "
            "ways, each with its own target and amount. On the MPC it is the <b>MOD OSC</b> page (OSC key). On the "
            "computer, press <b>MOD OSC</b> at the right of the modulation panel's source buttons."),
          table([["Control", "What it does"],
                 ["Mod Osc", "On / off. Off (the default) changes nothing, so older presets sound the same."],
                 ["Pitch", "Semitones above or below the note (-48 to +48). Whole numbers give harmonic tones; 7, 12 "
                  "and 19 are classic. Small offsets give slow beating and movement."],
                 ["Ring Mod + Amount", "Multiplies Osc A, Osc B or both by the sine. Amount blends from the dry oscillator "
                  "(0%) to full ring modulation (100%): bells, metal, talking basses."],
                 ["Phase + Amount", "Phase modulation (FM-style) of the chosen oscillators' wavetable reading. Up to one "
                  "cycle each way at 100%: from a little bite to harsh FM."],
                 ["Position + Amount", "Sweeps the wavetable position up and down at audio rate, around the oscillator's "
                  "own Position (up to half the table each way): buzzy, vocal, growling tones."],
                 ["Filter FM + Amount", "Moves Filter 1, Filter 2 or both up and down at audio rate (up to 4 octaves each "
                  "way at 100%). With resonance it gives screaming, gritty neuro tones."]], [36, 134]),
          P("Every Amount and Pitch can also be modulated from the matrix, e.g. an envelope on Phase Amt for an FM "
            "attack. Filter FM costs extra CPU while it is on, because the filter is recalculated every 4 samples.")]
    return s


def chapter_filters():
    s = H1("8  Filters and routing", need=135)
    s += shot("filters.jpg", "The two filters (Neuro Reese: a ladder filter feeding a formant filter). The curves are measured from "
                             "the filters themselves.", 105)
    s += [P("HELIX has two stereo filters. Each has the same controls:"),
          table([["Control", "What it does"],
                 ["Filter 1 / Filter 2", "On / off. A filter that is off passes its input straight through."],
                 ["Type", "The filter type (see below)."],
                 ["Cutoff", "Cutoff frequency, 20 Hz to 20 kHz."],
                 ["Reso", "Resonance. Near 100% the SVF and ladder types start to whistle on their own."],
                 ["Drive", "Saturation before the filter: from warmth to heavy grit."],
                 ["Key Trk", "Key tracking: at 100% the cutoff follows the notes you play (an octave up = cutoff doubled)."],
                 ["Fat", "Adds saturated body after the filter, Massive-style."],
                 ["Level", "Output level of the filter."]], [36, 134])]
    s += H2("Filter types")
    s += [table([["Type", "Sound"],
                 ["LP 12 / LP 24", "Low-pass, gentle or steep. The basic synth filter."],
                 ["BP 12 / BP 24", "Band-pass: keeps a band around the cutoff."],
                 ["HP 12 / HP 24", "High-pass: removes the lows."],
                 ["Notch 12 / Notch 24", "Removes a band around the cutoff."],
                 ["Ladder 24", "Four-pole ladder low-pass with built-in saturation: thick and aggressive at high resonance."],
                 ["Comb + / Comb -", "Comb filter: the cutoff sets the comb's pitch, Reso its feedback. Metallic, flanged, "
                  "pitched-noise tones."],
                 ["Formant", "Three vowel resonances. The cutoff morphs the vowel from A (low) through E, I, O to U (high). "
                  "Modulate it for talking and growl basses."],
                 ["Phaser", "Six all-pass stages with a notch at the cutoff; Reso sets the feedback."]], [36, 134])]
    s += H2("Filter models (from Mega Synth)")
    s += [P("Each filter has a <b>Model</b>. <b>HELIX</b> (the default) is the filter described above, chosen with "
            "<b>Type</b>. The other 17 models are the filters from Mega Synth. With one of those, <b>Type</b> is not used; "
            "the model is set with <b>Filter Type</b> (Low Pass, High Pass, Band Pass, Notch, Peak, All Pass) and "
            "<b>Slope</b> (6, 12, 18 or 24 dB, or <b>Model</b>: the model's own slope). Presets made before 0.3.1 use "
            "HELIX and sound exactly as before. On the MPC the screen on the FILTER page lists every choice: drag it through "
            "the 13 HELIX types and then each model with each filter type (e.g. <b>TB-303 BP</b>); <b>Slope</b> is in "
            "the MORE row. The <b>MODEL</b> page has the same settings as buttons. On the computer "
            "they are the three menus above each filter's knobs."),
          P("A model on Low Pass at its own slope is the original circuit model (its own stages, ladder and "
            "saturation). Any other type or slope runs a clean multimode filter that takes on the model's character: its "
            "drive stage, how strong the resonance is and how gritty it gets. On the MPC the Mega models use about three "
            "times the CPU of the HELIX filter."),
          table([["Model", "Character"],
                 ["Standard LP", "One clean 12 dB stage."],
                 ["Sallen-Key", "Two soft 12 dB stages: smooth, rounded 24 dB."],
                 ["Ladder", "Four-pole ladder: thick, self-oscillates at high Reso."],
                 ["Steiner-Parker", "High pass, band pass and low pass together: nasal and buzzy."],
                 ["State Var", "Band pass into low pass: bright, vocal resonance."],
                 ["OTA", "A saturating input stage into two low-pass stages: punchy."],
                 ["Polivoks", "Russian classic: hard, screaming resonance with a saturator after the filter."],
                 ["Sw Cap", "Switched-capacitor style with a notch between low-pass stages: hollow, digital edge."],
                 ["Acid", "Heavily driven two-stage low pass with limiting: squelch."],
                 ["MS-20", "High pass into a sharp low pass: aggressive, whistling resonance."],
                 ["Oberheim", "Wide, open low pass with a gentle low cut."],
                 ["SEM", "Soft 12 dB with a band-pass edge: smooth and musical."],
                 ["TB-303", "The 303 ladder: resonance fades at low cutoff, so the filter closes right down."],
                 ["Fat Cat", "Minimoog-style ladder with spread stages: fat low end."],
                 ["Odyssey", "ARP Odyssey style: high pass into a resonant low pass."],
                 ["CS-15", "Yamaha CS-15 style three-stage: thin, buzzy."],
                 ["SH-2", "Roland SH-2 style two-stage 24 dB: warm and solid."]], [36, 134]),
          P("With a Mega model, <b>Fat</b> becomes the Mega Synth's analogue <b>Warmth</b>: a softer, slightly "
            "asymmetric drive.")]
    s += H2("Routing: sends and Ser/Par")
    s += [P("Every source (Osc A, Osc B, Sub, Noise) has a <b>&gt; F1</b> and a <b>&gt; F2</b> send. The <b>Ser/Par</b> "
            "control then decides how the filters meet:"),
          *bullets(["<b>0% (parallel)</b>: Filter 1 and Filter 2 each filter their own input and are added together. Use it "
                    "for two different layers, e.g. a low-passed body on F1 and a band-passed top on F2.",
                    "<b>100% (serial)</b>: Filter 1's output also feeds Filter 2 and only Filter 2 is heard. Use it to stack "
                    "filters, e.g. a ladder into a formant for neuro basses.",
                    "<b>In between</b>: a mix of both."])]
    return s


def chapter_env():
    s = H1("9  Envelopes")
    s += [envelope_diagram(), P("An ADSR envelope.", "caption")]
    s += [P("HELIX has four ADSR envelopes. <b>Envelope 1 always controls the volume</b> of each note; envelopes 2-4 are free "
            "for anything in the modulation matrix (filter sweeps, pitch drops, wavetable movement)."),
          table([["Control", "Range", "What it does"],
                 ["Attack", "0.5 ms - 20 s", "Time to rise to full level."],
                 ["Decay", "1 ms - 20 s", "Time to fall to the sustain level."],
                 ["Sustain", "0 - 100%", "Level held while the key is down."],
                 ["Release", "1 ms - 20 s", "Time to fade out after the key is released."],
                 ["A / D / R Curve", "-100% … +100%", "Shape of each stage. For Decay and Release: negative = fast drop then "
                  "a long tail (snappy, percussive); positive = almost straight lines. For Attack: positive = straight rise, "
                  "negative = slow start then a quick swell."]], [30, 32, 108])]
    s += shot("env.jpg", "Envelope 2 of the Donk preset: a fast pitch drop. The curve is drawn by the engine's own envelope.")
    s += [P("<b>Vel Sens</b> (GLOBAL tab; VOICE page under BROWSE on the MPC) sets how much the velocity of each note changes its volume (0% = every "
            "note at full volume).")]
    return s


def chapter_lfo():
    s = H1("10  LFOs and performers", need=135)
    s += shot("lfo.jpg", "LFO 1 of Moving Reese: a triangle over one bar.")
    s += H2("LFOs")
    s += [P("Four LFOs, each with these controls:"),
          table([["Control", "What it does"],
                 ["Shape", "Sine, Triangle, Saw Up, Saw Down, Square, S&amp;H (random steps), Smooth Rnd (random glides), "
                  "Breakpoint (a drawn shape) and Steps (a step sequence)."],
                 ["Mode", "<b>Retrig</b>: restarts with every note. <b>Free</b>: runs continuously and is shared by all "
                  "notes (with Sync on, it locks to the song position). <b>One-shot</b>: plays one cycle per note, then "
                  "holds."],
                 ["Sync / Rate / Div", "With Sync on, the speed is a note value (8 bars down to 1/64, with dotted and "
                  "triplet values) following the MPC's or your DAW's tempo. With Sync off, Rate is free, 0.01-40 Hz."],
                 ["Rise", "Fades the LFO in over up to 5 seconds after each note starts (delayed vibrato)."],
                 ["Smooth", "Rounds off sharp edges (makes S&amp;H and Square softer)."],
                 ["Phase", "Start point of the cycle."],
                 ["Steps", "Number of steps (1-16) used by the Steps shape."],
                 ["Jump", "Rate jump: while the <b>Jump Macro</b> (one for all four LFOs, Macro 4 by default) is past half "
                  "way, the LFO runs x2, x3, x4 or x8 faster, or x1/2 as fast. Synced LFOs stay locked to the beat, so a "
                  "1/4 wobble jumps straight to 1/16 on the grid: put Jump on the wobble LFO and ride the macro (or map it "
                  "to a pad) for the classic jump-up wobble switch."]], [36, 134]),
          P("On the MPC, Steps and Jump are on the <b>STEPS/JUMP</b> page (LFO key); on the computer, Jump and Jump Macro "
            "are the menus at the right of each LFO page."),
          Spacer(1, 4),
          P("The breakpoint shapes and step values come with presets. On the computer they are saved with your presets and "
            "projects; on the MPC they are loaded with presets (there is no editor for them on the MPC screen).")]
    s += H2("Performers")
    s += shot("perf.jpg", "Performer 1 of Rolling Reese: 1/16 steps with Exp (pluck) shapes rolling the filter.")
    s += [P("A performer is a step sequencer for modulation, like Massive's: up to 16 steps, each with its own value "
            "(-100% … +100%) and its own shape, always in time with the music. Use them for rolling basses, gated pads and "
            "rhythmic wobbles."),
          table([["Control", "What it does"],
                 ["Step (P1 Step)", "Length of each step as a note value (1/16 by default)."],
                 ["Length", "Number of steps, 1-16."],
                 ["Mode", "Retrig (start with each note), Free (follow the song position) or One-shot (play once, hold the "
                  "last step)."],
                 ["Swing", "Delays every second step: 0% is straight, 50% a light shuffle, 100% a hard 75/25 swing. "
                  "Each pair of steps keeps its length, so the pattern stays on the bar."],
                 ["Feel", "<b>Normal</b>; <b>Half-time</b> (half speed: a 1/16 pattern plays as 1/8, the half-time "
                  "drop); <b>Double</b>; <b>Triplet</b> (1.5 x as fast)."],
                 ["Fill", "On the last pass of every 2 or every 4 (with a one-bar pattern: the end of each 2- or 4-bar "
                  "phrase) the last quarter of the steps rolls x4, keeping their levels: a built-in fill. Off steps stay off."],
                 ["Step 1-16", "The level of each step, -100% … +100% (new steps start at 100%)."],
                 ["Curve 1-16", "The curve each step draws, chosen from the library below and scaled by the step's level."]],
                [36, 134]),
          H3("The curve library"),
          P("Like Massive, every step plays a curve from a library of 28. The first six are the shapes HELIX always had, "
            "so older presets play exactly as before. <b>Smooth</b> is special: it glides from this step's level to the "
            "next step's level."),
          table([["Curves", "What they do"],
                 ["Hold, Off", "Flat at the step level; silent."],
                 ["Ramp Up / Down, Exp Up, Log Up / Down, S Up / Down", "Rises and falls with different curves: linear, "
                  "slow-then-fast, fast-then-slow, and S-shaped."],
                 ["Smooth, Triangle, Sine", "Glides and rounded bumps."],
                 ["Exp, Bounce", "Plucks: a fast hit that decays; Bounce also bounces."],
                 ["Gate 1/4, 1/2, 3/4", "On for part of the step, then off: tight rhythmic gating."],
                 ["Saw x2, Saw Dn x2, Tri x2, Sine x2, Pulse x2", "Two cycles inside one step: double-time rolls."],
                 ["Stairs Up / Dn", "Four steps up or down inside the step."],
                 ["Roll x2, x3, x4, x8", "Repeated plucks inside one step: drum-roll ratchets for the filter or the "
                  "amp, from a double hit to a buzz roll."]], [52, 118]),
          P("<b>On the MPC</b>: the <b>P1 DRAW</b> and <b>P2 DRAW</b> pages (PERF key) work like Massive's performer. The "
            "top two rows are the 16 steps as bars: drag a bar up or down to draw its level (the middle is zero). Tap "
            "the number and curve under a bar (or just drag the bar) to choose that step: it lights up, and tapping a "
            "curve in the library puts it on that step. <b>Step Level</b> sets the chosen step's level with a knob. Step "
            "rate, length, mode, Swing, Feel and Fill are on the <b>PERF</b> page."),
          P("<b>On the computer</b>: the library sits beside the performer display. Drag a curve onto a step, or click a "
            "curve and then click steps in the lane under the display to paint it. Draw the levels by dragging across the "
            "display: every step you pass sets its level to where the pointer is; "
            "right-click a step for a menu of all curves. Swing is the knob beside Length, Feel and Fill the menus next to it."),
          P("<b>Longer patterns</b>: Length counts steps, Step sets their size, so 16 steps of 1/8 is a 2-bar pattern and "
            "16 of 1/4 a 4-bar one; Half-time doubles either.")]
    return s


def chapter_matrix():
    s = H1("11  The modulation matrix", need=135)
    s += shot("matrix.jpg", "The MATRIX tab (Moving Reese): macros, LFOs and the mod wheel moving the filter and the detune.")
    s += [P("The matrix connects modulation <b>sources</b> to <b>destinations</b>. On the computer the quickest way is to "
            "<b>drag a source onto a knob</b>: see chapter 4. The MATRIX tab shows all 16 slots; each has:"),
          table([["Control", "What it does"],
                 ["Source", "What moves the destination (list below)."],
                 ["Dest", "What gets moved: almost any knob, plus pitch, amp and pan."],
                 ["Amount", "How far, -100% … +100%. Negative amounts move the other way."],
                 ["Aux", "An optional second source that scales the amount. Example: Source LFO 1, Aux Mod Wheel: the LFO "
                  "only works as far as the mod wheel is up."],
                 ["Curve", "Bends the source: positive = exponential (subtle at first, strong at the end), negative = "
                  "logarithmic."]], [36, 134])]
    s += H2("Sources")
    s += [table([["Source", "Range", "Notes"],
                 ["Env 1-4", "0 … 1", "Env 1 is the amp envelope"],
                 ["LFO 1-4", "-1 … +1", ""],
                 ["Perf 1-2", "-1 … +1", "The performers"],
                 ["Velocity", "0 … 1", "How hard the note was played"],
                 ["Pressure", "0 … 1", "Per-note pressure (poly aftertouch): on the MPC, pad pressure if the MPC sends it to "
                  "plugins (not yet confirmed)"],
                 ["Chan AT", "0 … 1", "Channel aftertouch"],
                 ["Mod Wheel", "0 … 1", "MIDI CC 1"],
                 ["Pitch Bend", "-1 … +1", ""],
                 ["Note", "-1 … +1", "The note played, centred on middle C (C3/C4 depending on your keyboard)"],
                 ["Random", "-1 … +1", "A new random value for each note"],
                 ["Macro 1-8", "0 … 1", "The eight macro knobs"]], [30, 24, 116])]
    s += H2("Destinations")
    s += [P("<b>Pitch</b>, <b>A Pitch</b> and <b>B Pitch</b> move the pitch by up to ±24 semitones at ±100% (so 50% is an "
            "octave). <b>Amp</b> scales the volume (tremolo). <b>Pan</b> moves the stereo position. Every other destination "
            "moves a knob from where you set it, by the amount, across the knob's whole range."),
          P("All destinations: " + ", ".join(MOD_DEST[1:]) + ".", "small"),
          Spacer(1, 4),
          P("Effects follow the modulation of the most recently played note. LFO rates can be modulated too (the change "
            "applies a fraction of a millisecond later).")]
    s += H2("Recipes")
    s += [table([["Sound", "Patch"],
                 ["Moving Reese", "LFO 1, Triangle, Sync 1 Bar → F1 Cutoff, 25%. LFO 2, Sine, free 0.15 Hz → A Detune, 25%."],
                 ["Wobble", "LFO 1, Sine, Sync 1/8 → F1 Cutoff, 55%, LP 24 with Reso 45%. Put the LFO's speed on the beat."],
                 ["Talking bass", "Vowels table. LFO 1 → A WT Pos 90% and → F1 Cutoff (Formant filter) 50%."],
                 ["Donk / pitch drop", "Env 2 with Attack 0.5 ms, Decay 60 ms, Sustain 0 → Pitch, 50% (an octave drop)."],
                 ["Expressive pad", "Mod Wheel → A WT Pos; Velocity → F1 Cutoff 20%; Pressure → F1 Cutoff 30%."]], [36, 134])]
    return s


def chapter_macros_voices():
    s = H1("12  Macros and voices")
    s += H2("Macros")
    s += [P("The eight macros are plain knobs whose job is to drive the matrix. Factory presets map their most useful moves "
            "to the first macros (for example in Classic Reese, Macro 1 opens the filter and Macro 2 widens the detune). On "
            "the MPC, put the macros on the Q-Links (the MACRO key's page holds all eight, and the OSC page has Macro 1-4) and record their movements into your "
            "sequences.")]
    s += H2("Voices and playing")
    s += [table([["Control", "What it does"],
                 ["Poly Mode", "<b>Poly</b>: up to 8 notes at once. <b>Mono</b>: one note; every new note restarts the "
                  "envelopes. <b>Legato</b>: one note; overlapping notes slide without restarting the envelopes (the "
                  "classic bassline mode). In Mono and Legato, releasing a key returns to the note you are still holding."],
                 ["Voices", "Maximum number of notes in Poly mode (1-8)."],
                 ["Glide", "Portamento time, 0-2 seconds (0 = off)."],
                 ["Glide Mode", "<b>Always</b>: every note glides from the last one. <b>Legato</b>: only overlapping notes "
                  "glide."],
                 ["Steal", "When all voices are busy: steal the <b>Oldest</b> or the <b>Quietest</b> note. Notes already "
                  "released are always taken first. A stolen note fades out in 3 ms, so there is no click."],
                 ["Bend Range", "Pitch-bend range, 0-24 semitones."],
                 ["Vel Sens", "How much velocity changes the volume."]], [36, 134]),
          Spacer(1, 4),
          P("HELIX also responds to the sustain pedal (CC 64), the mod wheel (CC 1), channel and poly aftertouch, and "
            "'all notes off'.")]
    return s


def chapter_fx():
    s = H1("13  Effects", need=135)
    s += [P("The effects work on the sum of all notes, after the amp envelope, in four slots like Massive's: <b>Insert 1</b> "
            "> <b>Insert 2</b> > <b>FX 1</b> > <b>FX 2</b>. Each slot plays one effect from the library below. HELIX's own "
            "seven effects are in the library too (the <b>HELIX</b> group); until 0.3.6 they lived in a fixed rack, which "
            "is now folded into the slots.")]
    s += [table([["HELIX effect", "Knobs", "Notes"],
                 ["HELIX Dist", "Type, Drive, Tone, Crush Bits, Crush Rate; Mix", "<b>Tube</b> (warm, asymmetric), <b>Hard "
                  "Clip</b>, <b>Fold</b> (wavefolder), <b>Crush</b> (bit depth and sample rate, e.g. 12 bit / 26 kHz for "
                  "SP-1200, lower for S950 grit), <b>Asym</b> (half-wave crunch). Tone is a low-pass after the distortion."],
                 ["HELIX Chorus", "Mode, Rate, Depth; Mix", "<b>Chorus</b>: two voices. <b>Ensemble</b>: three shimmering "
                  "voices per side, the 80s/90s string-machine sound."],
                 ["HELIX Phaser", "Rate, Depth, Freq, Feedback; Mix", "Six-stage stereo phaser; negative feedback gives a "
                  "hollower sound."],
                 ["HELIX Delay", "Mode, Sync, Division, Time, Feedback, Tone; Mix", "<b>Stereo</b> or <b>Ping-Pong</b>. With "
                  "Sync on, the time is the Division from the tempo. Tone darkens each repeat. Mix adds the echoes."],
                 ["HELIX Reverb", "Type, Size, Decay, Damp, Predelay; Mix", "<b>Plate</b> (bright, dense) or <b>Hall</b> "
                  "(bigger, longer)."],
                 ["OTT", "Depth, Time, Upward, Downward, Output", "Three-band compressor (split at 120 Hz and 2.5 kHz) that "
                  "pushes loud parts down and pulls quiet parts up: the modern bass sound. The most CPU-hungry HELIX effect "
                  "(the CPU readout says OTT when it is on)."],
                 ["HELIX EQ", "Low / Mid / High Freq and Gain", "Low shelf, bell and high shelf, ±18 dB (the bell's Q stays "
                  "as the preset set it)."]],
                [24, 48, 98])]
    s += [P("<b>Older presets</b> open with their rack effects already moved into the slots, in the same order, with the "
            "same settings and modulation, so they sound the same. Effects before the first delay or reverb go to the "
            "inserts and the delay / reverb onwards to FX 1 / 2, as in Massive. A preset with more than four rack effects "
            "switched on keeps the extra ones in a hidden rack between Insert 2 and FX 1 (none of the factory presets "
            "has more than two). In the matrix destination list the old rack settings (Dist Drive, Reverb Mix ...) now do "
            "nothing: aim at the slot's knobs instead (Insert 1 A ... FX 2 F).")]
    s += H2("Sub guardian")
    s += [P("Drum and bass lives on its sub. With <b>Sub Guard</b> on, HELIX splits the sound at <b>Split</b> (40-300 Hz, "
            "120 Hz to start) with a steep 24 dB crossover. Everything below goes round the effects: it is summed to mono and "
            "added back after FX 2, clean, so distortion, OTT, reverb and chorus can go as far as you like without smearing "
            "or widening the sub. Everything above goes through the slots as usual. <b>Sub Level</b> trims the sub (-12 to "
            "+6 dB, modulatable) and <b>Warmth</b> adds a little 2nd and 3rd harmonic so it still reads on small speakers. "
            "With no effects on, switching it on does not change the level. On the MPC it is the <b>SUB GUARD</b> page (FX "
            "key); on the computer it is under the effect slots.")]
    s += H2("The effects library: Insert 1, Insert 2, FX 1, FX 2")
    s += [P("Four more effect slots, laid out like Massive's: <b>Insert 1</b> and <b>Insert 2</b> run before the rack above, "
            "<b>FX 1</b> and <b>FX 2</b> after it. Each slot picks any effect from the library below and has <b>Mix</b> plus six "
            "knobs, <b>A</b> to <b>F</b>, whose names change with the effect (an unused knob shows -). When you pick an "
            "effect, its knobs and Mix go to that effect's own starting values. For delays and reverbs Mix adds the "
            "effect to the dry sound; for the others it blends from dry to fully processed. Every knob and Mix can be "
            "modulated from the matrix."),
          P("On the computer the slots fill the FX tab. On the MPC they are the <b>INSERTS</b> and <b>FX 1-2</b> pages (FX "
            "key): turn <b>Effect</b> or double-tap it for the list, and the knob names follow it."),
          P("The library brings together the effects from Twisted Genetics' other projects, each copy only once. "
            "Each effect appears once."),
          table([["Group", "Effects", "From"],
                 ["Delay", "<b>Ping-Pong</b> (dark half-rate dub echo), <b>Tape Echo</b> (wow, flutter, wear), <b>BBD Echo</b> "
                  "(bucket brigade), <b>Dub Delay</b> (filtered, driven, runs away past 100%)", "DREADNOUGHT, Mega Synth"],
                 ["Reverb", "<b>Spring</b>, <b>Tomb Room</b>, <b>Mod Hall</b>, <b>Celestial</b> (octave-up shimmer), "
                  "<b>Dolmen</b> (small, still), <b>Tumulus</b> (long, dark), <b>Plate</b>, <b>Gated Verb</b>, "
                  "<b>Vastness</b> (eight huge modes), <b>Super-Massive</b> (the Horizon network: ten modes from room to "
                  "self-oscillating dub echo), <b>Quadraverb</b> (1989 multi-effect chains, 16-bit grit)",
                  "DREADNOUGHT, Mega Synth, Super-Massive"],
                 ["Chorus", "<b>Ensemble</b> (string machine), <b>BBD Chorus</b>, <b>Multi-Chorus</b> (2-8 voices), "
                  "<b>Dimension</b> (four modes), <b>Widener</b> (micro-pitch), <b>Flanger</b>", "Mega Synth"],
                 ["Phaser", "<b>Script Phase</b>, <b>Colour Phase</b>, <b>Dual Phaser</b>, <b>Barber-pole</b> (endless "
                  "rise or fall), <b>Vibe</b>", "Mega Synth"],
                 ["Character", "<b>Filter</b> (with its own LFO), <b>Ring Mod</b>, <b>Drive</b> (five types)", "DREADNOUGHT"],
                 ["Sampler", "<b>SP-1200</b>, <b>MPC60</b>, <b>S950</b>, <b>S1000</b>, <b>Amiga</b>: each machine's rate, "
                  "bits, converter and output filter. Rate pitches the clock down; Bits Native = the machine's own.",
                  "EraCrush"],
                 ["Rhythm / utility", "<b>Beat Repeat</b> (random repeats by Chance), <b>Volume Shaper</b> (pump, gate, "
                  "1/8, triplet, locked to the tempo), <b>Stereo Tools</b> (bass mono, width, Haas)", "Mega Synth"]],
                [22, 110, 38]),
          P("Left out as duplicates: DREADNOUGHT's phaser, chorus and crusher (the Mega Synth phasers and choruses and "
            "EraCrush cover them), Mega Synth's spring (DREADNOUGHT's), vintage sampler (EraCrush) and multiband (HELIX's "
            "OTT). The IR reverb is not included: it needs impulse files and a convolution engine too heavy for the MPC."),
          P("CPU: an Off slot costs nothing. Most effects use a few percent of the MPC's CPU; the big reverbs (Plate, Gated "
            "Verb, Vastness, Super-Massive) and Dub Delay use the most. The library's delays and reverbs were tuned at "
            "44.1 kHz, the MPC's rate; at 48 kHz their times run about 8% shorter.")]
    return s


def chapter_presets():
    s = H1("14  Presets", need=135)
    s += shot("browser.jpg", "The GLOBAL tab on the computer: voices, engine quality, theme, CPU per voice and the preset list.")
    s += H2("Factory presets")
    rows = [["Category", "Preset", "Description"]]
    for j in PRESETS:
        rows.append([j["category"], "<b>%s</b>" % j["name"], j.get("comment", "")])
    s += [table(rows, [20, 34, 116])]
    s += H2("Saving and loading")
    s += [table([["", "On the computer", "On the MPC"],
                 ["Load", "Click the preset name at the top of the window, use &lt; and &gt;, or double-click a preset on "
                  "the GLOBAL tab.",
                  "The MPC's preset menu at the top of the screen (factory presets, then yours: those saved with the MPC's "
                  "save button, and HELIX presets in Content/Presets, marked *), or <b>Preset</b> + <b>Load</b> on the "
                  "BROWSE page."],
                 ["Save", "<b>Save</b> at the top of the window: name and category; saved in Documents/HELIX/Presets.",
                  "To put a sound in the MPC's preset menu: tap the MPC's <b>save button</b> (the disk icon at the top right "
                  "of the plugin screen), name it, and it is in the menu straight away (the MPC keeps it in its Plugin "
                  "Presets folder). Presets copied into HELIX's Content/Presets folder (from the computer) are listed in "
                  "the menu too, marked *, from the next time HELIX loads."],
                 ["Init", "<b>Init</b> (top of the window) resets everything to the starting sound.", "Start from a factory preset, or remove "
                  "HELIX from the track and add it again."]], [18, 76, 76])]
    s += H2("Mutate")
    s += [P("<b>Mutate</b> (computer) and <b>Mutate 10%</b> (MPC) nudge every continuous knob (cutoffs, levels, times, modulation amounts and so on) by a random "
            "amount up to 10% of its range. It leaves the structure alone: filter types, warp modes, routing choices, "
            "tables, the number of voices and the master level stay as they are. Press it a few times and keep the "
            "variations you like.")]
    s += H2("Preset files")
    s += [P("A HELIX preset is a small text file (.json) that holds every setting, the wavetables used (by name) and the "
            "LFO shapes. The same file works on the MPC and on the computer. If a preset uses one of your own wavetables, "
            "copy that wavetable into the other machine's Wavetables folder as well.")]
    return s


def chapter_mpc():
    s = H1("15  HELIX on the MPC")
    s += [P("On the MPC, HELIX's screen is drawn by the MPC itself, from the skin that comes with the plugin (the "
            "<b>Massive</b> colour scheme from version 0.3: navy panels in light grey frames). The sections are on the function keys along the bottom of the "
            "screen: OSC, FILTER, ENV, LFO, FX, MOD, PERF, MACRO and BROWSE. Press a key again to go to its next page (for "
            "example OSC → WARP → OSC MIX → SUB/NSE). The MPC shows a few keys at a time; use the arrow key to see the rest."),
          P("Every page is made of up to four <b>strips</b>. A strip is one row of four controls and matches one row of "
            "Q-Links: the top strip is the top row of knobs (Q-Links 13-16) and the bottom strip the bottom row (Q-Links "
            "1-4), so the knob next to a control edits it. Touching a control makes its row of knobs live and the MPC draws "
            "a frame around the strip.")]
    s += shot("mpc_osc.png", "The OSC page on the MPC: Osc A and B with their wavetable position screens, sub and noise, "
              "and Macro 1-4. The MPC draws the bar at the top and the function keys itself.")
    s += H2("Reading the screen")
    s += [*bullets(["<b>Grey tags</b> on the left name each strip. Where the section can be switched off (Osc A and B, Sub, "
                    "Noise, Filter 1 and 2, every effect), the tag is its <b>on/off button</b>: tap it. A cyan light and 'On' "
                    "mean on; a dark light, a darker tag and 'Off' mean off, and the section costs no CPU.",
                    "<b>Screens</b> show the oscillator's wavetable position, the warp mode, the unison stack, the sub shape, "
                    "the noise type, the filter response, the LFO shape and the effect types, and change as soon as you change "
                    "the parameter. Each screen is also the control that sets it: drag up or down on it, or turn its Q-Link. "
                    "Warp modes, filter types, LFO shapes and the effect types are all set this way.",
                    "<b>Rows of buttons</b> choose directly: touch the one you want. Sub shape and noise type (SUB/NSE), the "
                    "Pad Bank, Frames and the performer curves (PERF, drawn as small curves) are buttons. Rows with two choices (-1 Oct / -2 Oct) flip with each touch.",
                    "<b>Knobs</b>: drag up and down, use the data wheel, or the Q-Link. Double-tap for the large editor. Knobs "
                    "that go both ways from the middle (fine tune, pan, mod amounts) light up from the centre.",
                    "<b>Lists</b> (filter types, warp modes, LFO shapes, effects, mod-matrix sources and destinations, sync "
                    "divisions ...): touch the control and turn the data wheel to step through, as before, or <b>double-tap</b> "
                    "it (or push the data wheel) for the whole list on one screen. Tap the entry you want, then tap outside "
                    "the list or push the wheel again to close it. (These are HELIX's own lists: the MPC's built-in pop-up "
                    "menus can open with no entries on current firmware.)",
                    "<b>Envelopes</b> (ENV page) use the MPC's own envelope display: drag its points or use the four knobs "
                    "beside it.",
                    "When an LFO or the delay is tempo-synced, the free <b>Rate</b> / <b>Time</b> knob is hidden and its place "
                    "says so; with sync off, the <b>Sync Div</b> knob is hidden instead. LFO tempo sync is on the LFO+ page.",
                    "Names are 24 px and values 32 px, white on the navy panels, and values are kept short "
                    "(<b>500 ms</b>, <b>1.20 s</b>, <b>12.0 kHz</b>, <b>80%</b>)."])]
    s += shot("mpc_fx.png", "The INSERTS page: each slot's effect, Mix and knobs A-F, named after the effect.")
    s += H2("Page map")
    rows = [["Key", "Page", "Strips (top to bottom) and their controls"]]
    for p in MPC_PAGES:
        strips = []
        for st in p["strips"]:
            names = []
            for c in st["cells"]:
                if c["type"] == "env":
                    names.append("envelope display")
                elif c["type"] == "picture":
                    continue
                elif c["type"] == "screen":
                    names.append(c["param"] + " (screen)")
                else:
                    names.append(c.get("name") or c["param"])
            title = st["title"].replace("\n", " ")
            on = " [tag = %s on/off]" % st["toggle"] if st["toggle"] else ""
            strips.append("<b>%s</b>%s: %s" % (title, on, ", ".join(names)))
        rows.append([MPC_KEYS[p["fn"]] if p["sub"] == 0 else "", "<b>%s</b>" % p["name"], "<br/>".join(strips)])
    s += [table(rows, [16, 20, 134]), Spacer(1, 6)]
    s += [P("In the MPC's Program Q-Link mode (knobs assigned to the program rather than the screen), the 16 Q-Links are "
            "Macro 1-8, F1 Cutoff, F1 Reso, F1 Drive, A WT Pos, B WT Pos, Delay Mix, Reverb Mix and Master.", "small")]
    s += H2("Pad Browse: audition presets from the pads")
    s += [P("A plugin cannot see the MPC's Shift button, so instead of 'hold Shift to audition', HELIX has <b>Pad Browse</b> "
            "on the BROWSE page:"),
          *steps(["Turn <b>Pad Browse</b> on.",
                  "Choose a <b>Pad Bank</b> (1-16, 17-32 and so on).",
                  "Hit pads 1-16 (MIDI notes 36-51): each pad loads the matching preset and plays it, so you hear the new "
                  "sound straight away.",
                  "Turn Pad Browse off to play HELIX normally again."])]
    s += H2("The CPU readout")
    s += [P("<b>CPU Load</b> on the BROWSE page shows how much of one processor core HELIX itself is using, updated a few "
            "times a second. If it shows <b>ECO1</b> to <b>ECO4</b>, the CPU guard has lowered the quality to keep up (see the "
            "next chapter). Compare it with the MPC's own CPU meter, which shows the whole project.")]
    s += H2("What is different from the computer")
    s += [*bullets(["The screens are pictures made in advance, not live drawings: the wavetable screen shows a general stack "
                    "of frames that follows <b>Position</b>, not the waveforms of the table you loaded. No modulation rings.",
                    "No editor for the LFO breakpoint and step shapes; they come with presets.",
                    "Your own files must be inside the plugin's Content folder: a plugin cannot reach the MPC's sample pool or "
                    "other folders.",
                    "Pad pressure works only if the MPC passes it to plugins as poly aftertouch; this has not been confirmed "
                    "yet."])]
    s += note("Version 0.1.0's MPC screen file had a fault that can stop the MPC from showing any HELIX pages. If you "
              "installed 0.1.0, replace the whole <b>Twisted Genetics - VST - HELIX</b> folder with this version.", "Updating from 0.1.0")
    return s


def chapter_cpu():
    s = H1("16  Sound quality and CPU")
    s += [P("The <b>Quality</b> control (GLOBAL tab on the computer, VOICE page under BROWSE on the MPC) trades sound against "
            "processing power:"),
          table([["Quality", "What changes", "Use it for"],
                 ["Eco", "At most 3 unison voices; the smoothing between the oscillators' anti-aliasing levels is off.",
                  "Busy MPC projects, many HELIX instances"],
                 ["Normal", "Full quality.", "Most things"],
                 ["High", "Oscillators and distortion run at double rate, which cleans up warps and distortion on high "
                  "notes. Roughly doubles the oscillator cost.", "Leads and high sounds, on a computer or a 64-bit MPC"]],
                [22, 96, 52])]
    s += H2("CPU guard")
    s += [P("With <b>CPU Guard</b> on (the default), HELIX watches its own processing time. If it goes over 35% of one core, "
            "it steps the quality down, one level at a time, at most every quarter of a second, and steps back up once it "
            "has stayed well under the budget for two seconds:"),
          table([["Level", "What it turns down"],
                 ["ECO1", "Double-rate oscillators off (High behaves like Normal)"],
                 ["ECO2", "Smoothing between anti-aliasing levels off; unison at most 5"],
                 ["ECO3", "Unison at most 3"],
                 ["ECO4", "Unison at most 2"]], [22, 148]),
          Spacer(1, 4),
          P("When your DAW bounces or freezes a track offline, the CPU guard switches itself off, so a bounce is always at "
            "full quality and identical every time.")]
    s += H2("How much CPU does HELIX use?")
    s += [P("The heaviest standard patch, 8 notes × 2 oscillators × 4 unison voices with both filters and three effects, "
            "measured at about <b>4% of one core</b> on a 2.1 GHz computer processor. On an MPC that is estimated (not yet "
            "measured) at roughly <b>21-28% of one core</b> on the 32-bit MPCs (MPC One / One Retro, Live II) and "
            "<b>8-11%</b> on the 64-bit ones. Real presets usually use less. Tips for busy projects:"),
          *bullets(["Use 2-4 unison voices instead of 7.",
                    "Turn off the second oscillator, the second filter and effects you don't need: anything switched off "
                    "costs nothing.",
                    "OTT and the reverb are the most expensive effects.",
                    "Fewer Voices in Poly mode, or Mono / Legato for basses."])]
    return s


def chapter_troubleshooting():
    s = H1("17  Troubleshooting")
    s += [table([["Problem", "What to check"],
                 ["HELIX does not appear on the MPC", "MPC OS 3.10.1 or later? Both Enhancements switches on? Is the folder "
                  "named <b>Twisted Genetics - VST - HELIX</b> directly inside <b>VST2 Plugins</b> at the top level of the "
                  "drive (not inside another folder)? Restart the MPC after copying."],
                 ["HELIX appears but the screen is empty or generic", "Copy the whole HELIX folder again (the 'Plugin Skins' "
                  "folder holds the screen) and restart the MPC."],
                 ["No sound from the pads", "Set the pads to a chromatic layout, or turn Pad Browse off."],
                 ["My wavetables are not in the list", "Are they WAV files in Content/Wavetables (MPC) or "
                  "Documents/HELIX/Wavetables (computer)? The list is read when HELIX opens: reload the plugin."],
                 ["A preset sounds different on another machine", "It probably uses one of your own wavetables: copy the "
                  "wavetable to the other machine's Wavetables folder."],
                 ["Crackles on the MPC", "Lower the unison, use Quality Eco, switch off unused oscillators, filters and "
                  "effects, or raise the MPC's audio buffer size in its preferences."],
                 ["macOS won't open the plugin", "Remove the quarantine flag (see chapter 2) and rescan plug-ins."],
                 ["The MPC restarts while using HELIX", "Please report it with the steps that cause it. Third-party plugins "
                  "run inside the MPC software; save often while testing."]], [46, 124])]
    s += H2("MIDI implementation")
    s += [table([["Message", "Effect"],
                 ["Note on / off", "Plays notes (velocity used for volume and as a mod source)"],
                 ["Notes 36-51 with Pad Browse on (MPC)", "Load and audition presets"],
                 ["Pitch bend", "Pitch, by Bend Range; also a mod source"],
                 ["CC 1 (mod wheel)", "Mod source"],
                 ["CC 64 (sustain)", "Holds notes"],
                 ["CC 120 / CC 123", "All sound off / all notes off"],
                 ["Channel aftertouch", "Mod source 'Chan AT'"],
                 ["Poly aftertouch", "Mod source 'Pressure'"]], [60, 110])]
    return s


def appendix_params():
    s = [PageBreak(), Paragraph("Appendix: parameter reference", ss["h1"])]
    s += [P("Every HELIX parameter with its range and starting value. Groups that repeat (Osc B, Filter 2, envelopes 2-4, "
            "LFOs 2-4, performer 2, matrix slots 2-16, macros 2-8, performer steps 2-16) work exactly like the first one "
            "shown. Ranges marked 'log' or 'fine at low end' give more knob travel to small values.", "small"),
          Spacer(1, 4)]
    skip = re.compile(r"^P_(B_|F2_|E[234]_|L[234]_|P2_|M([2-9]|1[0-6])_|MACRO[2-8]$|P1_S([2-9]|1[0-6])$|P1_C([2-9]|1[0-6])$)")
    groups = [("Global", r"^P_(MASTER|POLY_MODE|VOICES|GLIDE|GLIDE_MODE|STEAL|BEND_RANGE|VEL_SENS|QUALITY|AUTO_SCALE)$"),
              ("Oscillators (A shown; B is the same)", r"^P_A_"), ("Sub", r"^P_SUB_"), ("Noise", r"^P_NOISE_"),
              ("Filters (Filter 1 shown; Filter 2 is the same)", r"^P_(F1_|FILTER_BLEND)"),
              ("Envelopes (1 shown; 2-4 are the same)", r"^P_E1_"), ("LFOs (1 shown; 2-4 are the same)", r"^P_L1_"),
              ("Performers (1 shown; 2 is the same)", r"^P_P1_"), ("Matrix (slot 1 shown; 2-16 are the same)", r"^P_M1_"),
              ("Macros", r"^P_MACRO1$"), ("Effects", r"^P_(FX_|DIST_|CHO_|PH_|DLY_|REV_|OTT_|EQ_)")]
    for title, rx in groups:
        rows = [["Parameter", "Range", "Starts at"]]
        for p in PARAMS:
            if not re.match(rx, p["enum"]) or skip.match(p["enum"]):
                continue
            name = p["name"]
            if p["enum"].startswith("P_A_"):
                name = name.replace("A ", "A/B ", 1) if name.startswith("A ") else name.replace("Osc A", "Osc A/B")
            if p["enum"].startswith("P_F1_"):
                name = name.replace("F1 ", "F1/F2 ").replace("Filter 1", "Filter 1/2")
            if p["enum"].startswith("P_E1_"):
                name = name.replace("E1 ", "E1-E4 ")
            if p["enum"].startswith("P_L1_"):
                name = name.replace("L1 ", "L1-L4 ")
            if p["enum"].startswith("P_P1_"):
                name = name.replace("P1 Step 1", "P1/P2 Step 1-16").replace("P1 Shape 1", "P1/P2 Shape 1-16").replace("P1 ", "P1/P2 ")
            if p["enum"].startswith("P_M1_"):
                name = name.replace("M1 ", "M1-M16 ")
            if p["enum"] == "P_MACRO1":
                name = "Macro 1-8"
            dflt = fmt_value(p, p["dflt"])
            if p["enum"] in ("P_A_TABLE",):
                dflt = "Basic Shapes"
            rows.append([name, fmt_range(p), dflt])
        s += [H3(title), table(rows, [50, 88, 32]), Spacer(1, 4)]
    s += [H3("Choice lists")]
    lists = [("Warp modes", "warpMode"), ("Filter types", "filterType"), ("LFO shapes", "lfoShape"),
             ("Sync divisions", "syncDiv"), ("Mod sources", "modSource"), ("Performer curves", "perfStepShape"),
             ("Noise types", "noiseType"), ("Distortion types", "distType"), ("FX slots", "fxType")]
    s += [table([["List", "Values"]] + [[a, " · ".join(CHOICES.get(b, []))] for a, b in lists], [36, 134])]
    return s


def build():
    story = []
    story += cover_page()
    story += contents()
    for ch in (chapter_welcome, chapter_install, chapter_quickstart, chapter_desktop, chapter_osc, chapter_tables, chapter_subnoise,
               chapter_filters, chapter_env, chapter_lfo, chapter_matrix, chapter_macros_voices, chapter_fx,
               chapter_presets, chapter_mpc, chapter_cpu, chapter_troubleshooting, appendix_params):
        story += ch()
    doc = Doc(OUT)
    doc.multiBuild(story)
    print("wrote", OUT)


if __name__ == "__main__":
    sys.exit(build())
