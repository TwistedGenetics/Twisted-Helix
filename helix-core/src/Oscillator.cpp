#include "helix/Oscillator.h"
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

namespace helix {

// --------------------------------------------------------------------------------------------- warps

float WtOscillator::warpSpeed(Warp w, float a)
{
    switch (w)
    {
        case Warp::BendPlus: case Warp::BendMinus: return 1.0f + 4.0f * a;
        case Warp::Asym: return 1.0f + 5.0f * a;
        case Warp::Pwm: return 0.5f / (0.5f - 0.48f * a);
        case Warp::Mirror: return 1.0f + a;
        case Warp::Sync: return 1.0f + 15.0f * a;
        case Warp::Formant: return 1.0f + 7.0f * a;
        case Warp::FmOther: case Warp::FmSub: case Warp::FmNoise: return 1.0f + 6.0f * a * a;
        default: return 1.0f;
    }
}

float WtOscillator::maxWarpAmount(Warp w, float otherSpeed, float inc)
{
    // Sync and formant windows spread energy around the squeezed cycle, so they get extra headroom.
    const float ceiling = w == Warp::Formant ? 0.15f : (w == Warp::Sync ? 0.3f : 0.45f);
    const float limit = ceiling / std::max(inc * otherSpeed, 1e-9f);
    if (warpSpeed(w, 1.0f) <= limit) return 1.0f;
    if (warpSpeed(w, 0.0f) > limit) return 0.0f;
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 10; ++i) { const float m = 0.5f * (lo + hi); (warpSpeed(w, m) <= limit ? lo : hi) = m; }
    return lo;
}

float WtOscillator::warpPhase(Warp w, float p, float a, float m)
{
    switch (w)
    {
        case Warp::BendPlus:
        {
            const float k = 4.0f * a, x = 2.0f * p - 1.0f;
            const float y = x * (1.0f + k) / (1.0f + k * std::fabs(x));
            return 0.5f * (y + 1.0f);
        }
        case Warp::BendMinus:
        {
            const float k = 4.0f * a, x = 2.0f * p - 1.0f;
            const float y = x / (1.0f + k - k * std::fabs(x));
            return 0.5f * (y + 1.0f);
        }
        case Warp::Asym:
        {
            // p^e with e = 1 + 5a, via exp2(e * log2 p); p in (0,1]
            if (p <= 0.0f) return 0.0f;
            const float e = 1.0f + 5.0f * a;
            return fastExp2(e * std::log2(p));
        }
        case Warp::Pwm:
        {
            const float wdt = 0.5f - 0.48f * a;
            return p < wdt ? 0.5f * p / wdt : 0.5f + 0.5f * (p - wdt) / (1.0f - wdt);
        }
        case Warp::Mirror:
        {
            const float tri = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
            return p + (tri - p) * a;
        }
        case Warp::Sync: { const float q = p * (1.0f + 15.0f * a); return q - (float)(int)q; }
        case Warp::Formant: { const float q = p * (1.0f + 7.0f * a); return q < 1.0f ? q : 0.0f; }
        case Warp::FmOther: case Warp::FmSub: case Warp::FmNoise:
        {
            const float q = p + 3.0f * a * a * m;
            return q - std::floor(q);
        }
        case Warp::Quantize:
        {
            const float steps = fastExp2(11.0f - 9.0f * a);
            return std::floor(p * steps) / steps;
        }
        default: return p;
    }
}

// Output-side part of a warp. p: the lane's unwarped phase, s: the sample read.
static inline float warpOutput(Warp w, float s, float p, float a, float other)
{
    switch (w)
    {
        case Warp::Sync:
        {
            const float win = 0.5f - 0.5f * std::cos(kTwoPi * p);
            return s * (1.0f + (win - 1.0f) * std::min(1.0f, 4.0f * a));
        }
        case Warp::Formant:
        {
            const float q = p * (1.0f + 7.0f * a);
            if (q >= 1.0f) return s * (1.0f - std::min(1.0f, 8.0f * a));   // silence after the squeezed cycle
            const float win = 0.5f - 0.5f * std::cos(kTwoPi * q);
            return s * (1.0f + (win - 1.0f) * std::min(1.0f, 4.0f * a));
        }
        case Warp::Am: return s * (1.0f - a + a * (0.5f + 0.5f * other));
        case Warp::Rm: return s * (1.0f + (other - 1.0f) * a);
        case Warp::BitRed:
        {
            const float levels = fastExp2(15.0f - 14.0f * a);
            return std::round(s * levels) / levels;
        }
        default: return s;
    }
}

