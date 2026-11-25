#include "PluginProcessor.h"
#include "helix/FxLibrary.h"
#include "PluginEditor.h"
#include "helix/WavetableGen.h"
#include <chrono>

// ---------------------------------------------------------------------------------------------- parameters

HelixParameter::HelixParameter(helix::ParamStore& store, int index)
    : juce::RangedAudioParameter(juce::ParameterID(helix::paramDef(index).id, 1), helix::paramDef(index).name),
      store_(store), index_(index)
{
    const int steps = helix::paramDef(index).numSteps();
    if (steps > 1) range_.interval = 1.0f / (float)(steps - 1);
}

float HelixParameter::getDefaultValue() const { const auto& d = helix::paramDef(index_); return d.toNormalised(d.def); }
static juce::String limit(juce::String s, int maxLen) { return maxLen > 0 ? s.substring(0, maxLen) : s; }   // 0 = no limit
juce::String HelixParameter::getName(int maxLen) const { return limit(helix::paramDef(index_).name, maxLen); }
juce::String HelixParameter::getLabel() const { return helix::paramDef(index_).unit; }
juce::String HelixParameter::getText(float n, int maxLen) const
{
    const auto& d = helix::paramDef(index_);
    if (formatter) return limit(formatter(d.toPlain(n)), maxLen);
    return limit(juce::String(d.format(d.toPlain(n))), maxLen);
}
float HelixParameter::getValueForText(const juce::String& text) const
{
    const auto& d = helix::paramDef(index_);
    if (d.choices)
        for (size_t i = 0; i < d.choices->size(); ++i)
            if (text.equalsIgnoreCase(juce::String((*d.choices)[i]))) return d.toNormalised((float)i);
    if (d.kind == helix::ParamKind::Bool) return text.equalsIgnoreCase("on") || text.getFloatValue() >= 0.5f ? 1.0f : 0.0f;
    float v = text.getFloatValue();
    if (text.endsWithIgnoreCase("k")) v *= 1000.0f;
    if (d.max - d.min <= 2.0f && d.unit[0] == '\0' && text.containsChar('%')) v *= 0.01f;
    return d.toNormalised(v);
}
int HelixParameter::getNumSteps() const { const int s = helix::paramDef(index_).numSteps(); return s > 1 ? s : juce::AudioProcessor::getDefaultNumParameterSteps(); }
bool HelixParameter::isDiscrete() const { return helix::paramDef(index_).numSteps() > 1; }
bool HelixParameter::isBoolean() const { return helix::paramDef(index_).kind == helix::ParamKind::Bool; }
juce::StringArray HelixParameter::getAllValueStrings() const
{
    juce::StringArray a;
    const auto& d = helix::paramDef(index_);
    if (d.choices) for (auto& s : *d.choices) a.add(s);
    return a;
}

// ---------------------------------------------------------------------------------------------- processor

HelixProcessor::HelixProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int i = 0; i < helix::kNumParams; ++i)
    {
        auto* p = new HelixParameter(engine_.params(), i);
        params_.push_back(p);
        addParameter(p);
    }
    midi_.reserve(4096);
    for (int slot = 0; slot < helix::kNumTableSlots; ++slot)
        params_[(size_t)(slot == 0 ? helix::P_A_TABLE : helix::P_B_TABLE)]->formatter = [this](float plain) {
            const auto name = library_.nameAt((int)plain);
            return name.empty() ? juce::String((int)plain) : juce::String(name);
        };
    // Effects library knobs: the text follows the slot's effect ("2.40 s", "1/8 Dotted", "On")
    for (int sl = 0; sl < helix::fxlib::kNumSlots; ++sl)
        for (int k = 0; k < helix::fxlib::kNumKnobs; ++k)
        {
            const int b = helix::P_I1_TYPE + sl * (helix::P_I2_TYPE - helix::P_I1_TYPE);
            params_[(size_t)(b + 2 + k)]->formatter = [this, b, k](float plain) {
                float kn[helix::fxlib::kNumKnobs];
                for (int i = 0; i < helix::fxlib::kNumKnobs; ++i) kn[i] = engine_.params().get(b + 2 + i);
                const auto t = helix::fxlib::knobText(engine_.params().getInt(b), k, plain, kn);
                return t.empty() ? juce::String("-") : juce::String(t);
            };
        }
    library_.setRoots({userWavetableFolder().getFullPathName().toStdString()});
    library_.rescan();
    juce::PropertiesFile::Options opts;
    opts.applicationName = "HELIX-ui";   // not "HELIX": the standalone wrapper keeps its own HELIX.settings
    opts.folderName = "HELIX";
    opts.filenameSuffix = ".settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.millisecondsBeforeSaving = 500;   // save soon after a change, not only on a clean exit
    settings_ = std::make_unique<juce::PropertiesFile>(opts);
    refreshPresetList();
    startTimerHz(10);
}

