#include "TestUtil.h"
#include "Analysis.h"
#include "helix/Filter.h"

using namespace helix;

static const float kSr = 44100.0f;

// Steady-state gain of a sine through the filter, dB.
static double sineGainDb(FilterType t, float cutoff, float res, float hz)
{
    Filter f;
    f.prepare(kSr);
    f.setType(t);
    f.update(cutoff, res, 0.0f, 0.0f);
    const int n = 32768, skip = 8192;
    double in = 0, out = 0;
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * std::sin(kTwoPi * hz * (float)i / kSr);
        float l = x, r = x;
        f.process(l, r);
        if (i >= skip) { in += (double)x * x; out += (double)l * l; }
    }
    return 10.0 * std::log10(std::max(out, 1e-30) / in);
}

int main()
{
    struct Slope { FilterType t; const char* name; float passHz, stopHz; double stopMax; };
    const Slope slopes[] = {
        {FilterType::LP12, "LP12", 100, 8000, -30}, {FilterType::LP24, "LP24", 100, 8000, -55},
        {FilterType::HP12, "HP12", 8000, 125, -30}, {FilterType::HP24, "HP24", 8000, 125, -55},
        {FilterType::BP12, "BP12", 1000, 16000, -18}, {FilterType::BP24, "BP24", 1000, 16000, -36},
        {FilterType::Ladder24, "Ladder", 100, 8000, -45},
    };
    for (const Slope& s : slopes)
    {
        const double pass = sineGainDb(s.t, 1000.0f, 0.0f, s.passHz);
        const double stop = sineGainDb(s.t, 1000.0f, 0.0f, s.stopHz);
        std::printf("%-7s pass %6.1f dB  stop %6.1f dB\n", s.name, pass, stop);
        CHECK(pass > -9.0 && pass < 3.0, "%s passband %.1f dB", s.name, pass);
        CHECK(stop < s.stopMax, "%s stopband %.1f dB (want < %.0f)", s.name, stop, s.stopMax);
    }
    {
        const double notch = sineGainDb(FilterType::Notch12, 1000.0f, 0.0f, 1000.0f);
        const double away = sineGainDb(FilterType::Notch12, 1000.0f, 0.0f, 100.0f);
        std::printf("Notch12 at centre %.1f dB, away %.1f dB\n", notch, away);
        CHECK(notch < -20.0 && away > -3.0, "notch shape wrong");
        const double ph = sineGainDb(FilterType::Phaser, 1000.0f, 0.0f, 1000.0f);
        std::printf("Phaser at its notch %.1f dB\n", ph);
        CHECK(ph < -10.0, "phaser should notch at the cutoff (%.1f dB)", ph);
    }
    // Resonance raises the cutoff region.
    {
        const double flat = sineGainDb(FilterType::LP24, 1000.0f, 0.0f, 1000.0f);
        const double reso = sineGainDb(FilterType::LP24, 1000.0f, 0.9f, 1000.0f);
        CHECK(reso > flat + 10.0, "LP24 resonance peak too small (%.1f vs %.1f dB)", reso, flat);
    }
    // Comb +: peaks at multiples of the cutoff frequency, a dip half-way between.
    {
        const double peak = sineGainDb(FilterType::CombPlus, 441.0f, 0.9f, 882.0f);
        const double dip = sineGainDb(FilterType::CombPlus, 441.0f, 0.9f, 661.5f);
        std::printf("Comb+ peak %.1f dB, dip %.1f dB\n", peak, dip);
        CHECK(peak > dip + 15.0, "comb has no comb shape");
        const double peakM = sineGainDb(FilterType::CombMinus, 441.0f, 0.9f, 661.5f);
        const double dipM = sineGainDb(FilterType::CombMinus, 441.0f, 0.9f, 882.0f);
        CHECK(peakM > dipM + 15.0, "comb- should peak half-way between comb+ peaks");
    }
    // Formant: low cutoff ("A") keeps more 1 kHz than high cutoff ("U").
    {
        const double a = sineGainDb(FilterType::Formant, 20.0f, 0.5f, 1150.0f);
        const double u = sineGainDb(FilterType::Formant, 20000.0f, 0.5f, 1150.0f);
        std::printf("Formant at 1150 Hz: A %.1f dB, U %.1f dB\n", a, u);
        CHECK(a > u + 6.0, "formant morph has no effect");
    }

    // Stability: every type, full resonance and drive, cutoff jumping every block, loud noise in.
    for (int t = 0; t < (int)FilterType::Count; ++t)
    {
        Filter f;
        f.prepare(kSr);
        f.setType((FilterType)t);
        Rng rng(7u + (uint32_t)t);
        float peak = 0.0f;
        bool finite = true;
        for (int blk = 0; blk < 44100 * 4 / kCtrlBlock; ++blk)
        {
            f.update(20.0f * std::pow(1000.0f, rng.uni()), 1.0f, rng.uni(), rng.uni());
            for (int i = 0; i < kCtrlBlock; ++i)
            {
                float l = rng.bi() * 2.0f, r = rng.bi() * 2.0f;
                f.process(l, r);
                finite = finite && std::isfinite(l) && std::isfinite(r);
                peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
            }
        }
        CHECK(finite && peak < 50.0f, "filter type %d unstable (peak %g)", t, peak);
    }

    // Routing through the engine: F1 = LP 200 Hz, F2 = HP 4 kHz. Parallel keeps both ends, serial almost nothing.
    {
        Engine e;
        e.prepare(kSr, 8192);
        auto setup = [&](float blend) {
            plainPatch(e);
            e.params().set(P_A_POS, 2.0f / 3.0f);   // saw
            e.params().set(P_A_TO_F1, 1.0f);
            e.params().set(P_A_TO_F2, 1.0f);
            e.params().set(P_F1_ON, 1.0f); e.params().set(P_F1_TYPE, (float)FilterType::LP24); e.params().set(P_F1_CUTOFF, 200.0f);
            e.params().set(P_F2_ON, 1.0f); e.params().set(P_F2_TYPE, (float)FilterType::HP24); e.params().set(P_F2_CUTOFF, 4000.0f);
            e.params().set(P_FILTER_BLEND, blend);
        };
        setup(0.0f);
        const double par = rms(renderHeld(e, 45, 16384).l);
        setup(1.0f);
        const double ser = rms(renderHeld(e, 45, 16384).l);
        std::printf("parallel rms %.4f, serial rms %.4f\n", par, ser);
        CHECK(par > 4.0 * ser, "serial/parallel blend has no effect");
        // A source sent only to F2, with F2 off, still sounds (passes through).
        plainPatch(e);
        e.params().set(P_A_TO_F1, 0.0f);
        e.params().set(P_A_TO_F2, 1.0f);
        CHECK(rms(renderHeld(e, 45, 8192).l) > 0.05, "a source routed to a bypassed F2 is lost");
        e.params().set(P_A_TO_F2, 0.0f);
        CHECK(rms(renderHeld(e, 45, 8192).l) < 1e-6, "a source with both sends at 0 should be silent");
    }
    TEST_MAIN_END();
}
