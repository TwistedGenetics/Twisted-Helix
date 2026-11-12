#include "HelixMpcApp.h"
#include "helix/WavetableGen.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace helix {

namespace fs = std::filesystem;

// ------------------------------------------------------------------------------------------- param defs

namespace mpcchoices {
static const std::vector<std::string> frames = {"8", "16", "32", "64", "128", "256"};
static const std::vector<std::string> slot = {"Osc A", "Osc B"};
static const std::vector<std::string> bank = {"1-16", "17-32", "33-48", "49-64", "65-80", "81-96", "97-112", "113-128"};
}

static ParamDef extraDefs[kMpcNumParams - kMpcReserveEnd] = {
    {"mpc_preset", "Preset", ParamKind::Int, 0, kMaxPresets - 1, 0, "", nullptr, false},
    {"mpc_load", "Load", ParamKind::Bool, 0, 1, 0, "", nullptr, false},
    {"mpc_pad_browse", "Pad Browse", ParamKind::Bool, 0, 1, 0, "", nullptr, false},
    {"mpc_bank", "Pad Bank", ParamKind::Choice, 0, 7, 0, "", &mpcchoices::bank, false},
    {"mpc_mutate", "Mutate 10%", ParamKind::Bool, 0, 1, 0, "", nullptr, false},
    {"mpc_cpu", "CPU", ParamKind::Linear, 0, 100, 0, "%", nullptr, false},
    {"mpc_s2t_file", "Sample", ParamKind::Int, 0, 255, 0, "", nullptr, false},
    {"mpc_s2t_frames", "Frames", ParamKind::Choice, 0, 5, 3, "", &mpcchoices::frames, false},
    {"mpc_s2t_slot", "To Osc", ParamKind::Choice, 0, 1, 0, "", &mpcchoices::slot, false},
    {"mpc_s2t_go", "Make Table", ParamKind::Bool, 0, 1, 0, "", nullptr, false},
};

bool mpcIsFiller(int i) { return i >= kNumParams && i < kMpcReserveEnd; }

const ParamDef& mpcParamDef(int i)
{
    if (i >= 0 && i < kNumParams) return paramDef(i);
    if (i >= kMpcReserveEnd && i < kMpcNumParams) return extraDefs[i - kMpcReserveEnd];
    static std::vector<ParamDef> fillers;
    static std::vector<std::string> ids;
    if (fillers.empty())
    {
        ids.reserve((size_t)(kMpcReserveEnd - kNumParams));
        for (int k = kNumParams; k < kMpcReserveEnd; ++k) ids.push_back("reserved_" + std::to_string(k));
        for (int k = kNumParams; k < kMpcReserveEnd; ++k)
            fillers.push_back({ids[(size_t)(k - kNumParams)].c_str(), "Reserved", ParamKind::Linear, 0, 1, 0, "", nullptr, false});
    }
    return fillers[(size_t)clamp(i - kNumParams, 0, (int)fillers.size() - 1)];
}

int s2tFrames(int c) { static const int f[] = {8, 16, 32, 64, 128, 256}; return f[clamp(c, 0, 5)]; }

// ------------------------------------------------------------------------------------------- app

HelixMpcApp::HelixMpcApp(IPlatform& platform, bool startWorker) : platform_(platform)
{
    for (int i = kMpcReserveEnd; i < kMpcNumParams; ++i) extra_[i - kMpcReserveEnd].store(extraDefs[i - kMpcReserveEnd].def);
    library_.setRoots({(fs::path(platform_.contentDir()) / "Wavetables").string()});
    library_.rescan();
    scanContent();
    if (startWorker) worker_ = std::thread([this] { workerLoop(); });
}

HelixMpcApp::~HelixMpcApp()
{
    quit_ = true;
    if (worker_.joinable()) worker_.join();
}

void HelixMpcApp::prepare(double sampleRate, int maxBlock) { engine_.prepare(sampleRate, maxBlock); }

