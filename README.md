# HELIX

A wavetable synthesizer for jungle, drum & bass and dark bass music (Reese, hoover, growl, neuro, 90s pads), in
the spirit of Serum and Massive. It runs **natively on Akai standalone MPCs** through inMusic's official MPC Plugin
SDK (MPC OS 3.10.1+, 32-bit Gen1 incl. MPC One / One Retro, and 64-bit Gen2), and on the desktop as
VST3 / AU / Standalone.

Read **[docs/PLATFORM.md](docs/PLATFORM.md)** for what the MPC allows and **[docs/PERF.md](docs/PERF.md)** for CPU numbers.

| Folder | What |
|---|---|
| `helix-core/` | Pure C++17 DSP library: no JUCE, no OS calls on the audio path, no allocation or locks on the audio thread, NEON / SSE / scalar paths, denormals flushed, sample-rate agnostic |
| `helix-render/` | Offline CLI: preset + MIDI file → WAV, factory preset export, `--bench` |
| `helix-desktop/` | JUCE 8 wrapper: VST3 / AU / Standalone, light-theme editor with wavetable view, mod rings, preset browser |
| `helix-mpc/` | MPC target (MPC Plugin SDK) behind the `IPlatform` layer, generated touchscreen skin, see [helix-mpc/README.md](helix-mpc/README.md) |
| `Presets/` | the 24 factory presets as JSON (`Presets/<Category>/<Name>.json`, written by `helix-render --write-factory`) |
| `tests/` | aliasing, oscillators / warps, filters, modulation + voice handling, FX, import + fuzz, presets (null + golden), MPC app |
| `external/mpc-plugin-builder-sdk` | the official MPC Plugin SDK (git submodule, pinned) |

## Sound engine (summary)
- **2 wavetable oscillators**: up to 256 × 2048 tables, frame morph, band-limited mipmaps (FFT, per octave, level
  sizes chosen so interpolation images stay below -70 dB, crossfaded), two warp slots each (Bend ±, Asym, PWM,
  Mirror, windowed Sync, Formant, FM from other osc / sub / noise, AM, RM, Quantize, Bit Red), up to 7 unison voices
  (detune, blend, spread, random phase), tuning, phase, level, pan. Quality *High* runs them at 2× with a half-band
  decimator.
- **Sub** (sine / tri / square / saw, -1 / -2 oct, direct-out past the filters) and **noise** (white, pink, brown,
  vinyl, tape hiss, crackle; pitch tracking, loop / one-shot).
- **2 filters**: SVF LP/BP/HP/Notch 12 & 24, Ladder 24, Comb ±, Formant (A-E-I-O-U), Phaser; cutoff, resonance,
  drive, key tracking, fat; per-source sends and a serial ↔ parallel blend.
- **Modulation**: 4 curved ADSRs (Env 1 = amp), 4 LFOs (incl. breakpoint and step shapes, tempo sync, retrig /
  free / one-shot, rise, smooth), 2 performers, 16-slot matrix with aux source and curve, 8 macros, velocity, poly
  pressure, channel AT, mod wheel, bend, note, random.
- **Voices**: 8, poly / mono / legato, portamento (always / legato-only), oldest / quietest stealing with a
  click-free 3 ms fade, sustain pedal, per-voice CPU accounting, CPU guard.
- **FX** (re-orderable): distortion (tube, hard clip, fold, crush, asym), chorus / ensemble, phaser, stereo /
  ping-pong delay, plate / hall FDN reverb, 3-band OTT, 3-band EQ.
- **Wavetables**: Serum WAVs (`clm` chunk), single cycles, folders of cycles, Sample → Table (pitch detection,
  zero-crossing alignment), 8 built-in generators; background scanning, asynchronous loading.
- **Presets**: versioned JSON, 24 factory presets, Mutate 10%.

## Build
Requirements: CMake ≥ 3.22, a C++17 compiler, Ninja (optional). Python 3 for the MPC build.

```
git submodule update --init                     # the MPC Plugin SDK
cmake -B build -G Ninja && cmake --build build  # core, render CLI, tests, MPC app + ui generator
ctest --test-dir build                          # all tests

# Desktop plugin (fetches JUCE 8.0.4, or -DHELIX_JUCE_DIR=<checkout>)
cmake -B build-desktop -G Ninja -DHELIX_BUILD_DESKTOP=ON && cmake --build build-desktop
#   macOS: VST3 + AU + Standalone in build-desktop/helix-desktop/HelixDesktop_artefacts/

# MPC plugin folder (native + arm32 + arm64, skins, validation)
helix-mpc/build.sh                              # -> helix-mpc/build/Twisted Genetics - VST - HELIX/

# Render and benchmark
build/helix-render/helix-render --factory "Classic Reese" --midi riff.mid --out reese.wav
build/helix-render/helix-render --bench --seconds 20
```

ARM builds of the core, the CLI and the tests (for the bench on a device, or tests under qemu):
`cmake -B build-arm32 -DCMAKE_TOOLCHAIN_FILE=cmake/zig-arm32.cmake` (or `zig-aarch64.cmake`); needs
`pip install ziglang`.

## Licences
HELIX itself: no licence chosen yet (all rights reserved until one is). The MPC Plugin SDK is MIT (inMusic). JUCE (desktop wrapper only) is
dual-licensed AGPLv3 / commercial: check its terms before distributing a desktop binary.
