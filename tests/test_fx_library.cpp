// The effects library: names match the parameter's choice list; every effect, at its defaults and with every knob
// at both ends, stays finite and bounded and does something; knob names and texts exist for the knobs it uses.
#include "TestUtil.h"
#include "helix/Denormals.h"
#include "helix/FxLibrary.h"
#include "helix/Params.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace helix;

static bool run(fxlib::Slot& s, int type, float mix, const float* knobs, double& outEnergy, float& peak, double& secs)
{
    const int n = 32, blocks = 44100 * 2 / n;
    float l[n], r[n];
    uint32_t rng = 1;
    outEnergy = 0; peak = 0;
    fxlib::Context c;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        c.beat = b * n / 44100.0 * 2.0;
        for (int i = 0; i < n; ++i)
        {
            const int t = b * n + i;
            rng = rng * 1664525u + 1013904223u;
            const float noise = ((int)(rng >> 8) / 8388608.0f - 1.0f);
            // a chord stab every half second, then silence (tails must die out, not blow up)
            const float env = (t % 22050) < 6000 ? 0.4f : 0.0f;
            l[i] = env * (0.6f * std::sin(t * 0.0393f) + 0.3f * std::sin(t * 0.0523f) + 0.1f * noise);
            r[i] = env * (0.6f * std::sin(t * 0.0401f) + 0.3f * std::sin(t * 0.0611f) + 0.1f * noise);
        }
        s.process(l, r, n, type, mix, knobs, c);
        for (int i = 0; i < n; ++i)
        {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) return false;
            peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
            outEnergy += l[i] * l[i] + r[i] * r[i];
        }
    }
    secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

int main()
{
    ScopedFlushDenormals ftz;   // as the engine runs them
    CHECK((int)choices::fxLibType.size() == fxlib::numTypes(), "type list %d vs %d", (int)choices::fxLibType.size(), fxlib::numTypes());
    for (int t = 0; t < fxlib::numTypes() && t < (int)choices::fxLibType.size(); ++t)
        CHECK(choices::fxLibType[(size_t)t] == fxlib::typeName(t), "type %d: %s vs %s", t, choices::fxLibType[(size_t)t].c_str(), fxlib::typeName(t));

    fxlib::Slot s;
    s.prepare(44100.0, 32);
    double dryE = 0, secs = 0; float peak = 0;
    const float zero[6] = {};
    run(s, 0, 1.0f, zero, dryE, peak, secs);
    std::printf("%-14s %8s %8s %8s\n", "effect", "level", "peak", "x rt");
    for (int t = 1; t < fxlib::numTypes(); ++t)
    {
        float k[6];
        int used = 0;
        for (int i = 0; i < 6; ++i)
        {
            k[i] = fxlib::knobDefault(t, i);
            const std::string nm = fxlib::knobName(t, i, k);
            if (!nm.empty())
            {
                ++used;
                CHECK(!fxlib::knobText(t, i, k[i], k).empty(), "%s knob %d (%s) has no text", fxlib::typeName(t), i, nm.c_str());
            }
        }
        CHECK(used > 0, "%s has no knobs", fxlib::typeName(t));
        s.reset();
        double e = 0;
        CHECK(run(s, t, 1.0f, k, e, peak, secs), "%s: not finite at defaults", fxlib::typeName(t));
        std::printf("%-14s %7.2f%% %8.2f %8.0f\n", fxlib::typeName(t), 100.0 * e / dryE, (double)peak, 2.0 / std::max(secs, 1e-9));
        CHECK(peak < 8.0f, "%s: peak %.2f", fxlib::typeName(t), (double)peak);
        CHECK(e > dryE * 1e-3, "%s: silent", fxlib::typeName(t));
        // every knob to both ends
        for (int i = 0; i < 6; ++i)
            for (float v : {0.0f, 1.0f})
            {
                float kk[6];
                for (int j = 0; j < 6; ++j) kk[j] = k[j];
                kk[i] = v;
                s.reset();
                double e2 = 0; float p2 = 0;
                CHECK(run(s, t, 1.0f, kk, e2, p2, secs), "%s: knob %d = %.0f not finite", fxlib::typeName(t), i, (double)v);
                CHECK(p2 < 12.0f, "%s: knob %d = %.0f peak %.2f", fxlib::typeName(t), i, (double)v, (double)p2);
            }
    }
    // switching types while running: no blow-up
    for (int t = 0; t < 200; ++t)
    {
        double e = 0;
        float k[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
        float l[32] = {}, r[32] = {};
        for (int i = 0; i < 32; ++i) l[i] = r[i] = 0.3f * std::sin(i * 0.3f);
        s.process(l, r, 32, (t * 7) % fxlib::numTypes(), 0.5f, k, fxlib::Context());
        for (int i = 0; i < 32; ++i) e += l[i] * l[i];
        CHECK(std::isfinite(e), "switching types went non-finite");
    }
    TEST_MAIN_END();
}