static inline bool isOutputWarp(Warp w) { return w == Warp::Sync || w == Warp::Formant || w == Warp::Am || w == Warp::Rm || w == Warp::BitRed; }
static inline bool isPhaseWarp(Warp w) { return w != Warp::Off && w != Warp::Am && w != Warp::Rm && w != Warp::BitRed; }

// --------------------------------------------------------------------------------------------- unison

void WtOscillator::configureLanes(const OscBlock& b)
{
    const int n = clamp(b.unison, 1, kMaxUnison);
    lanes_ = n;
    float w[kMaxUnison], sumSq = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float o = n > 1 ? 2.0f * (float)i / (float)(n - 1) - 1.0f : 0.0f;   // -1 .. 1
        const float cents = o * 50.0f * b.detune * std::sqrt(b.detune);
        ratio_[i] = fastExp2(cents * (1.0f / 1200.0f));
        const bool centre = n == 1 || std::fabs(o) < 1.0f / (float)n;          // middle lane(s)
        w[i] = centre ? 1.0f : b.blend;
        sumSq += w[i] * w[i];
        const float pan = clamp(b.pan + o * b.spread, -1.0f, 1.0f);
        const float ang = (pan + 1.0f) * 0.25f * kPi;
        gainL_[i] = std::cos(ang) * 1.41421356f;
        gainR_[i] = std::sin(ang) * 1.41421356f;
    }
    const float norm = sumSq > 0.0f ? 1.0f / std::sqrt(sumSq) : 0.0f;
    for (int i = 0; i < n; ++i)
    {
        gainM_[i] = w[i] * norm;
        gainL_[i] *= gainM_[i];
        gainR_[i] *= gainM_[i];
    }
}

