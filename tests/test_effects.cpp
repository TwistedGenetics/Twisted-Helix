#include "TestUtil.h"
#include "Analysis.h"
#include "helix/Effects.h"
#include <set>

using namespace helix;
static const float kSr = 44100.0f;

struct FxRig
{
    FxChain fx;
    float p[kNumParams];
    FxRig() { fx.prepare(kSr); for (int i = 0; i < kNumParams; ++i) p[i] = paramDef(i).def; }
    // Runs the chain over l/r in place.
    void run(std::vector<float>& l, std::vector<float>& r, bool os = false)
    {
        for (size_t pos = 0; pos < l.size(); pos += kCtrlBlock)
        {
            const int n = (int)std::min<size_t>(kCtrlBlock, l.size() - pos);
            fx.process(l.data() + pos, r.data() + pos, n, p, 120.0, os);
        }
    }
};

static std::vector<float> sine(float hz, float amp, int n)
{
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i) x[(size_t)i] = amp * std::sin(kTwoPi * hz * (float)i / kSr);
    return x;
}
static double seg(const std::vector<float>& x, size_t a, size_t b) { return rms(std::vector<float>(x.begin() + (long)a, x.begin() + (long)b)); }

int main()
{
    // Every effect alone: finite, not silent, tails die away.
    const int onParams[] = {P_DIST_ON, P_CHO_ON, P_PH_ON, P_DLY_ON, P_REV_ON, P_OTT_ON, P_EQ_ON};
    for (int k = 0; k < 7; ++k)
    {
        FxRig rig;
        rig.p[onParams[k]] = 1.0f;
        std::vector<float> l((size_t)kSr * 12, 0.0f), r = l;
        Rng rng(3);
        for (int i = 0; i < (int)kSr; ++i) { l[(size_t)i] = rng.bi() * 0.5f; r[(size_t)i] = rng.bi() * 0.5f; }
        rig.run(l, r);
        CHECK(allFinite(l) && allFinite(r), "fx %d not finite", k);
        CHECK(seg(l, 0, (size_t)kSr) > 0.01, "fx %d silent", k);
        const double tail = seg(l, (size_t)kSr * 11, (size_t)kSr * 12);
        CHECK(tail < 1e-3, "fx %d tail does not die (rms %g)", k, tail);
    }

    // Distortion adds harmonics; Crush reduces the number of distinct levels; 2x oversampling changes the result.
    {
        FxRig rig; rig.p[P_DIST_ON] = 1; rig.p[P_DIST_DRIVE] = 0.7f; rig.p[P_DIST_TONE] = 20000;
        auto l = sine(220, 0.5f, 16384), r = l;
        rig.run(l, r);
        auto mag = magnitudeSpectrum(l.data(), (int)l.size());
        const size_t h1 = (size_t)std::lround(220.0 * l.size() / kSr), h3 = 3 * h1;
        double m1 = 0, m3 = 0;
        for (size_t b = h1 - 3; b <= h1 + 3; ++b) m1 = std::max(m1, mag[b]);
        for (size_t b = h3 - 3; b <= h3 + 3; ++b) m3 = std::max(m3, mag[b]);
        std::printf("tube drive: 3rd harmonic at %.1f dB\n", 20 * std::log10(m3 / m1));
        CHECK(m3 / m1 > 0.01, "distortion adds no harmonics");

        FxRig c; c.p[P_DIST_ON] = 1; c.p[P_DIST_TYPE] = 3; c.p[P_DIST_BITS] = 4; c.p[P_DIST_DRIVE] = 0; c.p[P_DIST_TONE] = 20000;
        auto cl = sine(220, 0.9f, 8192), cr = cl;
        c.run(cl, cr);
        std::set<int> levels;
        for (size_t i = 4096; i < cl.size(); ++i) levels.insert((int)std::lround(cl[i] * 1000.0f));
        std::printf("crush 4-bit: %zu distinct levels\n", levels.size());
        CHECK(levels.size() < 40, "crush does not reduce bit depth (%zu levels)", levels.size());
    }

    // Delay: 1/8 at 120 BPM = 250 ms echo.
    {
        FxRig rig; rig.p[P_DLY_ON] = 1; rig.p[P_DLY_MODE] = 0; rig.p[P_DLY_SYNC] = 1; rig.p[P_DLY_DIV] = 10; rig.p[P_DLY_MIX] = 1; rig.p[P_DLY_TONE] = 20000;
        std::vector<float> l((size_t)kSr, 0.0f), r = l;
        l[100] = r[100] = 1.0f;
        rig.run(l, r);
        size_t peak = 200;
        for (size_t i = 200; i < l.size(); ++i) if (std::fabs(l[i]) > std::fabs(l[peak])) peak = i;
        const double ms = (double)(peak - 100) / kSr * 1000.0;
        std::printf("delay echo at %.1f ms\n", ms);
        CHECK(std::fabs(ms - 250.0) < 2.0, "synced delay time wrong (%.1f ms)", ms);
    }

    // Reverb: a real tail after an impulse, longer for higher decay.
    {
        double tails[2];
        for (int k = 0; k < 2; ++k)
        {
            FxRig rig; rig.p[P_REV_ON] = 1; rig.p[P_REV_MIX] = 1; rig.p[P_REV_DECAY] = k == 0 ? 0.3f : 0.8f; rig.p[P_REV_TYPE] = 1;
            std::vector<float> l((size_t)kSr * 3, 0.0f), r = l;
            l[0] = r[0] = 1.0f;
            rig.run(l, r);
            tails[k] = seg(l, (size_t)kSr, (size_t)kSr * 2);
        }
        std::printf("reverb tail 1-2 s: decay 0.3 -> %.2e, decay 0.8 -> %.2e\n", tails[0], tails[1]);
        CHECK(tails[1] > 1e-4 && tails[1] > 5.0 * tails[0], "reverb decay control has no effect");
    }

    // OTT: brings a quiet signal up and a loud one down.
    {
        FxRig q; q.p[P_OTT_ON] = 1; q.p[P_OTT_DEPTH] = 1;
        auto ql = sine(1000, 0.003f, 44100), qr = ql; const double qin = rms(ql); q.run(ql, qr);
        FxRig ld; ld.p[P_OTT_ON] = 1; ld.p[P_OTT_DEPTH] = 1;
        auto ll = sine(1000, 0.9f, 44100), lr = ll; const double lin = rms(ll); ld.run(ll, lr);
        const double up = 20 * std::log10(seg(ql, 22050, 44100) / qin), down = 20 * std::log10(seg(ll, 22050, 44100) / lin);
        std::printf("OTT: quiet %+.1f dB, loud %+.1f dB\n", up, down);
        CHECK(up > 6.0 && down < -3.0, "OTT does not compress both ways");
    }

    // EQ: +12 dB low shelf at 120 Hz lifts 40 Hz by ~12 dB and leaves 5 kHz alone.
    {
        FxRig rig; rig.p[P_EQ_ON] = 1; rig.p[P_EQ_LOW_GAIN] = 12;
        auto a = sine(40, 0.1f, 32768), ar = a; rig.run(a, ar);
        FxRig rig2; rig2.p[P_EQ_ON] = 1; rig2.p[P_EQ_LOW_GAIN] = 12;
        auto b = sine(5000, 0.1f, 32768), br = b; rig2.run(b, br);
        const double lo = 20 * std::log10(seg(a, 16384, 32768) / (0.1 / std::sqrt(2.0)));
        const double hi = 20 * std::log10(seg(b, 16384, 32768) / (0.1 / std::sqrt(2.0)));
        std::printf("EQ: 40 Hz %+.1f dB, 5 kHz %+.1f dB\n", lo, hi);
        CHECK(std::fabs(lo - 12.0) < 1.5 && std::fabs(hi) < 0.5, "EQ shelf wrong");
    }

    // Order matters: distortion before vs after the reverb sounds different.
    {
        auto runOrder = [](bool distFirst) {
            FxRig rig; rig.p[P_DIST_ON] = 1; rig.p[P_DIST_DRIVE] = 0.8f; rig.p[P_REV_ON] = 1; rig.p[P_REV_MIX] = 0.5f;
            rig.p[P_FX_ORDER1] = distFirst ? 0.0f : 4.0f;
            rig.p[P_FX_ORDER2] = distFirst ? 4.0f : 0.0f;
            auto l = sine(110, 0.5f, 22050), r = l; rig.run(l, r); return l;
        };
        auto a = runOrder(true), b = runOrder(false);
        double diff = 0; for (size_t i = 0; i < a.size(); ++i) diff += std::fabs(a[i] - b[i]);
        CHECK(diff / a.size() > 1e-3, "FX order has no effect");
    }
    TEST_MAIN_END();
}
