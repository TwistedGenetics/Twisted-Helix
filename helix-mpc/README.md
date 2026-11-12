# HELIX for Akai standalone MPCs

HELIX as a native MPC plugin, built with inMusic's official **MPC Plugin SDK** (MPC OS **3.10.1 or later**).
One folder runs on both MPC families:

| Build | Devices |
|---|---|
| `VST/helix-arm32.so` | Gen1, 32-bit: MPC One / One+ / **One Retro**, Live / Live II, X / X SE, Key 61 / Key 37, Force |
| `VST/helix-arm64.so` | Gen2, 64-bit: MPC Live III, MPC XL and later 64-bit units |

> **Status: untested on hardware.** It builds with the SDK's toolchain, passes the SDK's validator and the SDK's
> host simulator (which behaves like the MPC's host), and the app logic has its own tests
> (`tests/test_mpc_app.cpp`). See `docs/PLATFORM.md` for what still needs a device.

## Install
1. Build (below) or take a release zip.
2. Copy the folder `Twisted Genetics - VST - HELIX` into a folder named exactly **`VST2 Plugins`** at the root of
   a USB stick, an SD card or the MPC's internal drive.
3. On the MPC: **Preferences → Enhancements (Beta)**, turn on **Enable Enhancements** and **External Plugin
   Support**. Third-party plugins run inside MPC with no protection: save your projects often.
4. Add HELIX to a plugin program: it is under **External Plugins**. Set the track's pads to a chromatic layout
   (for example Pad Perform → Chromatic) to play it from the pads.

Your own files go in the plugin folder's `Content/`:
- `Content/Wavetables/`: Serum-style WAV tables, single-cycle WAVs, or folders of single cycles. They appear after
  the built-in tables when you turn **A Table** / **B Table**.
- `Content/Samples/`: WAVs (your breaks, vocals) for **Sample > Table** on the BROWSE page.
- `Content/Presets/<Category>/*.json`: HELIX presets (from the desktop plugin or another MPC). They appear after
  the 24 factory presets in the **Preset** list.

## On the MPC
28 pages on the function keys; every page is four blocks of four controls, one Q-Link block each.

| Pages | What |
|---|---|
| OSC A 1-2, OSC B 1-2 | table, position, level, pitch, two warp slots, unison, phase, sends to F1 / F2 |
| SUB/NSE | sub (shape, octave, direct-out) and noise (type, pitch tracking, loop) |
| FILTER 1-2 | both filters, drive, key tracking, fat, the serial / parallel blend |
| ENV 1-2 | four ADSRs with curves (Env 1 = amp) |
| LFO 1-2, LFO 3-4, PERF | LFOs, LFO step counts, performer settings |
| P1 STEP, P1 SHAPE, P2 STEP, P2 SHAPE | the performers' 16 steps and step shapes |
| M1-4 … M13-16, CURVES | the 16 mod-matrix slots (source, destination, amount, aux) and their curves |
| FX 1-4 | FX order and every effect |
| MACROS | Macro 1-8: put these on the Q-Links in your projects |
| BROWSE 1-2 | Preset, Load, Pad Browse, Pad Bank, Mutate 10%, CPU, Sample > Table, global settings |

- **Presets**: the MPC's own preset menu lists the 24 factory presets; the **Preset** control + **Load** also
  reaches your `Content/Presets`.
- **Pad Browse** (instead of "hold Shift to audition", which a plugin cannot see): while it is on, pads 1-16
  load presets of the chosen **Pad Bank** and play them.
- **CPU** shows HELIX's own load ("23%", "ECO2" when the CPU guard has stepped quality down).

## What the MPC cannot show (by design of the SDK skins)
The page is drawn by the MPC from JSON; a plugin cannot draw. So, compared with the desktop plugin:
no live wavetable display, no modulation rings, no drawable LFO shapes (they come with presets and from the
desktop editor). Pad pressure as poly aftertouch is handled, but whether MPC forwards it to plugins is unverified.

## Build
```
git submodule update --init external/mpc-plugin-builder-sdk
cmake -B build -G Ninja && cmake --build build      # also builds the ui.json generator
helix-mpc/build.sh                                  # native + arm32 + arm64, skins, validation
```
`build.sh` regenerates `ui.json` from the shared page layout (`helix-core` `UiLayout`), wraps each helix-core
source for the SDK (`tools/sync_core.sh`), runs the SDK's `tools/build.py` (Zig cross compiler, fetched on first
use), raises the skin's text sizes (`tools/skin_fonts.py`: values ≥ 32 px, other text ≥ 24 px) and validates.
Test the native build with the SDK's host simulator:
`MPC_PLUGIN_FOLDER=helix-mpc external/mpc-plugin-builder-sdk/.sdk-tools/host_sim build/helix-mpc/helix-native.so test --playing`.

## Layout
| File | Role |
|---|---|
| `include/IPlatform.h` | the thin platform interface (audio, MIDI, files, UI surface, encoders) |
| `include/HelixMpcApp.h`, `app/HelixMpcApp.cpp` | platform-independent app: engine, tables, presets, pad browse, triggers, CPU readout, worker thread |
| `include/MpcParams.h` | MPC-only parameters, after a reserved block (indices 359-447) so the core list can grow |
| `src/HelixMpc.cpp` | the MPC Plugin SDK adapter (`mpc::Plugin` + `IPlatform`) |
| `src/core_*.cpp` | generated wrappers that compile helix-core into the plugin |
| `tools/make_ui.cpp`, `tools/skin_fonts.py` | the skin generator and its text-size pass |
