#include "TestUtil.h"
#include "Analysis.h"
#include "helix/WavetableGen.h"

using namespace helix;

int main()
{
    const double sr = 44100.0;
    Engine e;
    e.prepare(sr, 16384);

    // 1. Warps that squeeze the phase read a band-limited mip level; warp amounts are capped so a squeezed cycle
    //    never folds over Nyquist. Kinked phase maps (PWM, Bend, Asym, Mirror) still add 1/h^2 partials that no
    //    mipmap can remove; Quality High runs the oscillators at 2x with a half-band decimator for those.
    //    Limits are per register: bass/mid (notes 24-72) and lead (73-96).
    struct W { Warp w; float amt; const char* name; };
    const W warps[] = {{Warp::Off, 0.0f, "None"}, {Warp::Pwm, 0.9f, "PWM"}, {Warp::BendPlus, 1.0f, "Bend+"},
                       {Warp::Asym, 0.8f, "Asym"}, {Warp::Mirror, 1.0f, "Mirror"}, {Warp::Sync, 0.6f, "Sync"},
                       {Warp::Formant, 0.5f, "Formant"}};
    std::printf("worst alias, dB          normal:bass  normal:lead   high:bass   high:lead\n");
    for (const W& w : warps)
    {
        double worst[2][2] = {{-400, -400}, {-400, -400}};   // [quality][register]
        for (int q = 0; q < 2; ++q)
            for (int note = 24; note <= 96; note += 6)
            {
                plainPatch(e);
                e.params().set(P_QUALITY, q == 0 ? 1.0f : 2.0f);
                e.params().set(P_A_POS, 2.0f / 3.0f);
                e.params().set(P_A_W1_MODE, (float)w.w);
                e.params().set(P_A_W1_AMT, w.amt);
                const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
                Stereo s = renderHeld(e, note, 16384);
                const int reg = note <= 72 ? 0 : 1;
                worst[q][reg] = std::max(worst[q][reg], aliasLevelDb(s.l, sr, f0));
            }
        std::printf("warp %-8s            %8.1f    %8.1f    %8.1f    %8.1f\n", w.name, worst[0][0], worst[0][1], worst[1][0], worst[1][1]);
        // Smooth warps must stay clean everywhere; kinked ones at High quality in the bass/mid register.
        const bool smooth = w.w == Warp::Off || w.w == Warp::Sync;
        if (smooth) CHECK(worst[0][0] < -55.0 && worst[0][1] < -40.0, "warp %s aliases (%.1f / %.1f dB)", w.name, worst[0][0], worst[0][1]);
        else CHECK(worst[1][0] < -40.0, "warp %s at High quality aliases at %.1f dB in the bass/mid register", w.name, worst[1][0]);
        if (!smooth) CHECK(worst[1][0] < worst[0][0] - 3.0 && worst[1][1] < worst[0][1] - 3.0, "warp %s: High quality should beat Normal", w.name);
    }

    // 2. Every warp mode in both slots stays finite and makes sound.
    for (int m = 0; m < (int)Warp::Count; ++m)
    {
        plainPatch(e);
        e.params().set(P_B_ON, 1.0f);
        e.params().set(P_SUB_ON, 1.0f);
        e.params().set(P_NOISE_ON, 1.0f);
        e.params().set(P_A_W1_MODE, (float)m);
        e.params().set(P_A_W1_AMT, 0.7f);
        e.params().set(P_B_W2_MODE, (float)m);
        e.params().set(P_B_W2_AMT, 0.5f);
        e.params().set(P_A_UNISON, 7.0f);
        Stereo s = renderHeld(e, 40, 8192);
        CHECK(allFinite(s.l) && allFinite(s.r), "warp %d produced NaN/Inf", m);
        CHECK(rms(s.l) > 1e-3, "warp %d is silent", m);
    }

    // 3. Unison: loudness roughly preserved, stereo spread widens the image.
    {
        plainPatch(e);
        e.params().set(P_A_UNISON, 1.0f);
        Stereo one = renderHeld(e, 45, 32768);
        plainPatch(e);
        e.params().set(P_A_UNISON, 7.0f);
        e.params().set(P_A_DETUNE, 0.3f);
        e.params().set(P_A_SPREAD, 1.0f);
        e.params().set(P_A_RAND, 1.0f);
        Stereo seven = renderHeld(e, 45, 32768);
        const double dB = 20.0 * std::log10(rms(seven.l) / rms(one.l));
        std::printf("unison 7 vs 1: %.1f dB\n", dB);
        CHECK(std::fabs(dB) < 4.0, "unison level jump %.1f dB", dB);
        double side = 0, mid = 0;
        for (size_t i = 0; i < seven.l.size(); ++i) { side += std::fabs(seven.l[i] - seven.r[i]); mid += std::fabs(seven.l[i] + seven.r[i]); }
        std::printf("unison side/mid: %.2f\n", side / mid);
        CHECK(side / mid > 0.2, "unison spread gives no stereo width (%.2f)", side / mid);
        double side1 = 0;
        for (size_t i = 0; i < one.l.size(); ++i) side1 += std::fabs(one.l[i] - one.r[i]);
        CHECK(side1 < 1e-3, "single centred voice should be mono");
    }

    // 4. Sub direct-out bypasses the filter: with the filter closed, the sub is still there.
    {
        plainPatch(e);
        e.params().set(P_A_ON, 0.0f);
        e.params().set(P_SUB_ON, 1.0f);
        e.params().set(P_F1_ON, 1.0f);
        e.params().set(P_F1_TYPE, (float)FilterType::HP24);
        e.params().set(P_F1_CUTOFF, 5000.0f);
        e.params().set(P_SUB_DIRECT, 1.0f);
        const double direct = rms(renderHeld(e, 48, 8192).l);
        e.params().set(P_SUB_DIRECT, 0.0f);
        const double filtered = rms(renderHeld(e, 48, 8192).l);
        std::printf("sub direct %.4f vs through HP filter %.4f\n", direct, filtered);
        CHECK(direct > 10.0 * filtered, "sub direct-out does not bypass the filter");
    }

    // 5. Every noise type makes finite sound.
    for (int t = 0; t < NoiseSource::kNumTypes; ++t)
    {
        plainPatch(e);
        e.params().set(P_A_ON, 0.0f);
        e.params().set(P_NOISE_ON, 1.0f);
        e.params().set(P_NOISE_TYPE, (float)t);
        e.params().set(P_NOISE_LEVEL, 1.0f);
        Stereo s = renderHeld(e, 60, 44100, 16);
        CHECK(allFinite(s.l) && rms(s.l) > 1e-4, "noise type %d silent or not finite (rms %g)", t, rms(s.l));
    }
    TEST_MAIN_END();
}
