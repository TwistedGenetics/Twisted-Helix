// HELIX on a hardware host: the engine, the table library, presets, the MPC-only controls and the worker that
// does all file work. Platform-independent (talks to the device through IPlatform).
#pragma once
#include "IPlatform.h"
#include "MpcParams.h"
#include "helix/Engine.h"
#include "helix/Preset.h"
#include "helix/SpscQueue.h"
#include "helix/TableLibrary.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace helix {

class HelixMpcApp
{
public:
    explicit HelixMpcApp(IPlatform& platform, bool startWorker = true);
    ~HelixMpcApp();

    // ---- audio thread
    void prepare(double sampleRate, int maxBlock);
    void process(float* l, float* r, int frames, const MidiMsg* midi, int numMidi, const Transport& tr);

    // ---- parameters (any thread, trivial)
    void setParam(int index, float plain);
    float getParam(int index) const;
    // Value text for parameters whose text the app owns (tables, presets, CPU). False = default text.
    bool formatValue(int index, float plain, std::string& out) const;

    // ---- state (non-real-time): the preset JSON
    std::string saveState() const;
    void loadState(const std::string& json);

    // ---- presets (non-real-time)
    int numPresets() const;
    std::string presetName(int index) const;
    void loadPresetIndex(int index, int auditionNote = -1);
    // The host selected program n (factory preset n): apply the parts plain values cannot carry.
    void onProgramSelected(int index);

    // Run one round of the worker's job (the thread calls this ~30 times a second; tests call it directly).
    void tick();
    void waitIdle() { library_.waitIdle(); }

    Engine& engine() { return engine_; }
    TableLibrary& library() { return library_; }

private:
    void workerLoop();
    void scanContent();
    void scanContentImpl();
    void applyPreset(const Preset& p);

    IPlatform& platform_;
    Engine engine_;
    TableLibrary library_{engine_};
    std::atomic<float> extra_[kMpcNumParams - kMpcReserveEnd];

    mutable std::mutex presetsMutex_;
    std::vector<Preset> presets_;                 // factory first, then Content/Presets
    std::vector<std::string> samples_;            // Content/Samples/*.wav (absolute paths)
    std::string currentName_ = "Init";

    // audio -> worker: pad-browse requests; worker -> audio: the note to audition once the preset is in
    std::atomic<int> padRequest_{-1};
    SpscQueue<MidiMsg, 64> injected_;
    int lastTable_[2] = {-1, -1};
    int lastProgram_ = -1;
    float lastCpuShown_ = -1.0f;
    int cpuCounter_ = 0;

    std::atomic<bool> quit_{false};
    std::thread worker_;
};

} // namespace helix
