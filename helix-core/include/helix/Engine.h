// The HELIX synth engine: voices, tables, parameters. Host-agnostic.
//
// Thread rules:
//  * process() runs on the audio thread: no allocation, no locks, no I/O.
//  * params() may be written from any thread.
//  * setTable() is called from a non-audio thread; the table is handed over through a lock-free queue and
//    the audio thread swaps the pointer at the next block. Old tables are handed back the same way and are
//    released by collectGarbage() (also non-audio).
#pragma once
#include "helix/Config.h"
#include "helix/Midi.h"
#include "helix/Params.h"
#include "helix/SpscQueue.h"
#include "helix/Voice.h"
#include "helix/Wavetable.h"
#include <atomic>
#include <memory>

namespace helix {

enum TableSlot : int { kSlotA = 0, kSlotB = 1, kNumTableSlots = 2 };

struct EngineContext
{
    const ParamStore* params = nullptr;
    float p[kNumParams];                         // snapshot of plain values for this block
    const Wavetable* table[kNumTableSlots] = {nullptr, nullptr};
    float sampleRate = 44100.0f;
    bool mipCrossfade = true;
    int unisonCap = kMaxUnison;
    bool oversample = false;          // 2x oscillators (Quality: High)
    float pitchBendSemis = 0.0f;
};

class Engine
{
public:
    Engine();
    ~Engine();

    // Not real-time safe. Call before processing and whenever the sample rate changes.
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Real-time. Renders n frames into l / r (overwrites). Events must be sorted by offset.
    void process(float* l, float* r, int n, const MidiMsg* events, int numEvents, const Transport& transport);

    ParamStore& params() { return params_; }
    const ParamStore& params() const { return params_; }

    // Non-real-time: hand a table to the engine. Returns false if the hand-over queue is full (try again later).
    bool setTable(int slot, WavetablePtr table);
    // The table the UI should show for a slot (the latest one handed over).
    WavetablePtr uiTable(int slot) const;
    // Non-real-time: release tables the audio thread no longer uses.
    void collectGarbage();

    // Monitoring (any thread)
    // Live (modulated) value of a parameter, normalised 0..1, from the most recently played voice. For mod rings.
    float liveNormalised(int index) const { return params_.getNormalised(index); }
    int activeVoices() const { return activeVoices_.load(std::memory_order_relaxed); }

    double sampleRate() const { return sampleRate_; }

private:
    void handleMidi(const MidiMsg& m);
    void noteOn(int note, int vel);
    void noteOff(int note);
    void allNotesOff(bool hard);
    void renderBlock(float* l, float* r, int n);
    void snapshotParams();
    void swapTables();

    ParamStore params_;
    EngineContext ctx_;
    Voice voices_[kMaxVoices];
    uint32_t ageCounter_ = 0;
    double sampleRate_ = 44100.0;
    int maxBlock_ = 512;
    float pitchBend_ = 0.0f;

    struct TableHandle { WavetablePtr ptr; int slot = 0; };
    SpscQueue<TableHandle, 16> pending_;       // UI -> audio
    SpscQueue<TableHandle, 64> retired_;       // audio -> UI
    WavetablePtr active_[kNumTableSlots];      // owned by the audio thread
    WavetablePtr uiTables_[kNumTableSlots];    // owned by the non-audio side
    std::atomic<int> activeVoices_{0};
};

} // namespace helix