void HelixMpcApp::scanContent()
{
    std::vector<Preset> list(factoryPresets().begin(), factoryPresets().end());
    std::vector<std::string> samples;
    std::error_code ec;
    const fs::path root(platform_.contentDir());
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(root / "Presets", ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".json") files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files)
    {
        if ((int)list.size() >= kMaxPresets) break;
        std::ifstream in(f);
        std::stringstream ss; ss << in.rdbuf();
        Preset p;
        if (Preset::fromString(ss.str(), p)) { if (p.name.empty()) p.name = f.stem().string(); list.push_back(p); }
    }
    for (const auto& de : fs::directory_iterator(root / "Samples", ec))
    {
        std::string ext = de.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (de.is_regular_file(ec) && ext == ".wav") samples.push_back(de.path().string());
    }
    std::sort(samples.begin(), samples.end());
    std::lock_guard<std::mutex> lk(presetsMutex_);
    presets_ = std::move(list);
    samples_ = std::move(samples);
}

int HelixMpcApp::numPresets() const { std::lock_guard<std::mutex> lk(presetsMutex_); return (int)presets_.size(); }
std::string HelixMpcApp::presetName(int i) const
{
    std::lock_guard<std::mutex> lk(presetsMutex_);
    return i >= 0 && i < (int)presets_.size() ? presets_[(size_t)i].name : std::string("-");
}

void HelixMpcApp::setParam(int index, float plain)
{
    if (index >= 0 && index < kNumParams) engine_.params().set(index, plain);
    else if (index >= kMpcReserveEnd && index < kMpcNumParams) extra_[index - kMpcReserveEnd].store(mpcParamDef(index).clampPlain(plain));
}

float HelixMpcApp::getParam(int index) const
{
    if (index >= 0 && index < kNumParams) return engine_.params().get(index);
    if (index >= kMpcReserveEnd && index < kMpcNumParams) return extra_[index - kMpcReserveEnd].load();
    return 0.0f;
}

bool HelixMpcApp::formatValue(int index, float plain, std::string& out) const
{
    char buf[64];
    switch (index)
    {
        case P_A_TABLE: case P_B_TABLE: { out = const_cast<TableLibrary&>(library_).nameAt((int)plain); return !out.empty(); }
        case MP_PRESET: out = presetName((int)plain); return true;
        case MP_CPU:
        {
            const int guard = const_cast<Engine&>(engine_).guardLevel();
            std::snprintf(buf, sizeof buf, guard ? "%.0f%% ECO%d" : "%.0f%%", (double)plain, guard);
            out = buf;
            return true;
        }
        case MP_S2T_FILE:
        {
            std::lock_guard<std::mutex> lk(presetsMutex_);
            const int i = (int)plain;
            out = samples_.empty() ? std::string("(no samples)") : (i < (int)samples_.size() ? fs::path(samples_[(size_t)i]).stem().string() : std::string("-"));
            return true;
        }
        case MP_LOAD: case MP_MUTATE: case MP_S2T_GO: out = plain >= 0.5f ? "..." : "Press"; return true;
        default: return false;
    }
}

void HelixMpcApp::applyPreset(const Preset& p)
{
    float before[kNumParams];
    for (int i = 0; i < kNumParams; ++i) before[i] = engine_.params().get(i);
    helix::applyPreset(p, engine_, &library_);
    for (int s = 0; s < 2; ++s) lastTable_[s] = engine_.params().getInt(s == 0 ? P_A_TABLE : P_B_TABLE);
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = engine_.params().get(i);
        if (v != before[i]) platform_.notifyParameter(i, v);
    }
    currentName_ = p.name;
}

void HelixMpcApp::loadPresetIndex(int index, int auditionNote)
{
    Preset p;
    {
        std::lock_guard<std::mutex> lk(presetsMutex_);
        if (index < 0 || index >= (int)presets_.size()) return;
        p = presets_[(size_t)index];
    }
    applyPreset(p);
    setParam(MP_PRESET, (float)index);
    platform_.notifyParameter(MP_PRESET, (float)index);
    if (auditionNote >= 0)
    {
        library_.waitIdle();   // the tables are in the engine's queue; the audio thread swaps them before the note
        injected_.push(MidiMsg::make(0, 0x90, (uint8_t)auditionNote, 100));
    }
}

void HelixMpcApp::onProgramSelected(int index)
{
    if (index < 0 || index >= (int)factoryPresets().size()) return;
    // Plain values were set by the host; LFO shapes and tables are not parameters, so apply the whole preset.
    applyPreset(factoryPresets()[(size_t)index]);
}

