// The modulation oscillator: off changes nothing; each route (ring mod, phase, position, filter FM)
// changes the sound, stays finite, and does what it says.
#include "TestUtil.h"
#include "helix/Engine.h"
#include "helix/ModSystem.h"
#include <cmath>
#include <vector>

using namespace helix;

static std::vector<float> render(void (*setup)(ParamStore&), int blocks = 80)
{
    Engine e;
    e.prepare(44100.0, 256);
    setup(e.params());
    std::vector<float> out;
    std::vector<float> l(256), r(256);
    Transport tr;
    MidiMsg on = MidiMsg::make(0, 0x90, 45, 127);
    for (int b = 0; b < blocks; ++b)
    {
        e.process(l.data(), r.data(), 256, b == 0 ? &on : nullptr, b == 0 ? 1 : 0, tr);
        out.insert(out.end(), l.begin(), l.end());
    }
    return out;
}

static double diff(const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0, e = 0;
    for (size_t i = 0; i < a.size(); ++i) { d += (a[i] - b[i]) * (a[i] - b[i]); e += a[i] * a[i]; }
    return d / std::max(e, 1e-12);
}

static bool finite(const std::vector<float>& a)
{
    for (float x : a) if (!std::isfinite(x) || std::fabs(x) > 8.0f) return false;
    return true;
}

static void base(ParamStore& p) { p.set(P_F1_CUTOFF, 3000.0f); }
static void moOnly(ParamStore& p) { base(p); p.set(P_MO_ON, 1.0f); }
static void rm(ParamStore& p) { moOnly(p); p.set(P_MO_PITCH, 7.0f); p.set(P_MO_RM_TGT, 3.0f); p.set(P_MO_RM_AMT, 1.0f); }
static void pm(ParamStore& p) { moOnly(p); p.set(P_MO_PITCH, 12.0f); p.set(P_MO_PM_TGT, 3.0f); p.set(P_MO_PM_AMT, 0.5f); }
static void pos(ParamStore& p) { moOnly(p); p.set(P_MO_PITCH, -12.0f); p.set(P_MO_POS_TGT, 3.0f); p.set(P_MO_POS_AMT, 1.0f); }
static void ffm(ParamStore& p) { moOnly(p); p.set(P_F1_CUTOFF, 800.0f); p.set(P_F1_RES, 0.6f); p.set(P_MO_FM_TGT, 3.0f); p.set(P_MO_FM_AMT, 0.6f); }
static void ffmMega(ParamStore& p) { ffm(p); p.set(P_F1_MODEL, 13.0f); }
static void all(ParamStore& p) { rm(p); p.set(P_MO_PM_TGT, 1.0f); p.set(P_MO_POS_TGT, 2.0f); p.set(P_MO_FM_TGT, 3.0f);
                             p.set(P_A_UNISON, 7.0f); p.set(P_QUALITY, 2.0f); p.set(P_F2_ON, 1.0f); }

int main()
{
    const auto ref = render(base);
    // On but routed nowhere: identical to off
    CHECK(diff(render(moOnly), ref) < 1e-12, "mod osc with no routes changed the sound");
    struct Case { void (*f)(ParamStore&); const char* name; };
    const Case cases[] = {{rm, "ring mod"}, {pm, "phase"}, {pos, "position"}, {ffm, "filter FM"},
                          {ffmMega, "filter FM (Mega model)"}, {all, "everything"}};
    for (const Case& c : cases)
    {
        const auto out = render(c.f);
        CHECK(finite(out), "%s: non-finite or runaway output", c.name);
        const double d = diff(out, ref);
        CHECK(d > 0.01, "%s: made no difference (%.4f)", c.name, d);
    }
    // Ring mod at full amount with the mod osc a fifth up removes the dry fundamental's dominance: the
    // signal now swings through zero every mod-osc cycle, so its average level drops.
    {
        const auto out = render(rm);
        double eo = 0, er = 0;
        for (size_t i = 4096; i < out.size(); ++i) { eo += out[i] * out[i]; er += ref[i] * ref[i]; }
        CHECK(eo < er * 0.8, "ring mod did not change the level as expected (%.3f vs %.3f)", eo, er);
    }
    // Performer curve library: the first six curves are exactly the old six shapes (old presets unchanged),
    // and every curve stays in 0..1.
    {
        float p[kNumParams] = {};
        p[P_P1_STEPS] = 2.0f; p[P_P1_S1] = 0.8f; p[P_P1_S2] = -0.4f;
        for (int c = 0; c < 6; ++c)
        {
            p[P_P1_C1] = (float)c;
            for (int i = 0; i < 10; ++i)
            {
                const float t = i / 10.0f, v = 0.8f, vn = -0.4f;
                float old = 0.0f;
                switch (c)
                {
                    case 0: old = v; break;
                    case 1: old = v * t; break;
                    case 2: old = v * (1.0f - t); break;
                    case 3: old = v + (vn - v) * (0.5f - 0.5f * std::cos(kPi * t)); break;
                    case 4: old = v * (1.0f - std::fabs(2.0f * t - 1.0f)); break;
                    case 5: old = v * (1.0f - t) * (1.0f - t) * (1.0f - t); break;
                }
                CHECK(std::fabs(performerValue(p, 0, t) - old) < 1e-5f, "curve %d differs from the old shape at %.1f", c, (double)t);
            }
        }
        for (int c = 0; c < kNumPerfCurves; ++c)
            for (int i = 0; i <= 100; ++i)
            {
                const float y = perfCurve(c, i / 100.0f * 0.9999f);
                CHECK(y >= -1e-5f && y <= 1.0f + 1e-5f, "curve %d out of range: %f", c, (double)y);
            }
    }
    TEST_MAIN_END();
}