void WtOscillator::render(const Wavetable* wt, const OscBlock& b, const OscModInputs& mod, float* l, float* r,
                          float* mono, int n)
{
    if (!wt || n <= 0) { for (int i = 0; i < n; ++i) mono[i] = 0.0f; return; }
    configureLanes(b);

    const int os = b.oversample ? 2 : 1;
    const Warp w0 = b.warp[0], w1 = b.warp[1];
    const float baseInc = b.freqHz * invSr_;
    const float maxRatio = ratio_[lanes_ - 1] > ratio_[0] ? ratio_[lanes_ - 1] : ratio_[0];
    // Effective (oversampled) increment of the fastest lane decides the warp limits and the mip level.
    const float fastInc = baseInc * maxRatio;
    float a0max = std::max(b.warpAmt0[0], b.warpAmt1[0]), a1max = std::max(b.warpAmt0[1], b.warpAmt1[1]);
    const float lim0 = maxWarpAmount(w0, warpSpeed(w1, a1max), fastInc);
    a0max = std::min(a0max, lim0);
    const float lim1 = maxWarpAmount(w1, warpSpeed(w0, a0max), fastInc);
    a1max = std::min(a1max, lim1);
    // Sync / formant windows widen every harmonic by about +-2x the squeeze ratio: read one octave duller.
    const bool windowed = w0 == Warp::Sync || w0 == Warp::Formant || w1 == Warp::Sync || w1 == Warp::Formant;
    const float speed = warpSpeed(w0, a0max) * warpSpeed(w1, a1max) * (windowed ? 2.0f : 1.0f);
    const bool phase0 = isPhaseWarp(w0), phase1 = isPhaseWarp(w1);
    const bool out0 = isOutputWarp(w0), out1 = isOutputWarp(w1);
    auto modFor = [&](Warp w) -> const float* {
        switch (w)
        {
            case Warp::FmOther: case Warp::Am: case Warp::Rm: return mod.other;
            case Warp::FmSub: return mod.sub;
            case Warp::FmNoise: return mod.noise;
            default: return nullptr;
        }
    };
    const float* m0 = modFor(w0);
    const float* m1 = modFor(w1);

    MipSel mips[kMaxUnison];
    float inc[kMaxUnison];
    for (int k = 0; k < lanes_; ++k)
    {
        inc[k] = baseInc * ratio_[k] / (float)os;
        // Band-limit to the OUTPUT Nyquist even when oversampling: the 2x path is there for the partials the
        // warps create, not to widen the table's own band (that would land in the decimator's transition band).
        mips[k] = selectMip(baseInc * ratio_[k] * speed, b.crossfade);
    }

    const float invN = 1.0f / (float)n;
    const float dPos = (b.pos1 - b.pos0) * invN, dLev = (b.level1 - b.level0) * invN;
    const float dA0 = (b.warpAmt1[0] - b.warpAmt0[0]) * invN, dA1 = (b.warpAmt1[1] - b.warpAmt0[1]) * invN;
    float pos = b.pos0, lev = b.level0, a0 = b.warpAmt0[0], a1 = b.warpAmt0[1];

    for (int i = 0; i < n; ++i)
    {
        int f0, f1; float t;
        framePos(*wt, pos, f0, f1, t);
        const float ms0 = m0 ? m0[i] : 0.0f, ms1 = m1 ? m1[i] : 0.0f;
        const float aa0 = std::min(a0, lim0), aa1 = std::min(a1, lim1);
        float subL[2] = {0, 0}, subR[2] = {0, 0}, sm = 0.0f;
        for (int o = 0; o < os; ++o)
        {
            float sl = 0.0f, sr = 0.0f;
            for (int k = 0; k < lanes_; ++k)
            {
                const float p = phase_[k];
                float q = p;
                if (phase0) q = warpPhase(w0, q, aa0, ms0);
                if (phase1) q = warpPhase(w1, q, aa1, ms1);
                float s = readTable(*wt, mips[k], f0, f1, t, q);
                if (out0) s = warpOutput(w0, s, p, aa0, ms0);
                if (out1) s = warpOutput(w1, s, p, aa1, ms1);
                sl += s * gainL_[k];
                sr += s * gainR_[k];
                sm += s * gainM_[k];
                const float np = p + inc[k];
                phase_[k] = np - (float)(int)np;
            }
            subL[o] = sl; subR[o] = sr;
        }
        float outL = subL[0], outR = subR[0];
        if (os == 2) { outL = decL_.process(subL[0], subL[1]); outR = decR_.process(subR[0], subR[1]); sm *= 0.5f; }
        l[i] += outL * lev;
        r[i] += outR * lev;
        mono[i] = sm * lev;
        pos += dPos; lev += dLev; a0 += dA0; a1 += dA1;
    }
}

// --------------------------------------------------------------------------------------------- sub

static inline float polyBlep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

void SubOscillator::render(int shape, float freqHz, float level, float* out, int n)
{
    const float dt = clamp(freqHz * invSr_, 0.0f, 0.45f);
    for (int i = 0; i < n; ++i)
    {
        const float p = phase_;
        float s;
        switch (shape)
        {
            case 0: s = std::sin(kTwoPi * p); break;
            case 1:
            {
                // Integrated band-limited square -> triangle (leaky integrator keeps DC away)
                float sq = p < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep(p, dt);
                sq -= polyBlep(wrap01(p + 0.5f), dt);
                triState_ = triState_ * 0.9995f + 4.0f * dt * sq;
                s = triState_;
                break;
            }
            case 2:
            {
                s = p < 0.5f ? 1.0f : -1.0f;
                s += polyBlep(p, dt);
                s -= polyBlep(wrap01(p + 0.5f), dt);
                s *= 0.7f;
                break;
            }
            default: s = (2.0f * p - 1.0f - polyBlep(p, dt)) * 0.8f; break;
        }
        out[i] = s * level;
        float np = p + dt;
        phase_ = np - (float)(int)np;
    }
}

// --------------------------------------------------------------------------------------------- noise

static std::vector<float>* g_noiseLoops = nullptr;   // kNumTypes * kLoopLen, immutable once built
static std::once_flag g_noiseOnce;

