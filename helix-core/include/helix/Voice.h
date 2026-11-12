// One synth voice.
#pragma once
#include "helix/Config.h"
#include "helix/Envelope.h"
#include "helix/Filter.h"
#include "helix/Oscillator.h"
#include "helix/Params.h"

namespace helix {

struct EngineContext;   // shared, read-only during a block (tables, param snapshot, global mod values)

class Voice
{
public:
    void prepare(float sampleRate);
    void start(int note, float velocity, uint32_t age, bool legato);
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
    float sr_ = 44100.0f;
    int note_ = 60;
    float vel_ = 1.0f;
    uint32_t age_ = 0;
    bool held_ = false;
    Envelope amp_;
    WtOscillator oscA_;
    Filter f1_;
    float lastPosA_ = 0.0f;
    float buf_[2][kCtrlBlock];
};

} // namespace helix
