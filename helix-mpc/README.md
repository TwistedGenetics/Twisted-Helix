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
The touch UI is the **Concrete Rack** skin: light concrete background, and every row of four Q-Links is one
full-width strip with a thick black border and an orange tag on the left. Where a section can be switched off
(Osc A/B, Sub, Noise, Filter 1/2, every effect), the tag is its on/off button: orange = on, hatched grey = off.
Small screens show the oscillator shapes, warp, unison stack, filter response, LFO shape, distortion curve,
delay taps and reverb type, and they change when you change the parameter. Touch a screen to make its Q-Link live;
double-tap it for the list. Names are 24 px, values 32 px, dark on light.

Each strip is one row of Q-Links, top strip = top row of knobs (Q-Links 13-16), so the knob next to a control
edits it. 9 function keys; press a key again for its next page:

| Key | Pages | What |
|---|---|---|
| OSC | OSC · WARP · OSC MIX · SUB/NSE | A/B wavetable (position screen), octave, fine; sub and noise shape; Macros 1-4 · warp slots and unison · level, semi, pan, phase, filter sends · sub octave, direct out, noise tracking / loop, sends |
| FILTER | FILTER | both filters with response screens; key tracking, fat, level, serial / parallel |
| ENV | ENV · CURVES | four ADSRs, each with the MPC's own envelope display (drag the points) · attack / decay / release curves |
| LFO | LFO · LFO+ | shape screen, rate (free) or sync division (synced), mode · tempo sync, rise, smooth, phase |
| FX | FX · FX+ · CHO/PHS · EQ · FX ORDER | distortion, delay, reverb, OTT · crusher, delay time / tone, reverb damp / predelay, OTT up / down · chorus, phaser · 3-band EQ · effect order |
| MOD | M1-4 … M13-16 · M CURVE | the 16 mod-matrix slots (source, destination, amount, aux) and their curves |
| PERF | PERF · P1 STEP · P1 SHAPE · P2 STEP · P2 SHAPE | LFO step counts, performer rate / length / mode, the performers' 16 steps and step shapes |
| MACRO | MACROS · SCENES | Macro 1-8: put these on the Q-Links in your projects · pad scenes, Filth, Pad Browse / Bank |
| BROWSE | BROWSE · TABLES · VOICE | Preset, Load, Mutate 10%, CPU, Name (on-screen keyboard), Slot, Save · Sample > Table, Print · master, voices, glide, bend, quality |

In Program Q-Link mode the knobs are Macro 1-8, F1 Cutoff / Reso / Drive, A and B WT Pos, Delay Mix, Reverb Mix
and Master.

- **Presets**: the MPC's own preset menu lists the factory presets, your `Content/Presets` files and the user slots
  U001-U128. The BROWSE page's **Preset** control lists the same entries in the same order and follows the menu.
- **Saving**: type the name on the BROWSE page's keyboard, pick a **Slot** and press **Save**. The slot, the
  **Preset** box and the MPC's preset menu show the new name straight away.
- **Pad Browse** (instead of "hold Shift to audition", which a plugin cannot see): while it is on, pads 1-16
  load presets of the chosen **Pad Bank** and play them.
- **CPU** shows HELIX's own load ("23%", "ECO2" when the CPU guard has stepped quality down).

## What the MPC cannot show (by design of the SDK skins)
The page is drawn by the MPC from JSON; a plugin cannot draw. The waveform screens are pre-drawn pictures that
follow a parameter, so compared with the desktop plugin: the wavetable screen shows a generic frame stack that
follows **Position** (not the real table's waveforms, and the same picture for your own tables), no modulation
rings, no drawable LFO shapes (they come with presets and from the desktop editor). The envelope display and the
segment / toggle buttons are untested on hardware. Pad pressure as poly aftertouch is handled, but whether MPC forwards it to plugins is unverified.

## Build
```
git submodule update --init external/mpc-plugin-builder-sdk
cmake -B build -G Ninja && cmake --build build      # also builds the ui.json generator
helix-mpc/build.sh                                  # native + arm32 + arm64, skins, validation
```
`build.sh` regenerates `ui.json` from the shared page layout (`helix-core` `UiLayout`), wraps each helix-core
source for the SDK (`tools/sync_core.sh`), runs the SDK's `tools/build.py` (Zig cross compiler, fetched on first
use), copies the hand-written skin from `skins/` and validates.

The skin is generated by `tools/make_skin.py` (Python 3 + Pillow) from `build/params.json` (written by the SDK
build). The pages are described at the top of that script by parameter name. After changing them, or adding
parameters, run `python3 helix-mpc/tools/make_skin.py --preview /tmp/skin` and commit `skins/`. `--preview` also
renders an approximate PNG of every page. `ui.json` / `make_ui` are no longer used for the MPC skin.
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
| `tools/make_skin.py`, `skins/` | the Concrete Rack skin generator and its output (TUI.json, Q-Links.json, PNGs) |
| `tools/make_ui.cpp`, `tools/skin_fonts.py` | the old generated skin (not used while `skins/` exists; `skin_fonts.py` drops the overlays' duplicate `version` keys, see `build.sh`) |
