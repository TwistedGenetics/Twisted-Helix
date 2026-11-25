#include "HelixMpcApp.h"
#include "helix/PrintTable.h"
#include "helix/CharOsc.h"
#include "helix/FxLibrary.h"
#include "helix/ModSystem.h"
#include "helix/WavetableGen.h"
#include "helix/WavetableImport.h"
#include <algorithm>
#include <chrono>
#include <cmath>
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
static const std::vector<std::string> steps = {"Step 1", "Step 2", "Step 3", "Step 4", "Step 5", "Step 6", "Step 7", "Step 8",
                                                "Step 9", "Step 10", "Step 11", "Step 12", "Step 13", "Step 14", "Step 15", "Step 16"};
}

static constexpr int kNumHelixTypes = 13;   // choices::filterType
static constexpr int kNumKinds = 6;         // choices::filterKind

// Filled on first use (not at static initialisation: choices:: lives in another file, whose globals may not be built
// yet when this file's statics are), so extraDefs below only takes its address.
static std::vector<std::string> gCombinedFilters;

const std::vector<std::string>& combinedFilterChoices()
{
    if (gCombinedFilters.empty())
    {
        static const char* shortKind[] = {"LP", "HP", "BP", "Notch", "Peak", "AP"};
        std::vector<std::string> out(choices::filterType.begin(), choices::filterType.end());
        for (size_t m = 1; m < choices::filterModel.size(); ++m)
            for (int k = 0; k < (int)choices::filterKind.size(); ++k) out.push_back(choices::filterModel[m] + " " + shortKind[k]);
        gCombinedFilters = std::move(out);
    }
    return gCombinedFilters;
}

int combinedFilterIndex(int model, int helixType, int megaKind)
{
    if (model <= 0) return clamp(helixType, 0, kNumHelixTypes - 1);
    return kNumHelixTypes + (model - 1) * kNumKinds + clamp(megaKind, 0, kNumKinds - 1);
}

void combinedFilterSplit(int index, int& model, int& helixType, int& megaKind)
{
    if (index < kNumHelixTypes) { model = 0; helixType = index; return; }
    model = 1 + (index - kNumHelixTypes) / kNumKinds;
    megaKind = (index - kNumHelixTypes) % kNumKinds;
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
    {"mpc_p1_edit", "P1 Edit Step", ParamKind::Choice, 0, 15, 0, "", &mpcchoices::steps, false},
    {"mpc_p1_curve", "P1 Curve", ParamKind::Choice, 0, kNumPerfCurves - 1, 0, "", &choices::perfStepShape, false},
    {"mpc_p1_level", "P1 Level", ParamKind::Linear, -1, 1, 1, "", nullptr, false},
    {"mpc_p2_edit", "P2 Edit Step", ParamKind::Choice, 0, 15, 0, "", &mpcchoices::steps, false},
    {"mpc_p2_curve", "P2 Curve", ParamKind::Choice, 0, kNumPerfCurves - 1, 0, "", &choices::perfStepShape, false},
    {"mpc_p2_level", "P2 Level", ParamKind::Linear, -1, 1, 1, "", nullptr, false},
    {"mpc_f1_filter", "F1 Filter", ParamKind::Choice, 0, 114, 0, "", &gCombinedFilters, false},
    {"mpc_f2_filter", "F2 Filter", ParamKind::Choice, 0, 114, 0, "", &gCombinedFilters, false},
#define LIBN(s, S) {"mpc_" s "_na", S " Name A", ParamKind::Linear, 0, 1, 0, "", nullptr, false}, \
                   {"mpc_" s "_nb", S " Name B", ParamKind::Linear, 0, 1, 0, "", nullptr, false}, \
                   {"mpc_" s "_nc", S " Name C", ParamKind::Linear, 0, 1, 0, "", nullptr, false}, \
                   {"mpc_" s "_nd", S " Name D", ParamKind::Linear, 0, 1, 0, "", nullptr, false}, \
                   {"mpc_" s "_ne", S " Name E", ParamKind::Linear, 0, 1, 0, "", nullptr, false}, \
                   {"mpc_" s "_nf", S " Name F", ParamKind::Linear, 0, 1, 0, "", nullptr, false},
    LIBN("i1", "Insert 1") LIBN("i2", "Insert 2") LIBN("x1", "FX 1") LIBN("x2", "FX 2")
#undef LIBN
    {"mpc_pr_src", "Print Sweep", ParamKind::Choice, 0, kNumPrintSources - 1, kPrintGrowl, "", &choices::printSource, false},
    {"mpc_pr_frames", "Print Frames", ParamKind::Choice, 0, 5, 3, "", &mpcchoices::frames, false},
    {"mpc_pr_len", "Print Length", ParamKind::Linear, 0.25f, 8.0f, 2.0f, "s", nullptr, false},
    {"mpc_pr_note", "Print Note", ParamKind::Int, 24, 42, 36, "", nullptr, false},
    {"mpc_pr_slot", "Print To", ParamKind::Choice, 0, 1, 0, "", &mpcchoices::slot, false},
    {"mpc_pr_fx", "Print FX", ParamKind::Bool, 0, 1, 1, "", nullptr, false},
    {"mpc_pr_go", "Print", ParamKind::Bool, 0, 1, 0, "", nullptr, false},
    {"mpc_pr_status", "Print Status", ParamKind::Linear, 0, 1, 0, "", nullptr, false},
    {"mpc_co_na", "CO Name A", ParamKind::Linear, 0, 1, 0, "", nullptr, false},
    {"mpc_co_nb", "CO Name B", ParamKind::Linear, 0, 1, 0, "", nullptr, false},
    {"mpc_co_nc", "CO Name C", ParamKind::Linear, 0, 1, 0, "", nullptr, false},
    {"mpc_co_nd", "CO Name D", ParamKind::Linear, 0, 1, 0, "", nullptr, false},
    {"mpc_co_ne", "CO Name E", ParamKind::Linear, 0, 1, 0, "", nullptr, false},
    {"mpc_co_nf", "CO Name F", ParamKind::Linear, 0, 1, 0, "", nullptr, false},
};
static_assert(sizeof(extraDefs) / sizeof(extraDefs[0]) == kMpcNumParams - kMpcReserveEnd, "extraDefs out of step");

