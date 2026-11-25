// Character oscillator: every engine plays (sane level, no NaN) at 44.1 and 96 kHz, levels roughly matched,
// Dive / Pitch Env bends the start; chord memory plays and releases the chord.
#include "TestUtil.h"
#include "helix/CharOsc.h"
#include "helix/Engine.h"
#include <cmath>
#include <cstdlib>
#include <vector>

using namespace helix;

static std::vector<float> play(double sr, void (*setup)(ParamStore&), int blocks, int note = 36, Engine* keep = nullptr)
{
    Engine local;
    Engine& e = keep ? *keep : local;
    e.prepare(sr, 256);
    e.setRealtime(false);
    setup(e.params());
    std::vector<float> out, l(256), r(256);
    Transport tr;
    MidiMsg on = MidiMsg::make(0, 0x90, (uint8_t)note, 110);
    for (int b = 0; b < blocks; ++b)
    {
        e.process(l.data(), r.data(), 256, b == 0 ? &on : nullptr, b == 0 ? 1 : 0, tr);
        out.insert(out.end(), l.begin(), l.end());
    }
    return out;
}

static int gType = 1;
static void coOnly(ParamStore& p)
{
    p.set(P_A_ON, 0.0f); p.set(P_B_ON, 0.0f); p.set(P_SUB_ON, 0.0f); p.set(P_NOISE_ON, 0.0f);
    p.set(P_CO_TYPE, (float)gType);
}
static void diveUp(ParamStore& p) { coOnly(p); p.set(P_CO_ENV, 1.0f); p.set(P_CO_TIME, 0.5f); }
static void chordMajor(ParamStore& p) { p.set(P_CHORD, 5.0f); }

static double rms(const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0;
    for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i];
    return std::sqrt(s / (double)(b - a));
}
static int zc(const std::vector<float>& x, size_t a, size_t b)
{
    int z = 0;
    for (size_t i = a + 1; i < b; ++i) z += (x[i - 1] < 0) != (x[i] < 0);
    return z;
}

int main()
{
    const bool verbose = std::getenv("HELIX_VERBOSE") != nullptr;
    for (double sr : {44100.0, 96000.0})
        for (gType = 1; gType < kNumCharTypes; ++gType)
        {
            const auto x = play(sr, coOnly, (int)(sr / 256));
            bool finite = true;
            float peak = 0;
            for (float v : x) { finite &= std::isfinite(v); peak = std::max(peak, std::fabs(v)); }
            const double lv = rms(x, x.size() / 2, x.size());
            if (verbose) std::printf("%-18s sr %.0f rms %.3f peak %.3f\n", choices::charType[(size_t)gType].c_str(), sr, lv, peak);
            CHECK(finite, "%s: not finite", choices::charType[(size_t)gType].c_str());
            CHECK(lv > 0.1 && lv < 0.35, "%s at %.0f: level %.3f", choices::charType[(size_t)gType].c_str(), sr, lv);
            CHECK(peak < 2.5f, "%s: peak %.2f", choices::charType[(size_t)gType].c_str(), peak);
        }

    // Hoover dive: starts high, comes down
    gType = kCharHoover;
    const auto d = play(44100.0, diveUp, 120);
    const size_t w = 4410;
    CHECK(zc(d, 0, w) > zc(d, d.size() - w, d.size()) * 1.3, "dive did not bend the start (%d vs %d)", zc(d, 0, w), zc(d, d.size() - w, d.size()));

    // Knob names follow the engine
    CHECK(std::string(charKnobName(kCharDonk, 0)) == "Harmonic" && std::string(charKnobName(kCharRollReese, 3)) == "Crunch", "knob names");

    // Chord memory: one key plays a major triad and lets it go again
    Engine e;
    play(44100.0, chordMajor, 20, 48, &e);
    CHECK(e.activeVoices() == 3, "chord voices %d", e.activeVoices());
    std::vector<float> l(256), r(256);
    Transport tr;
    MidiMsg off = MidiMsg::make(0, 0x80, 48, 0);
    e.params().set(P_CHORD, 0.0f);   // changing the chord while held must still release all three
    e.process(l.data(), r.data(), 256, &off, 1, tr);
    for (int b = 0; b < 400; ++b) e.process(l.data(), r.data(), 256, nullptr, 0, tr);
    CHECK(e.activeVoices() == 0, "chord notes left hanging: %d", e.activeVoices());
    TEST_MAIN_END();
}
