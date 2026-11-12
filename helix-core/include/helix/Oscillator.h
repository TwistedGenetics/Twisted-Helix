// Wavetable oscillator (one per voice per oscillator slot).
#pragma once
#include "helix/Config.h"
#include "helix/TableRead.h"

namespace helix {

struct OscBlock   // control-rate values for one block
{
    float freqHz = 440.0f;
    float pos0 = 0.0f, pos1 = 0.0f;   // position at block start / end (ramped)
    float level = 1.0f, pan = 0.0f;
};

class WtOscillator
{
public:
    void setSampleRate(float sr) { invSr_ = 1.0f / sr; }
    void start(float phase) { phase_ = wrap01(phase); }
    // Render n samples, adding into l/r. Returns nothing; table may be null (silence).
    void render(const Wavetable* wt, const OscBlock& b, bool crossfade, float* l, float* r, int n)
    {
        if (!wt) return;
        const float inc = b.freqHz * invSr_;
        const MipSel m = selectMip(inc, crossfade);
        const float gl = b.level * std::sqrt(0.5f * (1.0f - b.pan));
        const float gr = b.level * std::sqrt(0.5f * (1.0f + b.pan));
        const float dpos = (b.pos1 - b.pos0) / (float)n;
        float pos = b.pos0;
        for (int i = 0; i < n; ++i)
        {
            int f0, f1; float t;
            framePos(*wt, pos, f0, f1, t);
            const float s = readTable(*wt, m, f0, f1, t, phase_);
            l[i] += s * gl;
            r[i] += s * gr;
            phase_ += inc;
            phase_ -= (float)(int)phase_;
            pos += dpos;
        }
    }
private:
    float invSr_ = 1.0f / 44100.0f;
    float phase_ = 0.0f;
};

} // namespace helix