bool mpcIsFiller(int i) { return i >= kNumParams && i < kMpcReserveEnd; }

const ParamDef& mpcParamDef(int i)
{
    combinedFilterChoices();   // make sure the list behind MP_F1/F2_FILTER is built
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
    printCancel_ = true;
    if (worker_.joinable()) worker_.join();
    if (printThread_.joinable()) printThread_.join();
}

// Print to wavetable: snapshot the patch here (worker thread), render on a thread of its own (a few seconds on
// the MPC), write Wavetables/Prints/Print NNN.wav and load it into the chosen oscillator.
void HelixMpcApp::startPrint()
{
    if (printThread_.joinable()) printThread_.join();
    Preset patch = capturePreset(engine_, &library_);
    WavetablePtr ta = engine_.uiTable(kSlotA), tb = engine_.uiTable(kSlotB);
    PrintOptions o;
    o.source = (int)getParam(MP_PR_SRC);
    o.frames = s2tFrames((int)getParam(MP_PR_FRAMES));
    o.seconds = getParam(MP_PR_LEN);
    o.note = (int)std::lround(getParam(MP_PR_NOTE));
    o.effects = getParam(MP_PR_FX) >= 0.5f;
    const int slot = (int)getParam(MP_PR_SLOT);
    const std::string dir = (fs::path(platform_.contentDir()) / "Wavetables" / "Prints").string();
    printing_ = true;
    flipPrintStatus();
    printThread_ = std::thread([this, patch, ta, tb, o, slot, dir] {
        std::string status;
        try
        {
            std::vector<float> raw;
            std::string err;
            const std::string path = nextPrintPath(dir);
            const std::string name = fs::path(path).stem().string();
            WavetablePtr t = path.empty() ? nullptr : printToTable(patch, ta, tb, o, name, &raw, &err, &printCancel_);
            if (!t) status = err.empty() ? "Failed" : err;
            else if (!writePrintWav(path, raw)) status = "Can't write";
            else
            {
                // Rescan so the print is a library entry, then let the worker point the oscillator's table at it
                library_.rescan();
                library_.waitIdle();
                const int idx = library_.indexOfId("file:Prints/" + fs::path(path).filename().string());
                if (idx >= 0) printLoad_ = (slot << 16) | idx;
                else library_.requestLoadFile(slot, path);
                status = name;
            }
        }
        catch (...) { status = "Failed"; }
        { std::lock_guard<std::mutex> lk(printMutex_); printStatus_ = status; }
        printing_ = false;
        printDone_ = true;   // the worker redraws the status (notifyParameter stays on the worker thread)
    });
}