juce::File HelixProcessor::userWavetableFolder()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HELIX").getChildFile("Wavetables");
    dir.createDirectory();
    return dir;
}

HelixProcessor::~HelixProcessor()
{
    stopTimer();
    printCancel_ = true;
    if (printThread_.joinable()) printThread_.join();
}

void HelixProcessor::startPrint(int slot, const helix::PrintOptions& o)
{
    if (printing_.load()) return;
    if (printThread_.joinable()) printThread_.join();
    const helix::Preset patch = helix::capturePreset(engine_, &library_);
    const helix::WavetablePtr ta = engine_.uiTable(helix::kSlotA), tb = engine_.uiTable(helix::kSlotB);
    const std::string dir = userWavetableFolder().getChildFile("Prints").getFullPathName().toStdString();
    printing_ = true;
    { std::lock_guard<std::mutex> lk(printMutex_); printStatus_ = "Printing..."; }
    printThread_ = std::thread([this, patch, ta, tb, o, slot, dir] {
        std::string status;
        try
        {
            std::vector<float> raw;
            std::string err;
            const std::string path = helix::nextPrintPath(dir);
            const std::string name = juce::File(juce::String(path)).getFileNameWithoutExtension().toStdString();
            helix::WavetablePtr t = path.empty() ? nullptr : helix::printToTable(patch, ta, tb, o, name, &raw, &err, &printCancel_);
            if (!t) status = err.empty() ? "Print failed" : err;
            else if (!helix::writePrintWav(path, raw)) status = "Could not write " + path;
            else
            {
                library_.rescan();
                library_.waitIdle();
                const int idx = library_.indexOfId("file:Prints/" + name + ".wav");
                if (idx >= 0) printLoad_ = (slot << 16) | idx;
                else library_.requestLoadFile(slot, path);
                status = name;
            }
        }
        catch (...) { status = "Print failed"; }
        { std::lock_guard<std::mutex> lk(printMutex_); printStatus_ = status; }
        printing_ = false;
        printDone_ = true;
    });
}

bool HelixProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void HelixProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    engine_.prepare(sampleRate, samplesPerBlock);
    scratch_.assign((size_t)std::max(samplesPerBlock, 8192), 0.0f);
}

void HelixProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = std::chrono::steady_clock::now();
    // An offline bounce must not depend on how fast the computer is: the CPU guard only runs in real time.
    engine_.setRealtime(!isNonRealtime());
    const int n = buffer.getNumSamples();

    midi_.clear();
    for (const auto meta : midi)
    {
        if (midi_.size() == midi_.capacity()) break;   // never reallocate on the audio thread
        const auto msg = meta.getMessage();
        if (msg.getRawDataSize() > 3 || msg.getRawDataSize() < 1) continue;
        helix::MidiMsg m;
        m.offset = juce::jlimit(0, n - 1, meta.samplePosition);
        const auto* raw = msg.getRawData();
        for (int i = 0; i < msg.getRawDataSize(); ++i) m.data[i] = raw[i];
        midi_.push_back(m);
    }

    helix::Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            tr.valid = pos->getBpm().hasValue();
            tr.bpm = pos->getBpm().orFallback(120.0);
            tr.ppq = pos->getPpqPosition().orFallback(0.0);
            tr.playing = pos->getIsPlaying();
        }

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : l;
    if (r == l)
    {
        // Mono bus: render stereo, keep the average.
        float* sr = scratch_.data();
        const int m = std::min(n, (int)scratch_.size());
        engine_.process(l, sr, m, midi_.data(), (int)midi_.size(), tr);
        for (int i = 0; i < m; ++i) l[i] = 0.5f * (l[i] + sr[i]);
    }
    else engine_.process(l, r, n, midi_.data(), (int)midi_.size(), tr);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);

    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const float load = (float)(dt * sampleRate_ / std::max(1, n));
    cpuLoad_.store(cpuLoad_.load() * 0.9f + load * 0.1f);
}

