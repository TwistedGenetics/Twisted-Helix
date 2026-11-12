#include "PluginProcessor.h"
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
    library_.setRoots({userWavetableFolder().getFullPathName().toStdString()});
    library_.rescan();
    startTimerHz(10);
}

juce::File HelixProcessor::userWavetableFolder()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HELIX").getChildFile("Wavetables");
    dir.createDirectory();
    return dir;
}

HelixProcessor::~HelixProcessor() { stopTimer(); }

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
    // Table parameters drive the (asynchronous) table loader.
    for (int slot = 0; slot < helix::kNumTableSlots; ++slot)
    {
        const int idx = engine_.params().getInt(slot == 0 ? helix::P_A_TABLE : helix::P_B_TABLE);
        if (idx != lastTableIndex_[slot] && idx < library_.count())
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
    helix::applyPreset(p, engine_, &library_);
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
    loadPreset(f[(size_t)index]);
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