void HelixMpcApp::flipPrintStatus()
{
    const float v = getParam(MP_PR_STATUS) >= 0.5f ? 0.0f : 1.0f;   // a new value, so the status label redraws
    setParam(MP_PR_STATUS, v);
    platform_.notifyParameter(MP_PR_STATUS, v);
}

void HelixMpcApp::prepare(double sampleRate, int maxBlock) { engine_.prepare(sampleRate, maxBlock); }

void HelixMpcApp::scanContent()
{
    try { scanContentImpl(); } catch (...) {}   // a broken drive must never take MPC down
}

std::vector<Preset> HelixMpcApp::userPresets(const std::string& contentDir)
{
    std::vector<Preset> list;
    try
    {
        const fs::path root(contentDir);
        std::vector<std::string> all, files;
        listFilesRecursive((root / "Presets").string(), all);
        for (const std::string& f : all) if (fs::path(f).extension() == ".json") files.push_back(f);
        std::sort(files.begin(), files.end());
        for (const auto& f : files)
        {
            if ((int)(list.size() + factoryPresets().size()) >= kMaxPresets) break;
            std::ifstream in(f);
            std::stringstream ss; ss << in.rdbuf();
            Preset p;
            if (Preset::fromString(ss.str(), p)) { if (p.name.empty()) p.name = fs::path(f).stem().string(); list.push_back(p); }
        }
    }
    catch (...) {}
    return list;
}

