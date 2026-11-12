#!/bin/sh
# Builds the HELIX MPC plugin folder with the official MPC Plugin SDK (external/mpc-plugin-builder-sdk):
#   native (for the host simulator) + arm32 (Gen1: One / One+ / Retro / Live II / X / Key / Force) + arm64 (Gen2).
# Output: helix-mpc/build/Twisted Genetics - VST - HELIX/   -> copy into <drive>/VST2 Plugins/
#
#   helix-mpc/build.sh [extra build.py args, e.g. --target native]
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
sdk="$root/external/mpc-plugin-builder-sdk"
[ -f "$sdk/tools/build.py" ] || { echo "MPC Plugin SDK missing: git submodule update --init external/mpc-plugin-builder-sdk"; exit 1; }
py="${PYTHON:-python3}"

# 1. ui.json from the shared page layout, factory presets into Content/Presets for reference
if [ -x "$root/build/helix-mpc/helix-mpc-ui" ]; then "$root/build/helix-mpc/helix-mpc-ui" "$here/ui.json"; fi
"$here/tools/sync_core.sh" >/dev/null

# 2. SDK build; helix-core and helix-mpc headers come in through CPATH (build.py only adds src/ and the framework)
export CPATH="$root/helix-core/include:$here/include${CPATH:+:$CPATH}"
"$py" "$sdk/tools/build.py" "$here" "$@"

# 3. Bigger text in the skin (values >= 32 px, other text >= 24 px), then validate again
out="$here/build/Twisted Genetics - VST - HELIX"
if [ -f "$out/Plugin Skins/TUI.json" ]; then
  "$py" "$here/tools/skin_fonts.py" "$out/Plugin Skins/TUI.json"
  "$py" "$sdk/tools/validate.py" "$here" || true
fi
