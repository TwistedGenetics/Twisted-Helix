// Sub guardian: off changes nothing; on with no effects the sound is (nearly) unchanged; the sub band is mono and
// skips the effects (a heavy distortion insert leaves a 50 Hz sine clean while a 1 kHz tone still distorts).
#include "TestUtil.h"
#include "helix/Engine.h"
#include "helix/FxLibrary.h"
#include <cmath>
#include <vector>

using namespace helix;

static std::vector<float> render(void (*setup)(ParamStore&), int note, std::vector<float>* right = nullptr)
{
    Engine e;
    e.prepare(44100.0, 256);
    setup(e.params());
    std::vector<float> out, l(256), r(256);
    Transport tr;
    MidiMsg on = MidiMsg::make(0, 0x90, (uint8_t)note, 110);
    for (int b = 0; b < 120; ++b)
    {
        e.process(l.data(), r.data(), 256, b == 0 ? &on : nullptr, b == 0 ? 1 : 0, tr);
        out.insert(out.end(), l.begin(), l.end());
        if (right) right->insert(right->end(), r.begin(), r.end());
    }
    return out;
}

// level of harmonics 2..8 relative to the fundamental (single-bin DFT)
static double thd(const std::vector<float>& x, double f0)
{
    auto mag = [&](double f) {
        double re = 0, im = 0;
        const size_t a = x.size() / 2;
        for (size_t i = a; i < x.size(); ++i) { re += x[i] * std::cos(6.283185307 * f * i / 44100.0); im += x[i] * std::sin(6.283185307 * f * i / 44100.0); }
        return std::sqrt(re * re + im * im);
    };
    const double fund = mag(f0);
    double h = 0;
    for (int k = 2; k <= 8; ++k) h += mag(f0 * k) * mag(f0 * k);
    return std::sqrt(h) / std::max(fund, 1e-9);
}

static void sine(ParamStore& p)
{
    p.set(P_A_TABLE, 0.0f); p.set(P_A_POS, 0.0f);   // Basic Shapes frame 0 = sine
    p.set(P_A_UNISON, 1.0f); p.set(P_F1_ON, 0.0f); p.set(P_F2_ON, 0.0f); p.set(P_SUB_ON, 0.0f); p.set(P_NOISE_ON, 0.0f);
}
static void sineDist(ParamStore& p) { sine(p); p.set(P_I1_TYPE, 29.0f); p.set(P_I1_MIX, 1.0f); p.set(P_I1_B, 1.0f); }   // Drive
static void sineDistSg(ParamStore& p) { sineDist(p); p.set(P_SG_ON, 1.0f); }
static void sineSg(ParamStore& p) { sine(p); p.set(P_SG_ON, 1.0f); }

int main()
{
    CHECK(std::string(fxlib::typeName(29)) == "Drive", "type 29 is %s", fxlib::typeName(29));
    // 1. A 49 Hz sine (G1, note 31) through a hard Drive insert: distorted without the guard, clean with it.
    const double f0 = 440.0 * std::pow(2.0, (31 - 69) / 12.0);
    const double dry = thd(render(sine, 31), f0);
    const double dirty = thd(render(sineDist, 31), f0), clean = thd(render(sineDistSg, 31), f0);
    std::printf("THD dry %.3f, drive %.3f, drive + guard %.3f\n", dry, dirty, clean);
    CHECK(dirty > 0.05, "the drive did not distort the sub (THD %.3f)", dirty);
    CHECK(clean < dirty * 0.2, "sub guard did not keep the sub clean (THD %.3f vs %.3f)", clean, dirty);
    // 2. The guard with no effects: same level as without (complementary split).
    double a = 0, b = 0;
    for (float v : render(sine, 31)) a += v * v;
    std::vector<float> rr;
    const auto lg = render(sineSg, 31, &rr);
    for (float v : lg) b += v * v;
    CHECK(std::fabs(10.0 * std::log10(b / a)) < 0.5, "level changed by %.2f dB", 10.0 * std::log10(b / a));
    // 3. A note well above the split still goes through the effects (drive distorts a 523 Hz C5).
    const double f1 = 440.0 * std::pow(2.0, (72 - 69) / 12.0);
    CHECK(thd(render(sineDistSg, 72), f1) > 0.05, "the top band skipped the effects");
    TEST_MAIN_END();
}
