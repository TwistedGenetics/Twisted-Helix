// Master OTT behaves like the OTT preset: a quiet band is lifted (upward 4.17:1 + make-up), a loud one squashed
// (1:66), Depth 0 / off pass the input untouched, meters read the band level. Also: filter envelope amount.
#include "TestUtil.h"
#include "helix/Engine.h"
#include "helix/MasterOtt.h"
#include <cmath>
#include <vector>

using namespace helix;

static double gainDb(float inDb, float hz, float depth = 1.0f, MasterOtt* keep = nullptr, bool on = true)
{
    MasterOtt local;
    MasterOtt& o = keep ? *keep : local;
    const double sr = 48000.0;
    o.prepare(sr);
    float p[kNumParams];
    for (int i = 0; i < kNumParams; ++i) p[i] = paramDef(i).def;
    p[P_MOTT_ON] = on ? 1.0f : 0.0f;
    p[P_MOTT_DEPTH] = depth;
    const float a = dbToGain(inDb);
    const int n = 512, blocks = 200;
    std::vector<float> l(n), r(n);
    double ein = 0, eout = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < n; ++i) { l[(size_t)i] = r[(size_t)i] = a * (float)std::sin(2.0 * M_PI * hz * (b * n + i) / sr); }
        double si = 0;
        for (float v : l) si += (double)v * v;
        o.process(l.data(), r.data(), n, p);
        if (b > blocks / 2) { ein += si; for (float v : l) eout += (double)v * v; }
    }
    return 10.0 * std::log10(eout / ein);
}

static double zc(Engine& e, float amt)
{
    e.params().set(P_F1_ENV, amt);
    std::vector<float> l(256), r(256), out;
    Transport tr;
    MidiMsg on = MidiMsg::make(0, 0x90, 36, 110), off = MidiMsg::make(0, 0x80, 36, 0);
    for (int b = 0; b < 60; ++b)
    {
        e.process(l.data(), r.data(), 256, b == 0 ? &on : nullptr, b == 0 ? 1 : 0, tr);
        if (b > 30) out.insert(out.end(), l.begin(), l.end());
    }
    for (int b = 0; b < 200; ++b) e.process(l.data(), r.data(), 256, b == 0 ? &off : nullptr, b == 0 ? 1 : 0, tr);
    double d = 0, a = 0;   // brightness: energy of the first difference over the energy
    for (size_t i = 1; i < out.size(); ++i) { d += (double)(out[i] - out[i - 1]) * (out[i] - out[i - 1]); a += (double)out[i] * out[i]; }
    return d / std::max(a, 1e-12);
}

int main()
{
    // quiet mid band (-50 dB peak, below -41.8): lifted by (8.2 x (1 - 1/4.17)) + 5.7 make-up ~ +11.9 dB
    const double quiet = gainDb(-50.0f, 1000.0f);
    CHECK(std::fabs(quiet - 11.9) < 2.0, "quiet band gain %.2f dB (want ~+11.9)", quiet);
    // loud mid band (-10 dB peak; the detector sits a few dB under a sine's peak): squashed by ~1:66 over -30.2,
    // less the +5.7 make-up: around -10 dB
    const double loud = gainDb(-10.0f, 1000.0f);
    CHECK(loud < -7.0 && loud > -16.0, "loud band gain %.2f dB", loud);
    const double louder = gainDb(0.0f, 1000.0f);
    CHECK(louder < loud - 6.0, "1:66: 10 dB more in must come out much more squashed (%.2f vs %.2f)", louder, loud);
    // in between the thresholds: only the make-up (low band +10.3)
    const double mid = gainDb(-37.0f, 50.0f);
    CHECK(std::fabs(mid - 10.3) < 2.0, "between thresholds %.2f dB (want ~+10.3)", mid);
    CHECK(std::fabs(gainDb(-20.0f, 1000.0f, 0.0f)) < 0.01, "depth 0 must pass the input");
    CHECK(std::fabs(gainDb(-20.0f, 1000.0f, 1.0f, nullptr, false)) < 1e-6, "off must pass the input");
    MasterOtt m;
    gainDb(-30.0f, 6000.0f, 1.0f, &m);
    CHECK(std::fabs(m.bandLevel(2) + 30.0f) < 3.0f, "high band meter %.1f", m.bandLevel(2));
    CHECK(m.bandLevel(0) < -50.0f, "low band meter should be quiet (%.1f)", m.bandLevel(0));
    CHECK(m.bandGain(2) < 0.0f, "high band at -30 is squashed (%.1f)", m.bandGain(2));

    // Filter envelope: Env Amt +100% opens a dark filter with Filter Env at sustain 1
    Engine e;
    e.prepare(44100.0, 256);
    e.setRealtime(false);
    e.params().set(P_F1_ON, 1.0f);
    e.params().set(P_A_POS, 0.66f);   // a harmonic-rich frame (position 0 is a sine)
    e.params().set(P_F1_CUTOFF, 150.0f);
    e.params().set(P_SUB_ON, 0.0f);
    e.params().set(P_E2_S, 1.0f);
    const double dark = zc(e, 0.0f), open = zc(e, 1.0f), shut = zc(e, -1.0f);
    CHECK(open > dark * 3, "env amt + did not open (%g vs %g)", open, dark);
    CHECK(shut < dark, "env amt - did not close (%g vs %g)", shut, dark);
    CHECK(choices::modSource[2] == "Filter Env" && std::string(paramDef(P_E2_A).name) == "FEnv Attack", "filter env naming");
    TEST_MAIN_END();
}
