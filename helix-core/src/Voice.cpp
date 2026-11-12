#include "helix/Voice.h"
#include "helix/Engine.h"
#include "helix/Simd.h"
#include <cstring>

namespace helix {

void Voice::prepare(float sampleRate)
{
    sr_ = sampleRate;
    amp_.setSampleRate(sampleRate);
    for (auto& o : osc_) o.setSampleRate(sampleRate);
    sub_.setSampleRate(sampleRate);
    noise_.setSampleRate(sampleRate);
    for (auto& f : filt_) f.prepare(sampleRate);
    std::memset(prevM_, 0, sizeof prevM_);
}

void Voice::start(int note, float velocity, uint32_t age, bool legato, uint32_t seed)
{
    note_ = note;
    vel_ = velocity;
    age_ = age;
    held_ = true;
    if (!legato)
    {
        rng_ = Rng(seed * 2654435761u + 12345u);
        fresh_ = true;
        for (auto& f : filt_) f.reset();
        std::memset(prevM_, 0, sizeof prevM_);
    }
    amp_.gate(true, legato);
}

void Voice::release() { held_ = false; amp_.gate(false); }
void Voice::kill() { held_ = false; amp_.hardReset(); for (auto& f : filt_) f.reset(); }

static inline bool warpUsesOther(float mode)
{
    const Warp w = (Warp)(int)mode;
    return w == Warp::FmOther || w == Warp::Am || w == Warp::Rm;
}

void Voice::renderOsc(const EngineContext& ctx, int s, const float* other, int n)
{
    const float* p = ctx.p;
    const int o = s == 0 ? 0 : (P_B_ON - P_A_ON);   // parameter offset of oscillator B
    bufClear(oscL_[s], n);
    bufClear(oscR_[s], n);
    if (p[P_A_ON + o] < 0.5f) { bufClear(oscM_[s], n); return; }

    OscBlock b;
    const float semis = (float)note_ + p[P_A_OCT + o] * 12.0f + p[P_A_SEMI + o] + p[P_A_FINE + o] * 0.01f + ctx.pitchBendSemis;
    b.freqHz = noteToHz(semis);
    b.pos1 = p[P_A_POS + o];
    b.level1 = p[P_A_LEVEL + o];
    b.warp[0] = (Warp)(int)p[P_A_W1_MODE + o];
    b.warp[1] = (Warp)(int)p[P_A_W2_MODE + o];
    b.warpAmt1[0] = p[P_A_W1_AMT + o];
    b.warpAmt1[1] = p[P_A_W2_AMT + o];
    if (fresh_) { lastPos_[s] = b.pos1; lastLevel_[s] = b.level1; lastWarp_[s][0] = b.warpAmt1[0]; lastWarp_[s][1] = b.warpAmt1[1]; }
    b.pos0 = lastPos_[s];
    b.level0 = lastLevel_[s];
    b.warpAmt0[0] = lastWarp_[s][0];
    b.warpAmt0[1] = lastWarp_[s][1];
    b.pan = p[P_A_PAN + o];
    b.unison = std::min((int)p[P_A_UNISON + o], ctx.unisonCap);
    b.detune = p[P_A_DETUNE + o];
    b.blend = p[P_A_BLEND + o];
    b.spread = p[P_A_SPREAD + o];
    b.crossfade = ctx.mipCrossfade;
    b.oversample = ctx.oversample;

    OscModInputs mod;
    mod.other = other;
    mod.sub = sub_buf_;
    mod.noise = noise_buf_;
    osc_[s].render(ctx.table[s], b, mod, oscL_[s], oscR_[s], oscM_[s], n);

    lastPos_[s] = b.pos1; lastLevel_[s] = b.level1;
    lastWarp_[s][0] = b.warpAmt1[0]; lastWarp_[s][1] = b.warpAmt1[1];
}

void Voice::render(const EngineContext& ctx, float* l, float* r, int n)
{
    const float* p = ctx.p;
    amp_.set(p[P_E1_A], p[P_E1_D], p[P_E1_S], p[P_E1_R], p[P_E1_AC], p[P_E1_DC], p[P_E1_RC]);

    if (fresh_)
    {
        // Phases are set when the voice first renders, so start phase / random phase use current params.
        for (int s = 0; s < 2; ++s)
        {
            const int o = s == 0 ? 0 : (P_B_ON - P_A_ON);
            osc_[s].start(p[P_A_PHASE + o], p[P_A_RAND + o], rng_);
        }
        sub_.start(0.0f);
        noise_.start(rng_, p[P_NOISE_LOOP] >= 0.5f);
    }

    // Sub and noise first: they can modulate the oscillators.
    if (p[P_SUB_ON] >= 0.5f)
    {
        const float div = p[P_SUB_OCT] >= 0.5f ? 24.0f : 12.0f;
        sub_.render((int)p[P_SUB_SHAPE], noteToHz((float)note_ - div + ctx.pitchBendSemis), p[P_SUB_LEVEL], sub_buf_, n);
    }
    else bufClear(sub_buf_, n);
    if (p[P_NOISE_ON] >= 0.5f)
    {
        const float ratio = p[P_NOISE_PITCH] >= 0.5f ? fastExp2(((float)note_ - 60.0f + ctx.pitchBendSemis) / 12.0f) : 1.0f;
        noise_.render((int)p[P_NOISE_TYPE], ratio, p[P_NOISE_LOOP] >= 0.5f, p[P_NOISE_LEVEL], noise_buf_, n);
    }
    else bufClear(noise_buf_, n);

    // Oscillator order follows who modulates whom. When both modulate each other, B hears A one block late.
    const bool aNeedsB = warpUsesOther(p[P_A_W1_MODE]) || warpUsesOther(p[P_A_W2_MODE]);
    const bool bNeedsA = warpUsesOther(p[P_B_W1_MODE]) || warpUsesOther(p[P_B_W2_MODE]);
    if (bNeedsA && !aNeedsB)
    {
        renderOsc(ctx, 0, nullptr, n);
        renderOsc(ctx, 1, oscM_[0], n);
    }
    else
    {
        renderOsc(ctx, 1, bNeedsA ? prevM_[0] : nullptr, n);
        renderOsc(ctx, 0, oscM_[1], n);
    }
    std::memcpy(prevM_[0], oscM_[0], sizeof(float) * (size_t)n);
    std::memcpy(prevM_[1], oscM_[1], sizeof(float) * (size_t)n);

    // Sources -> filter buses. Each source has a send to F1 and to F2 (Massive style); the sub can instead go
    // straight to the output (direct-out), past both filters, so a Reese's sub stays clean.
    for (int f = 0; f < 2; ++f) { bufClear(busL_[f], n); bufClear(busR_[f], n); }
    bufClear(directL_, n); bufClear(directR_, n);
    const int sendOff = P_A_TO_F2 - P_A_TO_F1;   // same layout for every source
    for (int s = 0; s < 2; ++s)
    {
        const int o = s == 0 ? 0 : (P_B_ON - P_A_ON);
        for (int f = 0; f < 2; ++f)
        {
            const float send = p[P_A_TO_F1 + o + f * sendOff];
            if (send <= 0.0f) continue;
            bufMixIn(busL_[f], oscL_[s], send, n);
            bufMixIn(busR_[f], oscR_[s], send, n);
        }
    }
    auto panGains = [](float pan, float& gl, float& gr) {
        gl = std::sqrt(0.5f * (1.0f - pan)) * 1.41421356f;
        gr = std::sqrt(0.5f * (1.0f + pan)) * 1.41421356f;
    };
    if (p[P_SUB_ON] >= 0.5f)
    {
        float gl, gr; panGains(p[P_SUB_PAN], gl, gr);
        if (p[P_SUB_DIRECT] >= 0.5f) { bufMixIn(directL_, sub_buf_, gl, n); bufMixIn(directR_, sub_buf_, gr, n); }
        else
            for (int f = 0; f < 2; ++f)
            {
                const float send = f == 0 ? p[P_SUB_TO_F1] : p[P_SUB_TO_F2];
                bufMixIn(busL_[f], sub_buf_, gl * send, n);
                bufMixIn(busR_[f], sub_buf_, gr * send, n);
            }
    }
    if (p[P_NOISE_ON] >= 0.5f)
    {
        float gl, gr; panGains(p[P_NOISE_PAN], gl, gr);
        for (int f = 0; f < 2; ++f)
        {
            const float send = f == 0 ? p[P_NOISE_TO_F1] : p[P_NOISE_TO_F2];
            bufMixIn(busL_[f], noise_buf_, gl * send, n);
            bufMixIn(busR_[f], noise_buf_, gr * send, n);
        }
    }

    // Filters. Blend 0 = parallel (F1 + F2), 1 = serial (F1 feeds F2, only F2 is heard), in between = both.
    const float blend = clamp(p[P_FILTER_BLEND], 0.0f, 1.0f);
    const int fOff = P_F2_ON - P_F1_ON;
    for (int f = 0; f < 2; ++f)
    {
        const int o = f * fOff;
        float* bl = busL_[f];
        float* br = busR_[f];
        if (f == 1 && blend > 0.0f) { bufMixIn(bl, busL_[0], blend, n); bufMixIn(br, busR_[0], blend, n); }
        if (p[P_F1_ON + o] >= 0.5f)
        {
            Filter& flt = filt_[f];
            flt.setType((FilterType)(int)p[P_F1_TYPE + o]);
            const float kt = p[P_F1_KEYTRK + o] * ((float)note_ - 60.0f) / 12.0f;
            flt.update(p[P_F1_CUTOFF + o] * fastExp2(kt), p[P_F1_RES + o], p[P_F1_DRIVE + o], p[P_F1_FAT + o]);
            for (int i = 0; i < n; ++i) flt.process(bl[i], br[i]);
            bufScale(bl, p[P_F1_LEVEL + o], n);
            bufScale(br, p[P_F1_LEVEL + o], n);
        }
    }
    // out = (1 - blend) * F1 + F2 (+ direct)
    bufScale(busL_[0], 1.0f - blend, n);
    bufScale(busR_[0], 1.0f - blend, n);
    bufMixIn(busL_[0], busL_[1], 1.0f, n);
    bufMixIn(busR_[0], busR_[1], 1.0f, n);
    float* outL = busL_[0];
    float* outR = busR_[0];

    const float velGain = 1.0f - p[P_VEL_SENS] * (1.0f - vel_);
    for (int i = 0; i < n; ++i)
    {
        const float a = amp_.process() * velGain;
        l[i] += (outL[i] + directL_[i]) * a;
        r[i] += (outR[i] + directR_[i]) * a;
    }
    fresh_ = false;
}

} // namespace helix
