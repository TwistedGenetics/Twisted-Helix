// One synth voice: two wavetable oscillators, sub, noise, filters, envelopes.
#pragma once
#include "helix/Config.h"
#include "helix/Envelope.h"
#include "helix/Filter.h"
#include "helix/Oscillator.h"
#include "helix/Params.h"

namespace helix {

struct EngineContext;

class Voice
{
public:
    void prepare(float sampleRate);
    void start(int note, float velocity, uint32_t age, bool legato, uint32_t seed);
    void release();
    void kill();                      // immediate silence (used by reset)
    bool active() const { return amp_.active(); }
    bool held() const { return held_; }
    int note() const { return note_; }
    uint32_t age() const { return age_; }
    float level() const { return amp_.value(); }

    // Render n <= kCtrlBlock samples, adding into l / r.
    void render(const EngineContext& ctx, float* l, float* r, int n);

private:
    void renderOsc(const EngineContext& ctx, int slot, const float* other, int n);

    float sr_ = 44100.0f;
    int note_ = 60;
    float vel_ = 1.0f;
    uint32_t age_ = 0;
    bool held_ = false;
    Rng rng_;
    Envelope amp_;
    WtOscillator osc_[2];
    SubOscillator sub_;
    NoiseSource noise_;
    Filter f1_;
    float lastPos_[2] = {0, 0}, lastLevel_[2] = {0, 0}, lastWarp_[2][2] = {{0, 0}, {0, 0}};
    bool fresh_ = true;   // first block after a start: no ramps from stale values

    // Scratch buffers for one control block
    float oscL_[2][kCtrlBlock], oscR_[2][kCtrlBlock], oscM_[2][kCtrlBlock];
    float prevM_[2][kCtrlBlock];   // last block's mono output, for mutual cross-modulation
    float sub_buf_[kCtrlBlock], noise_buf_[kCtrlBlock];
    float busL_[kCtrlBlock], busR_[kCtrlBlock], directL_[kCtrlBlock], directR_[kCtrlBlock];
};

} // namespace helix