std::string HelixMpcApp::saveState() const
{
    Preset p = capturePreset(engine_, &library_);
    p.name = currentName_;
    return p.toString();
}

void HelixMpcApp::loadState(const std::string& json)
{
    Preset p;
    if (Preset::fromString(json, p)) applyPreset(p);
}

void HelixMpcApp::process(float* l, float* r, int frames, const MidiMsg* midi, int numMidi, const Transport& tr)
{
    // Pad browse: pads 1-16 (notes 36-51) pick a preset of the current bank; the note is swallowed and
    // replayed by the worker once the preset is in, so you hear the new sound.
    constexpr int kMaxEvents = 1024;
    MidiMsg buf[kMaxEvents];
    int n = 0;
    MidiMsg inj;
    while (n < kMaxEvents / 2 && injected_.pop(inj)) buf[n++] = inj;
    const bool browse = extra_[MP_PAD_BROWSE - kMpcReserveEnd].load(std::memory_order_relaxed) >= 0.5f;
    const int bank = (int)extra_[MP_PRESET_BANK - kMpcReserveEnd].load(std::memory_order_relaxed);
    for (int i = 0; i < numMidi && n < kMaxEvents; ++i)
    {
        const MidiMsg& m = midi[i];
        if (browse && m.status() == 0x90 && m.data[2] > 0 && m.data[1] >= 36 && m.data[1] <= 51)
        {
            padRequest_.store(((bank * 16 + (m.data[1] - 36)) << 8) | m.data[1], std::memory_order_release);
            continue;
        }
        buf[n++] = m;
    }
    engine_.process(l, r, frames, buf, n, tr);
}

void HelixMpcApp::tick()
{
    engine_.collectGarbage();

    // Program changes from the host's preset menu.
    const int prog = platform_.currentProgram();
    if (prog != lastProgram_) { if (lastProgram_ != -1 || prog > 0) onProgramSelected(prog); lastProgram_ = prog; }

    // Table browsing.
    for (int s = 0; s < 2; ++s)
    {
        const int idx = engine_.params().getInt(s == 0 ? P_A_TABLE : P_B_TABLE);
        if (idx != lastTable_[s])
        {
            if (lastTable_[s] >= 0) library_.requestLoad(s, idx);   // wraps
            lastTable_[s] = idx;
        }
    }

    // Triggers (momentary parameters).
    auto trigger = [&](int p) {
        if (getParam(p) < 0.5f) return false;
        setParam(p, 0.0f);
        platform_.notifyParameter(p, 0.0f);
        return true;
    };
    if (trigger(MP_LOAD)) loadPresetIndex((int)getParam(MP_PRESET));
    if (trigger(MP_MUTATE))
    {
        Preset m = capturePreset(engine_, &library_);
        mutatePreset(m, 0.1f, (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count());
        m.name = currentName_.find(" *") == std::string::npos ? currentName_ + " *" : currentName_;
        applyPreset(m);
    }
    if (trigger(MP_S2T_GO))
    {
        std::string path;
        {
            std::lock_guard<std::mutex> lk(presetsMutex_);
            const int i = (int)getParam(MP_S2T_FILE);
            if (i >= 0 && i < (int)samples_.size()) path = samples_[(size_t)i];
        }
        if (!path.empty()) library_.requestLoadFile((int)getParam(MP_S2T_SLOT), path, s2tFrames((int)getParam(MP_S2T_FRAMES)));
    }

    // Pad browse requests from the audio thread.
    const int req = padRequest_.exchange(-1, std::memory_order_acq_rel);
    if (req >= 0) loadPresetIndex(req >> 8, req & 0xFF);

    // CPU readout, ~3 times a second, only when the number changes.
    if (++cpuCounter_ >= 10)
    {
        cpuCounter_ = 0;
        const float cpu = std::round(engine_.cpuLoad() * 100.0f);
        if (cpu != lastCpuShown_) { lastCpuShown_ = cpu; setParam(MP_CPU, cpu); platform_.notifyParameter(MP_CPU, cpu); }
    }
}

void HelixMpcApp::workerLoop()
{
    while (!quit_.load())
    {
        tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

} // namespace helix
