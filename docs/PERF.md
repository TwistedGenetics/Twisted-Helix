# HELIX performance

*Measured 2026-11-12. **No MPC hardware was available.** Every number below is either measured on an x86 build
machine or an estimate for the MPC derived from those measurements, and each one is labelled as such. The device
measurement procedure is at the end. Please run it and replace the estimates.*

## The budget

From the brief: **8 voices × 2 oscillators × 4 unison + 2 filters + 3 FX must stay under ~35% of one core** on
the target.

Note that the MPC Plugin SDK's own guidance is stricter: "a few percent of one core per instance", and "well under
15% on a 32-bit MPC at 128-frame blocks" (see `docs/PLATFORM.md`). The full-spec patch is therefore a worst case.
Real presets use less: most have 1–3 unison voices and one or two FX.

## The benchmark

`helix-render --bench [--seconds 20] [--block 128] [--set id=value ...]`

- **Patch.** Osc A and Osc B both on, 4 unison voices each, sent to both filters. F1 is LP24 and F2 is BP12, with
  the serial/parallel blend at 0.5, so both filters process every voice. LFO 1 modulates F1's cutoff and Env 2
  modulates A's position, so the modulation path is live. FX: distortion + delay + reverb.
- **Performance.** An 8-note chord, re-struck every 2 s, so all 8 voices run all the time and voice starts are
  included.
- **Output.** 44.1 kHz, 128-frame blocks. Each `process()` call is timed. The bench reports the mean, the 99th
  percentile and the worst block as a percentage of real time on one core.
- **CPU guard.** `--set auto_scale=0` turns it off, so full quality is measured.

## Measured: x86-64 build machine

Intel Xeon @ 2.1 GHz (cloud VM, shared). Release build (GCC 13, `-O3`, SSE2 path). Each figure is the median of 5
runs of 20 s.

| Scenario | mean | p99 block |
|---|---|---|
| Full spec, **Quality Eco** (no mip crossfade, unison ≤ 3) | 3.3% | 5.4% |
| Full spec, **Quality Normal** | **3.8%** | 5.9% |
| Full spec, Quality High (oscillators at 2×) | 7.7% | 12.6% |
| Normal + OTT (the heavy FX) | 4.1% | 8.6% |
| Normal with 7 unison per oscillator (8 × 2 × 7) | 5.0% | 8.0% |
| Normal at 32-frame blocks | 3.8% | 6.6% |
| Normal, scalar build (`-DHELIX_FORCE_SCALAR=ON`) | 4.3% | n/a |

- **Worst blocks.** These ranged from 20% to 100%+ between runs, with no pattern. That is the shared VM's
  scheduler preempting the process, not HELIX. The SDK's host simulator, run on the same machine, reported
  worst blocks of 0.8–6%.
- **Instruction count.** Callgrind, full spec at Normal: **0.43 G instructions per second of audio** (x86-64). Eco
  is 0.37 G/s.

### What the performance pass changed

Callgrind instruction counts for a 3 s bench, start-up excluded:

