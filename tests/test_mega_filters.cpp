// The Mega Synth filter models: every model x type x slope stays finite and bounded under hard
// drive and full resonance, low pass passes lows and cuts highs, high pass the reverse.
#include "TestUtil.h"
#include "helix/MegaFilter.h"
#include "helix/Engine.h"
#include <cmath>
#include <vector>

using namespace helix;
using namespace helix::mega;

static const double kSr = 44100.0;

static double gainDb(int model, int type, int slope, float cutoff, float hz, float res = 0.0f)
{
    MegaFilter f;
    f.configure(model, type, slope);
    f.update(cutoff, res, 0.0f, 0.0f, kSr);
    const int n = 32768, skip = 8192, blk = 64;
    double in = 0, out = 0;
    float l[blk], r[blk];
    for (int i = 0; i < n; i += blk)
    {
        float x[blk];
        for (int k = 0; k < blk; ++k) { x[k] = 0.25f * (float)std::sin(2.0 * mega::kPi * hz * (double)(i + k) / kSr); l[k] = r[k] = x[k]; }
        f.processBlock(l, r, blk);
        if (i >= skip) for (int k = 0; k < blk; ++k) { in += (double)x[k] * x[k]; out += (double)l[k] * l[k]; }
    }
    return 10.0 * std::log10(std::max(out, 1e-30) / in);
}

int main()
{
    // Low pass at 1 kHz: 100 Hz near unity, 10 kHz well down, for every model at its own slope.
    for (int m = 0; m < kNumModels; ++m)
    {
        // (several models put a high pass well under the cutoff, as the originals do, so "pass"
        // is measured just under the cutoff)
        const double pass = gainDb(m, FT_LP, -1, 1000.0f, 700.0f);
        const double stop = gainDb(m, FT_LP, -1, 1000.0f, 10000.0f);
        CHECK(pass > -12.0 && pass < 12.0, "model %d LP pass band %.1f dB", m, pass);
        CHECK(stop < pass - 15.0, "model %d LP stop band %.1f dB", m, stop);
        const double hpPass = gainDb(m, FT_HP, 1, 1000.0f, 10000.0f);
        const double hpStop = gainDb(m, FT_HP, 1, 1000.0f, 100.0f);
        CHECK(hpStop < hpPass - 15.0, "model %d HP %.1f / %.1f dB", m, hpPass, hpStop);
    }

    // Torture: every combination, full resonance, full drive, cutoff sweeping fast. Must stay finite.
    for (int m = 0; m < kNumModels; ++m)
        for (int t = 0; t < FT_TYPE_COUNT; ++t)
            for (int s = -1; s <= 3; ++s)
            {
                MegaFilter f;
                f.configure(m, t, s);
                float peak = 0.0f;
                bool finite = true;
                for (int b = 0; b < 400; ++b)
                {
                    const float cut = 30.0f * std::pow(600.0f, 0.5f + 0.5f * (float)std::sin(b * 0.21));
                    f.update(cut, 1.0f, 1.0f, (b & 1) ? 1.0f : 0.0f, kSr);
                    float l[64], r[64];
                    for (int k = 0; k < 64; ++k) l[k] = r[k] = ((b * 64 + k) % 100 < 50) ? 0.9f : -0.9f;
                    f.processBlock(l, r, 64);
                    for (int k = 0; k < 64; ++k)
                    {
                        if (!std::isfinite(l[k]) || !std::isfinite(r[k])) finite = false;
                        peak = std::max(peak, std::abs(l[k]));
                    }
                }
                CHECK(finite, "model %d type %d slope %d went non-finite", m, t, s);
                CHECK(peak < 8.0f, "model %d type %d slope %d peak %.2f", m, t, s, (double)peak);
            }

    // Through the engine: Model 0 = the old HELIX filter (presets unchanged); Model > 0 renders sound.
    for (int model : {0, 4, 13})
    {
        Engine e;
        e.prepare(44100.0, 256);
        e.params().set(P_F1_MODEL, (float)model);
        e.params().set(P_F1_CUTOFF, 1200.0f);
        e.params().set(P_F1_RES, 0.7f);
        std::vector<float> l(256), r(256);
        double energy = 0;
        Transport tr;
        MidiMsg on = MidiMsg::make(0, 0x90, 48, 127);
        for (int b = 0; b < 60; ++b)
        {
            e.process(l.data(), r.data(), 256, b == 0 ? &on : nullptr, b == 0 ? 1 : 0, tr);
            for (int i = 0; i < 256; ++i) { CHECK(std::isfinite(l[i]), "engine non-finite"); energy += l[i] * l[i]; }
        }
        CHECK(energy > 1e-3, "model %d silent through the engine", model);
    }
    TEST_MAIN_END();
}
