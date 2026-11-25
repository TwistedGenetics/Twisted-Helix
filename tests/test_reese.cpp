// Reese: Drift moves the two oscillators apart (the sound changes, more so over time); Growl brightens and drives
// (filter opens); Phase Lock makes every note start the same; all off = unchanged.
#include "TestUtil.h"
#include "helix/Engine.h"
#include <cmath>
#include <vector>

using namespace helix;

static std::vector<float> render(void (*setup)(ParamStore&), int blocks = 120, int notes = 1)
{
    Engine e;
    e.prepare(44100.0, 256);
    setup(e.params());
    std::vector<float> out, l(256), r(256);
    Transport tr;
    for (int k = 0; k < notes; ++k)
    {
        MidiMsg on = MidiMsg::make(0, 0x90, 36, 110), off = MidiMsg::make(0, 0x80, 36, 0);
        for (int b = 0; b < blocks; ++b)
        {
            const MidiMsg* m = b == 0 ? &on : (b == blocks - 1 ? &off : nullptr);
            e.process(l.data(), r.data(), 256, m, m ? 1 : 0, tr);
            out.insert(out.end(), l.begin(), l.end());
        }
        for (int b = 0; b < 40; ++b) e.process(l.data(), r.data(), 256, nullptr, 0, tr);   // release
    }
    return out;
}

static double rel(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0, size_t to = 0)
{
    if (!to) to = a.size();
    double d = 0, e = 0;
    for (size_t i = from; i < to; ++i) { d += (a[i] - b[i]) * (a[i] - b[i]); e += a[i] * a[i]; }
    return d / std::max(e, 1e-12);
}

// zero-crossing rate as a cheap brightness measure
static double zcr(const std::vector<float>& x)
{
    int z = 0;
    for (size_t i = 1; i < x.size(); ++i) z += (x[i - 1] < 0) != (x[i] < 0);
    return (double)z / x.size();
}

static void base(ParamStore& p) { p.set(P_B_ON, 1.0f); p.set(P_F1_CUTOFF, 400.0f); p.set(P_F1_RES, 0.2f); p.set(P_RS_LOCK, 1.0f); }
static void drift(ParamStore& p) { base(p); p.set(P_RS_DRIFT, 1.0f); p.set(P_RS_DRIFT_RATE, 2.0f); }
static void growl(ParamStore& p) { base(p); p.set(P_RS_GROWL, 1.0f); }
static void unlocked(ParamStore& p) { base(p); p.set(P_RS_LOCK, 0.0f); p.set(P_A_UNISON, 7.0f); p.set(P_A_RAND, 1.0f); p.set(P_B_RAND, 1.0f); }
static void locked(ParamStore& p) { unlocked(p); p.set(P_RS_LOCK, 1.0f); }

int main()
{
    const auto ref = render(base);
    const auto d = render(drift);
    CHECK(rel(ref, d, ref.size() / 2) > 0.05, "drift did not move the oscillators (%.4f)", rel(ref, d, ref.size() / 2));
    const auto g = render(growl);
    CHECK(zcr(g) > zcr(ref) * 1.3, "growl did not open the sound (zcr %.4f vs %.4f)", zcr(g), zcr(ref));
    // Phase Lock: two consecutive notes start identically
    const auto two = render(locked, 20, 2), twoU = render(unlocked, 20, 2);
    const size_t len = 20 * 256 + 40 * 0;   // (the release blocks are not captured)
    std::vector<float> a(two.begin(), two.begin() + len), b(two.begin() + len, two.begin() + 2 * len);
    std::vector<float> c(twoU.begin(), twoU.begin() + len), e(twoU.begin() + len, twoU.begin() + 2 * len);
    CHECK(rel(a, b, 0, 1024) < 1e-6, "Phase Lock: notes start differently (%.3g)", rel(a, b, 0, 1024));
    CHECK(rel(c, e, 0, 1024) > 1e-3, "without Phase Lock the random phases should differ (%.3g)", rel(c, e, 0, 1024));
    TEST_MAIN_END();
}
