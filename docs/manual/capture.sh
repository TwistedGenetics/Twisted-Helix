#!/bin/bash
# Re-takes the manual's screenshots from the desktop standalone under Xvfb (Linux).
#   docs/manual/capture.sh            (needs Xvfb, xdotool, ImageMagick's import, python3 + Pillow)
set -e
cd "$(dirname "$0")/../.."
APP=build-desktop/helix-desktop/HelixDesktop_artefacts/Release/Standalone/HELIX
OUT=docs/manual/img
TMP=$(mktemp -d)
export DISPLAY=:98
Xvfb :98 -screen 0 1920x1200x24 >/dev/null 2>&1 & XPID=$!
sleep 2
SET=$HOME/HELIX/HELIX-ui.settings
mkdir -p "$(dirname "$SET")"
theme() { printf '<?xml version="1.0" encoding="UTF-8"?>\n<PROPERTIES>\n  <VALUE name="theme" val="%s"/>\n</PROPERTIES>\n' "$1" > "$SET"; }

# shot <name> <program> <tab> <crop x y w h in window content coords | full> [chip index to click]
shot() {
  local name=$1 prog=$2 tab=$3 crop=$4 chip=${5:-}
  (HELIX_START_PROGRAM=$prog HELIX_START_PAGE=$tab timeout 14 "$APP" >/dev/null 2>&1 &)
  xdotool mousemove 5 5
  sleep 8
  xdotool mousemove 6 6
  sleep 1.5      # let any tooltip close
  import -window root "$TMP/raw.png"
  # content origin = window crop origin + (4, 30) (frame and title bar)
  read BX BY < <(python3 -c "
from PIL import Image; im=Image.open('$TMP/raw.png'); b=im.convert('L').point(lambda v:255 if v>8 else 0).getbbox(); print(b[0]+4, b[1]+30)")
  if [ -n "$chip" ]; then
    xdotool mousemove $((BX + 16 + chip * 53 + 25)) $((BY + 610)) click 1
    sleep 0.6
    xdotool mousemove 5 5
    sleep 0.4
    import -window root "$TMP/raw.png"
  fi
  python3 - "$TMP/raw.png" "$OUT/$name.jpg" $BX $BY $crop <<'PY'
import sys
from PIL import Image
src, dst, bx, by = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
im = Image.open(src).convert("RGB")
if sys.argv[5] == "full":
    box = (bx, by, bx + 1200, by + 800)
else:
    x, y, w, h = map(int, sys.argv[5:9])
    box = (bx + x - 4, by + y - 4, bx + x + w + 4, by + y + h + 4)
im.crop(box).save(dst, quality=90, optimize=True)
PY
  sleep 6
}

theme 0
shot window   2  0 full
shot oscA     10 0 "8 84 384 400"
shot filters  2  0 "792 84 400 400"
shot subnoise 16 0 "8 490 1184 96"
shot env      3  0 "8 592 776 200" 1
shot lfo      1  0 "8 592 776 200" 4
shot perf     8  0 "8 592 776 200" 8
shot macros   0  0 "792 592 400 200"
shot matrix   1  2 "8 84 1184 330"
shot fx       15 1 "8 84 1184 330"
shot browser  0  3 full
theme 1
shot paper    15 0 full
theme 0
kill $XPID
rm -rf "$TMP"
echo "screenshots in $OUT"