void NoiseSource::buildSharedLoops()
{
    std::call_once(g_noiseOnce, [] {
        auto* buf = new std::vector<float>((size_t)kNumTypes * kLoopLen);
        Rng rng(0x1234567u);
        auto loop = [&](int t) { return buf->data() + (size_t)t * kLoopLen; };
        const float sr = (float)kLoopRate;
        // 0 white
        for (int i = 0; i < kLoopLen; ++i) loop(0)[i] = rng.bi() * 0.5f;
        // 1 pink (Paul Kellet's refined filter)
        {
            float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
            for (int i = 0; i < kLoopLen; ++i)
            {
                const float w = rng.bi();
                b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
                b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
                b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
                loop(1)[i] = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f;
                b6 = w * 0.115926f;
            }
        }
        // 2 brown
        {
            float y = 0;
            for (int i = 0; i < kLoopLen; ++i) { y = 0.998f * y + rng.bi() * 0.04f; loop(2)[i] = y * 1.5f; }
        }
        // 3 vinyl: rumble + soft hiss + crackle pops with a little ring
        {
            float rum = 0, hiss = 0, ring = 0;
            for (int i = 0; i < kLoopLen; ++i)
            {
                rum = 0.9995f * rum + rng.bi() * 0.002f;
                hiss = 0.6f * hiss + 0.4f * rng.bi();
                if (rng.uni() < 6.0f / sr) ring += rng.bi() * 0.9f;
                if (rng.uni() < 40.0f / sr) ring += rng.bi() * 0.2f;
                ring *= 0.82f;
                const float wow = 0.6f + 0.4f * std::sin(kTwoPi * 0.555f * (float)i / sr);   // 33 rpm
                loop(3)[i] = rum * 3.0f + hiss * 0.03f * wow + ring;
            }
        }
        // 4 tape hiss: bright noise with flutter modulation
        {
            float lp = 0, hp = 0;
            for (int i = 0; i < kLoopLen; ++i)
            {
                const float w = rng.bi();
                lp += 0.6f * (w - lp);
                const float hiPass = lp - hp; hp += 0.02f * hiPass;
                const float flutter = 1.0f + 0.15f * std::sin(kTwoPi * 7.0f * (float)i / sr);
                loop(4)[i] = hiPass * 0.35f * flutter;
            }
        }
        // 5 crackle: sparse clicks only
        {
            float ring = 0;
            for (int i = 0; i < kLoopLen; ++i)
            {
                if (rng.uni() < 25.0f / sr) ring += rng.bi();
                ring *= 0.7f;
                loop(5)[i] = ring * 0.8f;
            }
        }
        // Click-free loop seam: crossfade the last 2048 samples into the first.
        for (int t = 0; t < kNumTypes; ++t)
        {
            float* x = loop(t);
            const int fade = 2048;
            for (int i = 0; i < fade; ++i)
            {
                const float g = (float)i / fade;
                x[kLoopLen - fade + i] = x[kLoopLen - fade + i] * (1.0f - g) + x[i] * g;
            }
        }
        g_noiseLoops = buf;
    });
}

void NoiseSource::render(int type, float pitchRatio, bool loop, float level, float* out, int n)
{
    if (!g_noiseLoops || done_) { for (int i = 0; i < n; ++i) out[i] = 0.0f; return; }
    type = clamp(type, 0, kNumTypes - 1);
    const float* x = g_noiseLoops->data() + (size_t)type * kLoopLen;
    const double step = (double)(rateScale_ * clamp(pitchRatio, 0.0625f, 16.0f));
    for (int i = 0; i < n; ++i)
    {
        if (pos_ >= kLoopLen)
        {
            if (loop) pos_ -= kLoopLen;
            else { done_ = true; for (; i < n; ++i) out[i] = 0.0f; return; }
        }
        const int i0 = (int)pos_;
        const float fr = (float)(pos_ - i0);
        const int i1 = i0 + 1 < kLoopLen ? i0 + 1 : 0;
        out[i] = (x[i0] + (x[i1] - x[i0]) * fr) * level;
        pos_ += step;
    }
}

} // namespace helix
