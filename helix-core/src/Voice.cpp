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
    f1_.prepare(sampleRate);
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
        f1_.reset();
        std::memset(prevM_, 0, sizeof prevM_);
    }
    amp_.gate(true, legato);
}

void Voice::release() { held_ = false; amp_.gate(false); }
void Voice::kill() { held_ = false; amp_.hardReset(); f1_.reset(); }

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

    // Mix the sources into the filter bus (sends) and the direct bus (sub direct-out).
    bufClear(busL_, n); bufClear(busR_, n); bufClear(directL_, n); bufClear(directR_, n);
    for (int s = 0; s < 2; ++s)
    {
        const int o = s == 0 ? 0 : (P_B_ON - P_A_ON);
        const float send = p[P_A_TO_F1 + o];
        bufMixIn(busL_, oscL_[s], send, n);
        bufMixIn(busR_, oscR_[s], send, n);
    }
    {
        const float pan = p[P_SUB_PAN], gl = std::sqrt(0.5f * (1.0f - pan)) * 1.41421356f, gr = std::sqrt(0.5f * (1.0f + pan)) * 1.41421356f;
        if (p[P_SUB_DIRECT] >= 0.5f) { bufMixIn(directL_, sub_buf_, gl, n); bufMixIn(directR_, sub_buf_, gr, n); }
        else { bufMixIn(busL_, sub_buf_, gl * p[P_SUB_TO_F1], n); bufMixIn(busR_, sub_buf_, gr * p[P_SUB_TO_F1], n); }
    }
    {
        const float pan = p[P_NOISE_PAN], gl = std::sqrt(0.5f * (1.0f - pan)) * 1.41421356f, gr = std::sqrt(0.5f * (1.0f + pan)) * 1.41421356f;
        bufMixIn(busL_, noise_buf_, gl * p[P_NOISE_TO_F1], n);
        bufMixIn(busR_, noise_buf_, gr * p[P_NOISE_TO_F1], n);
    }

    if (p[P_F1_ON] >= 0.5f)
    {
        f1_.setType((FilterType)(int)p[P_F1_TYPE]);
        const float kt = p[P_F1_KEYTRK] * ((float)note_ - 60.0f) / 12.0f;
        f1_.update(p[P_F1_CUTOFF] * fastExp2(kt), p[P_F1_RES], p[P_F1_DRIVE], p[P_F1_FAT]);
        for (int i = 0; i < n; ++i) f1_.process(busL_[i], busR_[i]);
        bufScale(busL_, p[P_F1_LEVEL], n);
        bufScale(busR_, p[P_F1_LEVEL], n);
    }

    const float velGain = 1.0f - p[P_VEL_SENS] * (1.0f - vel_);
    for (int i = 0; i < n; ++i)
    {
        const float a = amp_.process() * velGain;
        l[i] += (busL_[i] + directL_[i]) * a;
        r[i] += (busR_[i] + directR_[i]) * a;
    }
    fresh_ = false;
}

} // namespace helix