void HelixProcessor::timerCallback()
{
    engine_.collectGarbage();
    // A scene morph writes the macros when it lands: tell the host and the controls
    for (int m = 0; m < helix::kNumMacros; ++m)
    {
        const float v = engine_.params().get(helix::P_MACRO1 + m);
        if (v != lastMacro_[m])
        {
            if (lastMacro_[m] > -1.0f) params_[(size_t)(helix::P_MACRO1 + m)]->sendValueChangedMessageToListeners(params_[(size_t)(helix::P_MACRO1 + m)]->getValue());
            lastMacro_[m] = v;
        }
    }
    if (printDone_.exchange(false))
    {
        const int req = printLoad_.exchange(-1);
        if (req >= 0)
        {
            // The rescan may have moved entries: keep both table parameters naming what they play, then select the print.
            for (int s = 0; s < helix::kNumTableSlots; ++s)
            {
                const int at = library_.indexOfId(library_.slotId(s));
                const int tp = s == 0 ? helix::P_A_TABLE : helix::P_B_TABLE;
                if (at >= 0 && library_.wrap(engine_.params().getInt(tp)) != at) { lastTableIndex_[s] = at; setParamFromUI(tp, (float)at); }
            }
            const int s = req >> 16, idx = req & 0xFFFF;
            lastTableIndex_[s] = idx;
            setParamFromUI(s == 0 ? helix::P_A_TABLE : helix::P_B_TABLE, (float)idx);
            library_.requestLoad(s, idx);
        }
    }
    // Table parameters drive the (asynchronous) table loader.
    for (int slot = 0; slot < helix::kNumTableSlots; ++slot)
    {
        const int idx = engine_.params().getInt(slot == 0 ? helix::P_A_TABLE : helix::P_B_TABLE);
        if (idx != lastTableIndex_[slot])
        {
            if (lastTableIndex_[slot] >= 0) library_.requestLoad(slot, idx);
            lastTableIndex_[slot] = idx;
        }
    }
}

juce::AudioProcessorEditor* HelixProcessor::createEditor() { return new HelixEditor(*this); }

// The plugin state is the HELIX preset JSON: the same format as preset files and the MPC's state chunk.
void HelixProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    const std::string text = currentPreset().toString();
    dest.replaceAll(text.data(), text.size());
}

void HelixProcessor::setStateInformation(const void* data, int size)
{
    helix::Preset p;
    if (helix::Preset::fromString(std::string((const char*)data, (size_t)size), p)) loadPreset(p);
}

helix::Preset HelixProcessor::currentPreset() const
{
    helix::Preset p = helix::capturePreset(engine_, &library_);
    p.name = presetName_.toStdString();
    return p;
}

void HelixProcessor::loadPreset(const helix::Preset& p)
{
    float before[helix::kNumParams];
    for (int i = 0; i < helix::kNumParams; ++i) before[i] = engine_.params().get(i);
    helix::applyPreset(p, engine_, &library_);
    // The values were written straight into the engine: tell JUCE's listeners (the editor's controls and the
    // host) about every parameter that changed, or they keep showing the old preset.
    for (int i = 0; i < helix::kNumParams; ++i)
        if (engine_.params().get(i) != before[i])
            params_[(size_t)i]->sendValueChangedMessageToListeners(params_[(size_t)i]->getValue());
    presetName_ = p.name;
    for (int slot = 0; slot < helix::kNumTableSlots; ++slot)
        lastTableIndex_[slot] = engine_.params().getInt(slot == 0 ? helix::P_A_TABLE : helix::P_B_TABLE);   // loaded by id already
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged(true));
}

void HelixProcessor::setCurrentProgram(int index)
{
    const auto& f = helix::factoryPresets();
    if (index < 0 || index >= (int)f.size()) return;
    currentProgram_ = index;
    currentEntry_ = index;
    loadPreset(f[(size_t)index]);
}

// ---------------------------------------------------------------------------------------------- editor helpers