| Change | Effect |
|---|---|
| One mip-level choice per block for all unison lanes (the fastest lane's, which is alias-free for all) | fewer per-lane selections |
| Table pointers hoisted out of the lane loop; frame position computed once when it doesn't move | fewer address calculations |
| Second mip level skipped when the crossfade is < 2% or > 98% | fewer table reads |
| 4-lane NEON / SSE oscillator path (phase, index, interpolation, mixing; scalar gathers) for unwarped oscillators | about 11% overall on x86 |
| Filters process whole blocks with the type `switch` outside the loop | filter cost about −50% (254 M → 128 M) |
| **Total** | **render instructions −36%** (2.1 G → 1.35 G for 3 s) |

Profile at Normal, after the pass:
- oscillators: about 55% of render time
- filters: about 15%
- reverb: about 7%
- envelopes and modulation: about 5%
- the rest is spread thin

## Estimated: the MPCs

These are estimates, not measurements.

### Method
1. Start from the x86 instruction count: 0.43 G/s.
2. Allow about 20% more instructions on ARM (separate loads and stores, fewer addressing modes): about 0.5 G/s.
3. Divide by the clock speed times an assumed IPC (instructions per cycle) for each core.

| Device | Core | Assumed IPC | Estimate: full spec, Normal | Eco |
|---|---|---|---|---|
| Gen1: MPC One / One+ / **One Retro**, Live II, Force (RK3288) | Cortex-A17 @ 1.8 GHz | 1.0–1.3 | **≈ 21–28% of one core** | ≈ 18–24% |
| Gen2: MPC Live III and later 64-bit units, on the big cores | Cortex-A76 class @ ~2.2–2.4 GHz | 2.0–2.5 | ≈ 8–11% | ≈ 7–10% |
| Gen2 if MPC schedules the plugin on a little core | Cortex-A55 @ ~1.8 GHz | 0.8–1.0 | ≈ 28–35% | ≈ 24–30% |

(The community's on-device work puts the Live III's audio workers on its fast cores, 4–7; see
`sd88me/mpc-vst-plugins` docs/BENCH.md.)

### Reading the estimates
- **Gen1 (MPC One Retro).** The full-spec patch should fit the brief's 35%, but not the SDK's "well under 15%". On a
  busy Gen1 project, use presets with fewer unison voices, or Quality Eco. The CPU guard (below) does this
  automatically when HELIX's own load passes the budget.
- **Gen2.** Comfortable.
- **Quality High.** Doubles the oscillator cost. It is meant for the desktop or Gen2. The CPU guard turns it off
  first under load.

## CPU guard (automatic scaling)

`Engine` times every `process()` call and keeps a smoothed load figure. When the CPU Guard parameter is on (the
default) and the load is above the budget (default 0.35 of one core), it steps one level down. It waits at least
0.25 s between steps. It steps back up after 2 s below 60% of the budget.

| Level | Effect |
|---|---|
| 1 | 2× oscillator oversampling off (Quality High → Normal) |
| 2 | mip crossfade off, unison capped at 5 |
| 3 | unison capped at 3 |
| 4 | unison capped at 2 |

- **Where you see it.** The desktop CPU readout shows `ECO<level>`. The MPC CPU readout shows `23% ECO2`.
- **Offline.** Offline rendering (`helix-render`, the tests) runs with the guard off (`Engine::setRealtime(false)`),
  so output never depends on machine speed. The ARM test runs under qemu caught this: the guard was engaging
  because emulation is slow, and the golden renders drifted.
- **Per voice.** `Engine::voiceLoad(v)` gives each voice's share of real time over the last second (per-voice CPU
  accounting).

## Correctness on ARM (no timing)

The core, the render CLI and all 8 test suites were cross-built with the MPC's own targets: Zig,
`arm-linux-gnueabihf` with `-mcpu=cortex_a17` + NEON, and `aarch64-linux-gnu` with `-mcpu=cortex_a55`. They were run
under qemu-user, and every suite passes on both, including:
- the aliasing test
- the golden render compare of all 24 presets, against goldens made on x86
- the render null tests

Two real bugs were found this way and fixed:
- the CPU-guard nondeterminism above
- directory scanning on 32-bit ARM. `readdir` without large-file support fails with EOVERFLOW on filesystems that
  hand out 64-bit directory offsets, so the scanners now use their own LFS `readdir`. No directory walk can throw
  any more.

qemu's timing says nothing about real hardware, so no ARM CPU numbers come from it.

## Measuring on a device (please do this)

1. **In the plugin (simplest).** Load HELIX on an MPC and play the full-spec situation: 8 held notes, a preset with
   2 × 4 unison and 3 FX. The BROWSE page's **CPU** readout is HELIX's own load. Compare it with MPC's own CPU
   meter.
2. **The bench binary.**
   ```
   cmake -B build-arm32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/zig-arm32.cmake && cmake --build build-arm32 --target helix-render
   # Gen2: cmake/zig-aarch64.cmake / build-aarch64
   ```
   Copy `build-*/helix-render/helix-render` to the device and run `./helix-render --bench --seconds 20` (add
   `--set quality=0` for Eco). Developer mode gives SSH (MPC OS 3.10.1). The SDK asks plugin developers not to run
   other commands on the device, so do this only on your own unit, at your own risk. The bench runs at normal
   priority, so MPC's real-time audio wins.
3. **The SDK host simulator, natively.** `MPC_PLUGIN_FOLDER=helix-mpc external/mpc-plugin-builder-sdk/.sdk-tools/host_sim build/helix-mpc/helix-native.so test --playing`
   runs the plugin the way MPC does (load sequence, state round trip, 32/128/512-frame blocks, timing for
   orientation).

When real numbers exist, put them in a "Measured: MPC" section here and recalibrate the CPU guard's default budget.
