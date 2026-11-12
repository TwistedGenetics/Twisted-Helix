#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "helix/Engine.h"
#include <vector>

// One HELIX parameter as JUCE sees it. The value lives in the engine's ParamStore (the single source of truth),
// so the host, the editor and the engine can never disagree.
class HelixParameter : public juce::RangedAudioParameter
{
public:
    HelixParameter(helix::ParamStore& store, int index);
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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Init"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    helix::Engine& engine() { return engine_; }
    HelixParameter* parameter(int index) { return params_[(size_t)index]; }
    float cpuLoad() const { return cpuLoad_.load(); }

private:
    void timerCallback() override;

    helix::Engine engine_;
    std::vector<HelixParameter*> params_;
    std::vector<helix::MidiMsg> midi_;
    std::vector<float> scratch_ = std::vector<float>(8192, 0.0f);
    std::atomic<float> cpuLoad_{0.0f};
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelixProcessor)
};
