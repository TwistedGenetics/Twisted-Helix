#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "helix/Engine.h"
#include "helix/TableLibrary.h"
#include "helix/Preset.h"
#include <functional>
#include <vector>

// One HELIX parameter as JUCE sees it. The value lives in the engine's ParamStore (the single source of truth),
// so the host, the editor and the engine can never disagree.
class HelixParameter : public juce::RangedAudioParameter
{
public:
    HelixParameter(helix::ParamStore& store, int index);
    // Optional value text override (table names come from the library).
    std::function<juce::String(float plain)> formatter;
    float getValue() const override { return store_.getNormalised(index_); }
    void setValue(float newValue) override { store_.setNormalised(index_, newValue); }
    float getDefaultValue() const override;
    juce::String getName(int maxLen) const override;
    juce::String getLabel() const override;
    juce::String getText(float normalised, int maxLen) const override;
    float getValueForText(const juce::String& text) const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    bool isBoolean() const override;
    juce::StringArray getAllValueStrings() const override;
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range_; }
    int index() const { return index_; }
private:
    helix::ParamStore& store_;
    int index_;
    juce::NormalisableRange<float> range_{0.0f, 1.0f};
};

class HelixProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    HelixProcessor();
    ~HelixProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HELIX"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return (int)helix::factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    // Presets (message thread)
    void loadPreset(const helix::Preset& p);
    helix::Preset currentPreset() const;
    juce::String presetName() const { return presetName_; }
    static juce::File userPresetFolder();

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    helix::Engine& engine() { return engine_; }
    helix::TableLibrary& library() { return library_; }
    // Wavetables folder the library scans (created on first run).
    static juce::File userWavetableFolder();
    HelixParameter* parameter(int index) { return params_[(size_t)index]; }
    float cpuLoad() const { return cpuLoad_.load(); }

    // ---- editor helpers (message thread)
    // Set a parameter from the UI as one undoable host gesture.
    void setParamFromUI(int index, float plain);
    // Modulation by drag and drop: route source `src` (helix::ModSource) to parameter `param` (or a special
    // destination, given as -DST_*). Reuses the slot that already does it, else the first free slot.
    // Returns the slot used, or -1 when all 16 slots are busy.
    int assignMod(int src, int param, float amount = 0.25f);
    // Matrix slots (0..15) whose destination is `param`.
    std::vector<int> modSlotsFor(int param) const;
    void clearModSlot(int slot);
    static int destIndexFor(int param);   // mod destination index of a parameter, 0 if not modulatable

    struct PresetEntry { juce::String category, name; juce::File file; int factory = -1; };
    // Factory presets, then the user's (Documents/HELIX/Presets/<Category>/*.json). Re-read by refreshPresetList().
    const std::vector<PresetEntry>& presetList() const { return presets_; }
    void refreshPresetList();
    bool loadPresetEntry(int index);
    int currentPresetEntry() const { return currentEntry_; }

    // Per-user settings that are not part of a sound (theme, display options).
    juce::PropertiesFile& settings() { return *settings_; }
private:
    void timerCallback() override;

    helix::Engine engine_;
    helix::TableLibrary library_{engine_};
    int lastTableIndex_[helix::kNumTableSlots] = {-1, -1};
    int currentProgram_ = 0;
    juce::String presetName_ = "Init";
    std::vector<PresetEntry> presets_;
    int currentEntry_ = -1;
    std::unique_ptr<juce::PropertiesFile> settings_;
    std::vector<HelixParameter*> params_;
    std::vector<helix::MidiMsg> midi_;
    std::vector<float> scratch_ = std::vector<float>(8192, 0.0f);
    std::atomic<float> cpuLoad_{0.0f};
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelixProcessor)
};