void HelixProcessor::setParamFromUI(int index, float plain)
{
    if (index < 0 || index >= helix::kNumParams) return;
    auto* p = params_[(size_t)index];
    p->beginChangeGesture();
    p->setValueNotifyingHost(helix::paramDef(index).toNormalised(plain));
    p->endChangeGesture();
}

int HelixProcessor::destIndexFor(int param)
{
    if (param < 0) return -param;   // special destination given as -DST_*
    for (int d = 0; d < helix::numModDests(); ++d) if (helix::modDestParam(d) == param) return d;
    return 0;
}

std::vector<int> HelixProcessor::modSlotsFor(int param) const
{
    std::vector<int> out;
    const int dest = destIndexFor(param);
    if (dest <= 0) return out;
    const int stride = helix::P_M2_SRC - helix::P_M1_SRC;
    for (int m = 0; m < helix::kNumModSlots; ++m)
    {
        const int b = helix::P_M1_SRC + m * stride;
        if (engine_.params().getInt(b + 1) == dest && engine_.params().getInt(b) != helix::SRC_NONE) out.push_back(m);
    }
    return out;
}

int HelixProcessor::assignMod(int src, int param, float amount)
{
    const int dest = destIndexFor(param);
    if (dest <= 0 || src <= helix::SRC_NONE || src >= helix::kNumModSources) return -1;
    const int stride = helix::P_M2_SRC - helix::P_M1_SRC;
    int slot = -1;
    for (int m = 0; m < helix::kNumModSlots && slot < 0; ++m)
    {
        const int b = helix::P_M1_SRC + m * stride;
        if (engine_.params().getInt(b) == src && engine_.params().getInt(b + 1) == dest) return m;   // already there
    }
    for (int m = 0; m < helix::kNumModSlots && slot < 0; ++m)
    {
        const int b = helix::P_M1_SRC + m * stride;
        if (engine_.params().getInt(b) == helix::SRC_NONE || engine_.params().getInt(b + 1) == 0) slot = m;
    }
    if (slot < 0) return -1;
    const int b = helix::P_M1_SRC + slot * stride;
    setParamFromUI(b, (float)src);
    setParamFromUI(b + 1, (float)dest);
    setParamFromUI(b + 2, amount);
    setParamFromUI(b + 3, 0.0f);
    setParamFromUI(b + 4, 0.0f);
    return slot;
}

void HelixProcessor::clearModSlot(int slot)
{
    if (slot < 0 || slot >= helix::kNumModSlots) return;
    const int b = helix::P_M1_SRC + slot * (helix::P_M2_SRC - helix::P_M1_SRC);
    setParamFromUI(b, 0.0f);
    setParamFromUI(b + 1, 0.0f);
    setParamFromUI(b + 2, 0.0f);
    setParamFromUI(b + 3, 0.0f);
    setParamFromUI(b + 4, 0.0f);
}

void HelixProcessor::refreshPresetList()
{
    presets_.clear();
    const auto& f = helix::factoryPresets();
    for (int i = 0; i < (int)f.size(); ++i) presets_.push_back({f[(size_t)i].category, f[(size_t)i].name, {}, i});
    auto files = userPresetFolder().findChildFiles(juce::File::findFiles, true, "*.json");
    files.sort();
    for (const auto& file : files)
        presets_.push_back({file.getParentDirectory().getFileName(), file.getFileNameWithoutExtension(), file, -1});
}

bool HelixProcessor::loadPresetEntry(int index)
{
    if (index < 0 || index >= (int)presets_.size()) return false;
    const auto& e = presets_[(size_t)index];
    if (e.factory >= 0) { setCurrentProgram(e.factory); currentEntry_ = index; return true; }
    helix::Preset p;
    if (!helix::Preset::fromString(e.file.loadFileAsString().toStdString(), p)) return false;
    if (p.name.empty()) p.name = e.name.toStdString();
    loadPreset(p);
    currentEntry_ = index;
    return true;
}

const juce::String HelixProcessor::getProgramName(int index)
{
    const auto& f = helix::factoryPresets();
    return index >= 0 && index < (int)f.size() ? juce::String(f[(size_t)index].name) : juce::String();
}

juce::File HelixProcessor::userPresetFolder()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HELIX").getChildFile("Presets");
    dir.createDirectory();
    return dir;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HelixProcessor(); }
