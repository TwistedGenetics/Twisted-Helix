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
    std::string currentPresetName() const { return currentName_; }
    void loadPresetIndex(int index, int auditionNote = -1);
    // The host selected program n (factory preset n): apply the parts plain values cannot carry.
    void onProgramSelected(int index);

    // Run one round of the worker's job (the thread calls this ~30 times a second; tests call it directly).
    void tick();
    void waitIdle() { library_.waitIdle(); }

    Engine& engine() { return engine_; }
    TableLibrary& library() { return library_; }
    // The presets saved in <content>/Presets (sorted by path), as the plugin lists them after the factory ones.
    static std::vector<Preset> userPresets(const std::string& contentDir);
    static std::string sharedPresetsDir(const std::string& contentDir);   // VST2 Plugins/Content/Presets/HELIX or ""
    // User slot s: <shared presets>/User Slots/Slot NN.json (or Content/User Slots without a shared folder), and the
    // preset in it (false if empty; a slot left in the old Content/User Slots still loads)
    static std::string userSlotPath(const std::string& contentDir, int s);
    static bool loadUserSlot(const std::string& contentDir, int s, Preset& out);

private:
    void workerLoop();
    void scanContent();
    void scanContentImpl();
    void applyPreset(const Preset& p);
    void startPrint();
    std::vector<std::string> tableRoots() const;
    std::string tablesDir() const;   // where new tables (prints) go
    void saveUserSlot(int s);
    void syncNameEditor();
    void typeKey(int key);
    int slotBase() const;                          // browser index of U001 (call with presetsMutex_ held)
    void nudge(int index);                         // re-send a parameter with a tiny change so its text redraws
    void flipPrintStatus();

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
    struct PerfEdit
    {
        int step = -1; float curve = -1, shape = -1, level = -9, levelCore = -9;
        float levels[16] = {-9, -9, -9, -9, -9, -9, -9, -9, -9, -9, -9, -9, -9, -9, -9, -9};
    };
    PerfEdit perfEdit_[2];
    int filterProxy_[2][2] = {{-1, -1}, {-1, -1}};
    struct LibSlot { int type = -1; float knobs[7] = {}; };   // Mix + A..F as last seen
    LibSlot libSlot_[4];   // last shown list value, last core value
    int cpuCounter_ = 0;
    int lastCharFam_ = -99;
    std::string saveStatus_;
    std::string nameBuf_;                       // the name being edited (guarded by presetsMutex_)
    std::vector<std::string> slotNames_;         // user slot names ("" = empty), for the Save Slot display
    std::atomic<bool> nameReset_{true};
    int lastNameCursor_ = -1, lastNameChar_ = -1;
    std::mutex keyMutex_;
    std::vector<int> keys_;                      // NAME keyboard taps, queued by setParam (never missed)
    bool caps_ = false;
    bool nudged_[kMpcNumParams - kMpcReserveEnd] = {};
public:
    // Program index of user slot U1 (= factory + Presets/ files when the program list was built)
    void setProgramBase(int b) { programBase_ = b; }
private:
    int programBase_ = 1 << 20;
    int lastSceneParam_ = -1, lastEngineScene_ = -1;
    std::atomic<bool> sceneResync_{false};
    float lastMacro_[kNumMacros] = {-9, -9, -9, -9, -9, -9, -9, -9};

    std::atomic<bool> quit_{false};
    std::thread worker_;

    // Print to wavetable (its own thread: a print takes seconds)
    std::thread printThread_;
    std::atomic<bool> printing_{false}, printCancel_{false}, printDone_{false};
    mutable std::mutex printMutex_;
    std::string printStatus_;
    std::atomic<int> printLoad_{-1};   // (slot << 16) | library index of the new print, for the worker
public:
    bool printing() const { return printing_.load(); }
    void waitPrint() { if (printThread_.joinable()) printThread_.join(); library_.waitIdle(); }
private:
};

} // namespace helix
