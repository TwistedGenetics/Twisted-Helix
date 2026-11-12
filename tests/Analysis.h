#pragma once
#include "helix/Engine.h"
#include "helix/FFT.h"
#include <cmath>
#include <vector>

// Worst non-harmonic component relative to the strongest, dB.
inline double aliasLevelDb(const std::vector<float>& x, double sr, double f0, double minHz = 30.0)
{
    const int n = (int)x.size();
    std::vector<double> mag = helix::magnitudeSpectrum(x.data(), n);
    double ref = 0.0;
    for (double m : mag) ref = std::max(ref, m);
    const double binHz = sr / n;
    double worst = 0.0;
    for (size_t b = 3; b < mag.size(); ++b)
    {
        const double hz = b * binHz;
        if (hz < minHz) continue;
        const double h = hz / f0;
        const double dist = std::fabs(h - std::round(h)) * f0 / binHz;
        if (std::round(h) >= 1 && dist < 6.0) continue;
        worst = std::max(worst, mag[b]);
    }
    return 20.0 * std::log10(std::max(worst, 1e-12) / ref);
}

struct Stereo { std::vector<float> l, r; };

// Hold a note: skip `skip` samples of attack, return n samples. Leaves the engine silent afterwards.
inline Stereo renderHeld(helix::Engine& e, int note, int n, int skip = 4096)
{
    Stereo s; s.l.resize((size_t)n); s.r.resize((size_t)n);
    std::vector<float> wl((size_t)std::max(skip, 16)), wr(wl.size());
    helix::MidiMsg on = helix::MidiMsg::make(0, 0x90, (uint8_t)note, 127);
    helix::Transport tr;
    e.process(wl.data(), wr.data(), std::max(skip, 1), &on, 1, tr);
    e.process(s.l.data(), s.r.data(), n, nullptr, 0, tr);
    helix::MidiMsg off = helix::MidiMsg::make(0, 0xB0, 120, 0);
    e.process(wl.data(), wr.data(), 16, &off, 1, tr);
    return s;
}

inline double rms(const std::vector<float>& x) { double a = 0; for (float v : x) a += (double)v * v; return std::sqrt(a / std::max<size_t>(1, x.size())); }
inline bool allFinite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v)) return false; return true; }

// Sustained, unfiltered, velocity-insensitive patch for oscillator tests.
inline void plainPatch(helix::Engine& e)
{
    using namespace helix;
    e.params().resetToDefaults();
    e.params().set(P_A_RAND, 0.0f);
    e.params().set(P_F1_ON, 0.0f);
    e.params().set(P_E1_A, 0.5f);
    e.params().set(P_E1_S, 1.0f);
    e.params().set(P_VEL_SENS, 0.0f);
}
