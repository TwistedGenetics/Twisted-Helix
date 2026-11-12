# HELIX

A wavetable synthesizer for jungle, drum & bass and dark bass music (Reese, hoover, growl, neuro, 90s pads),
built to run **natively on Akai standalone MPCs** through the official MPC Plugin SDK (MPC OS 3.10.1+), and on
the desktop as VST3 / AU / Standalone.

| Folder | What |
|---|---|
| `helix-core/` | Pure C++17 DSP library: no JUCE, no OS calls, no allocation or locks on the audio thread |
| `helix-render/` | Offline CLI: preset + MIDI file → WAV, tests, `--bench` |
| `helix-desktop/` | JUCE VST3 / AU / Standalone wrapper |
| `helix-mpc/` | MPC Plugin SDK target behind a thin `IPlatform` layer |
| `docs/` | `PLATFORM.md` (what the MPC allows), `PERF.md` (CPU numbers) |

See `docs/PLATFORM.md` first.
