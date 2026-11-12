#include "helix/Voice.h"
#include "helix/Engine.h"
#include "helix/Simd.h"

namespace helix {

void Voice::prepare(float sampleRate)
{
    sr_ = sampleRate;
    amp_.setSampleRate(sampleRate);
    oscA_.setSampleRate(sampleRate);
    f1_.prepare(sampleRate);
}

void Voice::start(int note, float velocity, uint32_t age, bool legato)
{
    note_ = note;
    vel_ = velocity;
    age_ = age;
    held_ = true;
    if (!legato)
    {
        oscA_.start(0.0f);
        f1_.reset();
    }
    amp_.gate(true, legato);
}

void Voice::release() { held_ = false; amp_.gate(false); }
void Voice::kill() { held_ = false; amp_.hardReset(); f1_.reset(); }

void Voice::render(const EngineContext& ctx, float* l, float* r, int n)
{
    const float* p = ctx.p;
    amp_.set(p[P_E1_A], p[P_E1_D], p[P_E1_S], p[P_E1_R], p[P_E1_AC], p[P_E1_DC], p[P_E1_RC]);

    float* bl = buf_[0];
    float* br = buf_[1];
    bufClear(bl, n);
    bufClear(br, n);

    if (p[P_A_ON] >= 0.5f)
    {
        OscBlock ob;
        const float semis = (float)note_ + p[P_A_OCT] * 12.0f + p[P_A_SEMI] + p[P_A_FINE] * 0.01f + ctx.pitchBendSemis;
        ob.freqHz = noteToHz(semis);
        ob.pos0 = lastPosA_;
        ob.pos1 = p[P_A_POS];
        ob.level = p[P_A_LEVEL];
        ob.pan = p[P_A_PAN];
        oscA_.render(ctx.table[kSlotA], ob, ctx.mipCrossfade, bl, br, n);
        lastPosA_ = ob.pos1;
    }

    if (p[P_F1_ON] >= 0.5f)
    {
        f1_.setType((FilterType)(int)p[P_F1_TYPE]);
        const float kt = p[P_F1_KEYTRK] * ((float)note_ - 60.0f) / 12.0f;
        f1_.update(p[P_F1_CUTOFF] * fastExp2(kt), p[P_F1_RES], p[P_F1_DRIVE], p[P_F1_FAT]);
        for (int i = 0; i < n; ++i) f1_.process(bl[i], br[i]);
    }

    const float velGain = 1.0f - p[P_VEL_SENS] * (1.0f - vel_);
    for (int i = 0; i < n; ++i)
    {
        const float a = amp_.process() * velGain;
        l[i] += bl[i] * a;
        r[i] += br[i] * a;
    }
}

} // namespace helix
