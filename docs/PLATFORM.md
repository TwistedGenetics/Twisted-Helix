# HELIX on the MPC: platform investigation

*Investigated 2026-11-12. Everything here was read from source code and docs, not tested on hardware. Where
something is not known, this file says so.*

## Short answer

**Route (a), a native MPC plugin through an official SDK, now exists and is the route HELIX uses.**

inMusic released the **MPC Plugin SDK** with MPC OS 3.10.1:
[`inmusic-software/mpc-plugin-builder-sdk`](https://github.com/inmusic-software/mpc-plugin-builder-sdk)
(MIT, read at commit `febd2ca`, 2026-11-07). The press covered it on 2026-11-12, the day of this investigation.
A third-party plugin is a Linux VST 2.4-ABI shared library that the MPC loads into its own process. It sits in
`<drive>/VST2 Plugins/<Manufacturer> - VST - <Name>/`, has a JSON-skin touch UI, and has Q-Link maps.

| Route | Possible? | Notes |
|---|---|---|
| **(a) Native plugin, official SDK** | **Yes, on MPC OS ≥ 3.10.1** | The user turns on *Preferences → Enhancements (Beta) → Enable Enhancements* and *External Plugin Support*. No rooting. One 32-bit and one 64-bit `.so` in the same folder. |
| (b) Standalone ALSA/JACK process driven by MIDI | Technically possible on a modded (root SSH) unit, not supported | inMusic's open-source release is only the Linux base. Running a daemon needs root, which the stock OS doesn't give. MPC OS 3.10.0 removed the known autorun-stick root route (community report, `sd88me/mpc-vst-plugins` docs/GEN2.md). Not pursued. |
| (c) Desktop only | Not needed as the end state | HELIX still ships a desktop VST3/AU/Standalone through JUCE (`helix-desktop/`) for writing patches in Ableton. |

## What I looked at

### 1. inMusic's open-source repositories

- **`inmusic-software/az0x-open-source`**: branches `mpc-g1/3.10`, `mpc-g2/3.10`, `engine-os-g1/5.3`,
  `engine-os-g2/5.3`, `mpc-sample/1.3.0`. As expected, this is the **embedded Linux base** (kernel, glibc,
  toolchain sources and so on) for GPL compliance. It is **not MPC OS** and has no plugin API.
  Twisted-MPC-Breaks pins its G1 toolchain from it: linux-az01 6.18.44 PREEMPT_RT on RK3288, glibc 2.39,
  GCC 13.4.
- **`inmusic-software/mpc-plugin-builder-sdk`**: the official SDK described below.

### 2. Twisted-MPC-Breaks (your repo)

`TwistedGenetics/Twisted-MPC-Breaks` uses the **community** route from before the SDK:

- `src/MpcHost.cpp` wraps the JUCE Twisted Breaks processor behind `sd88me/mpc-vst-plugins`'
  `wrapper/engine.h`. That interface is int16 interleaved, 128-frame blocks at 44.1 kHz, with string key/value params.
- The community's hand-written VST2 shim (`vst2_wrap.c`) is what MPC loads.
- Installation needs a root-SSH (modded) unit, and the plugin is registered by editing `MPC.settings`.
- It is a G1-only armv7 build with JUCE compiled in. Its README says Gen2 is "locked down and not supported",
  which the community has since partly worked around.

**What HELIX takes from it:** the build discipline (pinned toolchain, glibc ceiling, static C++ runtime,
only `VSTPluginMain` exported, append-only parameter list, offline host tests in CI).
**What HELIX does not take:** the community wrapper and the `MPC.settings` registration. The official SDK
replaces both without root, takes float buffers rather than int16, and loads from a `VST2 Plugins` folder.
Both routes use the same VST2 ABI and the same skin format. Porting Twisted Breaks to the SDK would be mostly
a matter of the folder layout.

### 3. Has the MPC SDK / plugin API been released?

**Yes.** MPC OS 3.10.1 and the MPC Plugin SDK were announced at ADC26. What the SDK actually gives you:

| Area | Fact (from the SDK docs) | Effect on HELIX |
|---|---|---|
| Devices | 32-bit: X, X SE, Live, Live II, One, One+, Key 61, Key 37, Force. 64-bit: Live III, XL "and later". | Build both. The **MPC One Retro** has the MPC One's quad-core ARM, so it is a 32-bit (Gen1, Cortex-A17 class) target. The **MPC One G2** is the 8-core Gen2 model; the community lists it as aarch64. *Unverified:* the SDK's own list doesn't name the One G2. |
| Format | VST 2.4 ABI, `processReplacing`, float, non-interleaved, **in place** (in == out pointers) | `helix-core` renders float stereo; the adapter respects in-place I/O. |
| Sample rate | **44.1 kHz fixed** | The core stays sample-rate agnostic anyway (desktop runs at 48/96k). |
| Block size | 32–512 frames per call (user setting); announced size is only a maximum | The core handles any block length and splits internally into 32-sample control-rate blocks. |
| Threads | `process` on an RT thread. Param writes from the UI thread can happen *during* `process`. State load and lifecycle run on the UI thread. | Params are atomics. Table and preset loads run on a worker thread, and the audio thread swaps a pointer. |
| MIDI | Instruments get note on/off, CC, pitch bend, aftertouch, program change and sysex, sample-accurate. MIDI out is discarded. | Fine for a synth. **Poly aftertouch (pad pressure): unverified.** The docs say "aftertouch" without saying channel or poly. HELIX handles both `0xA0` and `0xD0`; whether MPC forwards per-pad pressure is a device test. |
| Transport | Tempo, ppq, bar start, playing, every block | LFO, performer and delay sync to the MPC clock. |
| Params | Float 0..1. Count fixed at load. Names and value text queried. Display refresh via `audioMasterUpdateDisplay` ("use sparingly"). **Param 0 is nudged on every prepare.** | Param 0 = Master Volume. The parameter list is **append-only** (IDs saved by string, projects save the index). |
| State | Opaque chunk (≤128 MB) in project and presets | HELIX stores its JSON preset in the chunk, so desktop and MPC use the same format. |
| Programs | `addProgram` lists them in MPC's preset menu | The 24 factory presets are exposed as programs. |
| Files | `mpc::contentPath()` finds `<plugin folder>/Content/` via `/proc/self/maps`. No file I/O in `process`. | `Content/Wavetables/`, `Content/Presets/` and `Content/Samples/` live in the plugin folder. |
| Toolchain | Zig c++ (`arm-linux-gnueabihf.2.31 -mcpu=cortex_a17`, `aarch64-linux-gnu.2.31 -mcpu=cortex_a55`), or GCC/Docker | `helix-mpc` builds through the SDK's `build.py`. |
| CPU guidance | "Aim for a few percent of one core per instance". "Well under 15% on a 32-bit MPC at 128-frame blocks" | **Conflicts with the brief's 35% budget.** See below. |
| UI | **Declarative JSON skins only.** Knobs, sliders, buttons, labels, images, film strips, overlays, envelope widget, XY pad. Three fonts (Titillium Web, Roboto, Nexa); no custom fonts. **No code-drawn graphics.** | Several UI items in the brief are not possible as specified. See below. |

## What this means for the brief, item by item

### Possible as specified
- A native plugin on Gen1 (incl. One / One+ / Retro / Live II) and Gen2 64-bit units.
- MIDI from pads, keys and the sequencer. Mod wheel, pitch bend, channel aftertouch, velocity.
- Q-Links: 16 knobs per page, in blocks of four. The skin decides which parameter each knob drives, per page.
  HELIX banks 4-at-a-time as the brief asks.
- The 8 macros on Q-Links, tempo sync to the MPC clock, saving in projects, and built-in presets in MPC's preset menu.
- A light theme. Skin colours are ARGB, so a light background with dark text is possible.

### Possible with a workaround (the workaround is what HELIX does)
| Brief | Constraint | HELIX on MPC |
|---|---|---|
| Big wavetable view with 3D frames and a moving position line | No code-drawn graphics | **Stub.** A pre-rendered film strip of stacked frames could follow the *Position* parameter for **built-in** tables only (one strip per table, swapped with `visibleWhen`). A user-loaded table can't be drawn. The desktop UI has the real view. |
| Mod rings showing live modulated values | No code-drawn graphics, and value text refresh is expensive | **Not on MPC.** The desktop UI has mod rings. On MPC a knob shows its base value only. |
| CPU meter | Same | A read-only `CPU` parameter whose **value text** reads "23%". It is updated a few times a second by a worker thread through `audioMasterAutomate`. *Unverified on hardware* whether MPC repaints it. |
| BROWSER page | No text entry, no native drop-down for plugins (community finding) | Index parameters ("Preset", "Table A", "Table B") whose value text is the file name, plus a LOAD button parameter. |
| Tables in a `Wavetables/` folder on MPC storage | The SDK says load only from inside the plugin folder and never hard-code absolute paths | `Content/Wavetables/` inside the HELIX folder, scanned in the background. Drop tables there from a computer (USB mode) or a drive. |
| "Sample → Table" from **any MPC sample** | A plugin can't reach MPC's sample pool or its in-RAM samples | Sample→Table works on **WAV files in `Content/Samples/`** (copy your breaks and vocals there). The engine is the same as the desktop's. |
| Large type (≥24 px body, ≥32 px values) | Fonts limited to three families; sizes are set per text style in the skin | The skin generator in `helix-mpc/skin/` sets the sizes. *Readability needs a device check.* |
| Pads chromatic with pressure as poly aftertouch | The track's pad mode is MPC's setting, not the plugin's. Poly AT forwarding unverified | Set the track's pads to chromatic or keygroup mode in MPC. HELIX accepts poly AT and channel AT. |

### Not possible (as far as the published SDK says)
- **"Hold Shift to audition presets."** A plugin never sees the Shift button: hardware buttons go to MPC OS,
  not to plugins. **Replacement:** a `PAD BROWSE` toggle. While it's on, notes 36–51 (pads 1–16) select
  presets *bank × 16 + pad* and play them.
- **Sending MIDI** to other tracks. HELIX doesn't need it.
- **A 35%-of-one-core budget being "reliable next to drums" on Gen1.** The SDK's own guidance for a 32-bit
  MPC is **well under 15%** per instance. HELIX keeps the brief's 35% as the ceiling for the full-spec patch
  (8 voices × 2 osc × 4 unison), but also has an **Eco** quality mode and automatic unison and quality scaling
  under load, aimed at ≤15%. See `docs/PERF.md`.

## Unknowns (stubbed behind `IPlatform`)

These can only be settled on a device. Each one is behind `helix::IPlatform` (`helix-mpc/include/IPlatform.h`),
so a fix stays in the platform layer:

1. Whether per-pad pressure reaches a plugin as poly aftertouch (`0xA0`).
2. Whether value-text updates through `audioMasterAutomate` repaint on screen fast enough for the CPU readout.
3. Real CPU numbers on an RK3288 (Gen1) and on Gen2. Only x86 numbers can be measured here (`docs/PERF.md`).
4. Whether the generated skin's font sizes and light palette read well on the 7" screen.
5. Memory limits. "The MPC locks its memory and checks free memory before loading a plugin." HELIX caps a
   table at 256 × 2048 and stores mip levels compactly (about 7.6 MB for a full 256-frame table).

## Sources
- MPC Plugin SDK: https://github.com/inmusic-software/mpc-plugin-builder-sdk. Files used: `docs/host-contract.md`,
  `docs/plugin-folder-format.md`, `docs/building.md`, `docs/ui-guide.md`, `docs/ui-reference.md`,
  `docs/faq-and-legal.md`, `framework/mpc_plugin.h`.
- inMusic open-source Linux base: https://github.com/inmusic-software/az0x-open-source (branch list read with `git ls-remote`).
- Twisted-MPC-Breaks: https://github.com/TwistedGenetics/Twisted-MPC-Breaks (README, CMakeLists, `src/MpcHost.cpp`).
- Community toolkit: https://github.com/sd88me/mpc-vst-plugins (`README.md`, `wrapper/engine.h`, `docs/GEN2.md`, `docs/BENCH.md`).
- Press: Sonicstate, "ADC26: Akai's New MPC OS Opens up All Models" (2026-11-12); MusicRadar, "You can now vibe code
  plugins for your MPC with the help of Akai's official SDK"; Akai's MPC One Retro product page (quad-core ARM,
  2 GB RAM).
