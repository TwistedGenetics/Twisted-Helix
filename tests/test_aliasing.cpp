// Aliasing test: play a saw from a wavetable at high notes and measure, with an FFT, the strongest spectral
// component that is NOT a harmonic of the note. With the band-limited mipmaps this must stay far below the
// fundamental. A naive (non-band-limited) saw is measured too, to prove the test can see aliasing.
#include "TestUtil.h"
#include "helix/Engine.h"
#include "helix/FFT.h"
#include "helix/Math.h"
#include "helix/WavetableGen.h"
#include <vector>

using namespace helix;

// Worst non-harmonic peak relative to the strongest component, in dB.
static double aliasLevelDb(const std::vector<float>& x, double sr, double f0)
{
    const int n = (int)x.size();
    std::vector<double> mag = magnitudeSpectrum(x.data(), n);
    double ref = 0.0;
    for (double m : mag) ref = std::max(ref, m);
    const double binHz = sr / n;
    double worst = 0.0;
    for (size_t b = 3; b < mag.size(); ++b)
    {
        const double hz = b * binHz;
        if (hz < 30.0) continue;
        const double h = hz / f0;
        const double dist = std::fabs(h - std::round(h)) * f0 / binHz;   // distance to nearest harmonic, in bins
        if (std::round(h) >= 1 && dist < 6.0) continue;                 // inside a harmonic's window lobe
        worst = std::max(worst, mag[b]);
    }
    return 20.0 * std::log10(std::max(worst, 1e-12) / ref);
}

static std::vector<float> renderNote(Engine& e, int note, int n, double sr)
{
    std::vector<float> l((size_t)n), r((size_t)n), warm(4096), warmR(4096);
    MidiMsg on = MidiMsg::make(0, 0x90, (uint8_t)note, 127);
    Transport tr;
    e.process(warm.data(), warmR.data(), 4096, &on, 1, tr);   // skip the attack
    e.process(l.data(), r.data(), n, nullptr, 0, tr);
    MidiMsg off = MidiMsg::make(0, 0xB0, 120, 0);
    e.process(warm.data(), warmR.data(), 16, &off, 1, tr);
    return l;
}

int main()
{
    const double sr = 44100.0;
    const int n = 16384;
    Engine e;
    e.prepare(sr, n);
    e.setTable(kSlotA, makeBuiltinTable(BuiltinTable::BasicShapes));
    e.params().set(P_A_POS, 2.0f / 3.0f);   // pure saw frame
    e.params().set(P_A_RAND, 0.0f);
    e.params().set(P_F1_ON, 0.0f);
    e.params().set(P_E1_A, 0.5f);
    e.params().set(P_E1_S, 1.0f);
    e.params().set(P_VEL_SENS, 0.0f);

    double worstDb = -400.0;
    for (int note = 60; note <= 120; note += 3)
    {
        const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        std::vector<float> x = renderNote(e, note, n, sr);
        const double db = aliasLevelDb(x, sr, f0);
        worstDb = std::max(worstDb, db);
        std::printf("note %3d (%7.1f Hz): worst alias %6.1f dB\n", note, f0, db);
        CHECK(db < -70.0, "note %d aliases at %.1f dB (limit -70 dB)", note, db);
    }

    // Reference: naive saw at a high note must show strong aliasing, otherwise the measurement is broken.
    {
        const int note = 100;
        const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        std::vector<float> naive((size_t)n);
        double ph = 0.0;
        for (int i = 0; i < n; ++i) { naive[(size_t)i] = (float)(2.0 * ph - 1.0); ph += f0 / sr; ph -= std::floor(ph); }
        const double db = aliasLevelDb(naive, sr, f0);
        std::printf("naive saw note %d: worst alias %.1f dB (must be loud)\n", note, db);
        CHECK(db > -40.0, "naive reference should alias, got %.1f dB", db);
    }
    std::printf("band-limited worst case: %.1f dB\n", worstDb);
    TEST_MAIN_END();
}