void HelixMpcApp::scanContentImpl()
{
    std::vector<Preset> list(factoryPresets().begin(), factoryPresets().end());
    std::vector<std::string> samples;
    std::error_code ec;
    const fs::path root(platform_.contentDir());
    for (Preset& p : userPresets(platform_.contentDir())) list.push_back(std::move(p));
    for (const std::string& f : listDirectory((root / "Samples").string()))
    {
        std::string ext = fs::path(f).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (fs::is_regular_file(f, ec) && ext == ".wav") samples.push_back(f);
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
        case MP_PR_GO: out = plain >= 0.5f || printing_.load() ? "..." : "Press"; return true;
        case MP_PR_STATUS:
        {
            std::lock_guard<std::mutex> lk(printMutex_);
            out = printing_.load() ? std::string("Printing...") : (printStatus_.empty() ? std::string("-") : printStatus_);
            return true;
        }
        case MP_PR_NOTE:
        {
            static const char* nn[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            const int n = (int)std::lround(plain);
            std::snprintf(buf, sizeof buf, "%s%d", nn[((n % 12) + 12) % 12], n / 12 - 1);
            out = buf;
            return true;
        }
        default: break;
    }
    // Effects library: knob names (read-only proxies) and knob values, both following the slot's effect
    auto slotOf = [](int idx, int& s, int& k) {
        for (s = 0; s < fxlib::kNumSlots; ++s)
        {
            const int b = P_I1_TYPE + s * (P_I2_TYPE - P_I1_TYPE);
            if (idx >= b + 2 && idx < b + 2 + fxlib::kNumKnobs) { k = idx - b - 2; return b; }
        }
        return -1;
    };
    if (index >= MP_CONAME && index < MP_CONAME + 6)
    {
        out = charKnobName(engine_.params().getInt(P_CO_TYPE), index - MP_CONAME);
        return true;
    }
    if (index >= MP_LIBNAME && index < MP_LIBNAME + fxlib::kNumSlots * fxlib::kNumKnobs)
    {
        const int s = (index - MP_LIBNAME) / fxlib::kNumKnobs, k = (index - MP_LIBNAME) % fxlib::kNumKnobs;
        const int b = P_I1_TYPE + s * (P_I2_TYPE - P_I1_TYPE);
        float kn[fxlib::kNumKnobs];
        for (int i = 0; i < fxlib::kNumKnobs; ++i) kn[i] = engine_.params().get(b + 2 + i);
        out = fxlib::knobName(engine_.params().getInt(b), k, kn);
        if (out.empty()) out = "-";
        return true;
    }
    {
        int s = 0, k = 0;
        const int b = slotOf(index, s, k);
        if (b >= 0)
        {
            float kn[fxlib::kNumKnobs];
            for (int i = 0; i < fxlib::kNumKnobs; ++i) kn[i] = engine_.params().get(b + 2 + i);
            out = fxlib::knobText(engine_.params().getInt(b), k, plain, kn);
            if (out.empty()) out = "-";
            return true;
        }
    }
    // Continuous core parameters: the compact text the desktop plugin shows ("500 ms", "1.20 s", "12.0 kHz",
    // "80%") instead of the SDK default ("500.0 ms", "12000.0 Hz", "0.80"), so values fit the skin's large type.
    if (index < 0 || index >= kNumParams) return false;
    const ParamDef& d = mpcParamDef(index);
    if (d.kind == ParamKind::Bool || d.kind == ParamKind::Choice || d.kind == ParamKind::Int) return false;
    const float v = d.clampPlain(plain);
    const std::string unit = d.unit ? d.unit : "";
    if (unit == "Hz" && std::fabs(v) >= 1000.0f)
        std::snprintf(buf, sizeof buf, std::fabs(v) >= 10000.0f ? "%.1f kHz" : "%.2f kHz", (double)v / 1000.0);
    else if (unit == "ms" && std::fabs(v) >= 1000.0f)
        std::snprintf(buf, sizeof buf, "%.2f s", (double)v / 1000.0);
    else
    {
        out = d.format(v);
        if (!unit.empty() && out.back() != '%') out += " " + unit;
        return true;
    }
    out = buf;
    return true;
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
    // Programs: the factory presets, then the user presets that were in Presets/ when HELIX loaded (same order as
    // the browser's list). Plain values were set by the host; tables and LFO shapes are not parameters, so the whole
    // preset is applied.
    if (index < 0) return;
    if (index < (int)factoryPresets().size()) { applyPreset(factoryPresets()[(size_t)index]); return; }
    Preset p;
    {
        std::lock_guard<std::mutex> lk(presetsMutex_);
        if (index >= (int)presets_.size()) return;
        p = presets_[(size_t)index];
    }
    applyPreset(p);
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

    // Performer editing (Massive-style): Edit Step chooses a step; Curve and Level show and set that step.
    for (int q = 0; q < 2; ++q)
    {
        const int base = q == 0 ? P_P1_DIV : P_P2_DIV;
        const int ed = q == 0 ? MP_P1_EDIT : MP_P2_EDIT;
        PerfEdit& pe = perfEdit_[q];
        const int step = clamp((int)getParam(ed), 0, kMaxSteps - 1);
        const int shapeP = base + 19 + step, levelP = base + 3 + step;
        const float shape = getParam(shapeP), level = getParam(levelP);
        const float pCurve = getParam(ed + 1), pLevel = getParam(ed + 2);
        auto show = [&](int proxy, float v) { setParam(proxy, v); platform_.notifyParameter(proxy, v); };
        auto write = [&](int core, float v) { setParam(core, v); platform_.notifyParameter(core, v); };
        if (step != pe.step)
        {
            // a new step: show its curve and level
            show(ed + 1, shape); show(ed + 2, level);
        }
        else
        {
            if (pCurve != pe.curve) write(shapeP, pCurve);          // curve tapped in the library
            else if (shape != pe.shape) show(ed + 1, shape);        // changed elsewhere (preset, step page)
            if (pLevel != pe.level) write(levelP, pLevel);
            else if (level != pe.levelCore) show(ed + 2, level);
        }
        // Dragging a step's bar on the DRAW page makes it the step being edited (only when one level moved,
        // so a preset load does not jump the selection).
        int moved = -1, nMoved = 0;
        for (int k = 0; k < kMaxSteps; ++k)
        {
            const float lv = getParam(base + 3 + k);
            if (pe.levels[k] != lv && pe.levels[k] > -9.0f) { moved = k; ++nMoved; }
            pe.levels[k] = lv;
        }
        if (nMoved == 1 && moved != step)
        {
            setParam(ed, (float)moved);
            platform_.notifyParameter(ed, (float)moved);
            show(ed + 1, getParam(base + 19 + moved));
            show(ed + 2, getParam(base + 3 + moved));
            pe.step = moved;
            pe.curve = getParam(ed + 1);
            pe.shape = getParam(base + 19 + moved);
            pe.level = getParam(ed + 2);
            pe.levelCore = getParam(base + 3 + moved);
            continue;
        }
        pe.step = step;
        pe.curve = getParam(ed + 1);
        pe.shape = getParam(shapeP);
        pe.level = getParam(ed + 2);
        pe.levelCore = getParam(levelP);
    }

    // Filter lists (FILTER page screens): one value per filter covering HELIX types and Mega model x type.
    for (int f = 0; f < 2; ++f)
    {
        const int proxy = f == 0 ? MP_F1_FILTER : MP_F2_FILTER;
        const int pModel = f == 0 ? P_F1_MODEL : P_F2_MODEL, pType = f == 0 ? P_F1_TYPE : P_F2_TYPE,
                  pKind = f == 0 ? P_F1_KIND : P_F2_KIND;
        const int core = combinedFilterIndex((int)getParam(pModel), (int)getParam(pType), (int)getParam(pKind));
        const int shown = (int)getParam(proxy);
        int& lastProxy = filterProxy_[f][0];
        int& lastCore = filterProxy_[f][1];
        if (shown != lastProxy && lastProxy >= 0)
        {
            // turned on the FILTER page: write model + type
            int m = 0, t = (int)getParam(pType), k = (int)getParam(pKind);
            combinedFilterSplit(shown, m, t, k);
            auto write = [&](int p, float v) { if (getParam(p) != v) { setParam(p, v); platform_.notifyParameter(p, v); } };
            write(pModel, (float)m);
            if (m == 0) write(pType, (float)t); else write(pKind, (float)k);
        }
        else if (core != lastCore || lastProxy < 0)
        {
            setParam(proxy, (float)core);
            platform_.notifyParameter(proxy, (float)core);
        }
        lastProxy = (int)getParam(proxy);
        lastCore = combinedFilterIndex((int)getParam(pModel), (int)getParam(pType), (int)getParam(pKind));
    }

    // Effects library: a new effect in a slot gets its own defaults (unless the knobs moved with it: a preset), and
    // the knob names redraw.
    for (int sl = 0; sl < fxlib::kNumSlots; ++sl)
    {
        const int b = P_I1_TYPE + sl * (P_I2_TYPE - P_I1_TYPE);
        const int type = (int)getParam(b);
        LibSlot& ls = libSlot_[sl];
        float kn[fxlib::kNumKnobs + 1];
        for (int i = 0; i <= fxlib::kNumKnobs; ++i) kn[i] = getParam(b + 1 + i);
        if (ls.type >= 0 && type != ls.type)
        {
            bool knobsMoved = false;
            for (int i = 0; i <= fxlib::kNumKnobs; ++i) knobsMoved |= kn[i] != ls.knobs[i];
            if (!knobsMoved)
            {
                auto write = [&](int p, float v) { setParam(p, v); platform_.notifyParameter(p, v); };
                write(b + 1, fxlib::mixDefault(type));
                for (int i = 0; i < fxlib::kNumKnobs; ++i) write(b + 2 + i, fxlib::knobDefault(type, i));
            }
        }
        if (type != ls.type)
            for (int i = 0; i < fxlib::kNumKnobs; ++i)
            {
                const int px = MP_LIBNAME + sl * fxlib::kNumKnobs + i;
                const float v = getParam(px) >= 0.5f ? 0.0f : 1.0f;   // a new value, so the name label redraws
                setParam(px, v);
                platform_.notifyParameter(px, v);
            }
        ls.type = type;
        for (int i = 0; i <= fxlib::kNumKnobs; ++i) ls.knobs[i] = getParam(b + 1 + i);
    }

    // Character oscillator: the knob names follow the engine
    {
        const int ct = engine_.params().getInt(P_CO_TYPE);
        const int fam = ct >= kCharDread && ct <= kCharDreadHoover ? 10 + ct : charFamily(ct);   // Dread modes differ
        if (fam != lastCharFam_)
        {
            lastCharFam_ = fam;
            for (int k = 0; k < 6; ++k)
            {
                const float v = getParam(MP_CONAME + k) >= 0.5f ? 0.0f : 1.0f;
                setParam(MP_CONAME + k, v);
                platform_.notifyParameter(MP_CONAME + k, v);
            }
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

    if (trigger(MP_PR_GO) && !printing_.load()) startPrint();
    if (printDone_.exchange(false))
    {
        const int req = printLoad_.exchange(-1);
        if (req >= 0)
        {
            // The rescan may have moved entries: keep both table knobs naming what they play, then select the print.
            for (int s = 0; s < 2; ++s)
            {
                const int at = library_.indexOfId(library_.slotId(s));
                const int tp = s == 0 ? P_A_TABLE : P_B_TABLE;
                if (at >= 0 && library_.wrap(engine_.params().getInt(tp)) != at)
                {
                    setParam(tp, (float)at); lastTable_[s] = at; platform_.notifyParameter(tp, (float)at);
                }
            }
            const int s = req >> 16, idx = req & 0xFFFF, tp = s == 0 ? P_A_TABLE : P_B_TABLE;
            setParam(tp, (float)idx);
            platform_.notifyParameter(tp, (float)idx);
            library_.requestLoad(s, idx);
            lastTable_[s] = idx;
        }
        flipPrintStatus();
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
        try { tick(); } catch (...) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

} // namespace helix
