#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <memory>
#include <vector>

// Light theme: dark text on a light background, high contrast, large values.
struct HelixColours
{
    static juce::Colour bg()      { return juce::Colour(0xfff3f1ec); }
    static juce::Colour panel()   { return juce::Colour(0xffffffff); }
    static juce::Colour ink()     { return juce::Colour(0xff15171a); }
    static juce::Colour muted()   { return juce::Colour(0xff5d6168); }
    static juce::Colour line()    { return juce::Colour(0xffc9c5bc); }
    static juce::Colour accent()  { return juce::Colour(0xffd6402b); }   // knob value
    static juce::Colour mod()     { return juce::Colour(0xff1f6fd1); }   // modulation ring
};

class HelixLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HelixLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

// One parameter: a knob, a menu or a switch, with its name and value text. Draws a modulation ring when the
// engine reports a live value different from the knob's own.
class ParamControl : public juce::Component
{
public:
    ParamControl(HelixProcessor& proc, int paramIndex);
    void resized() override;
    void paintOverChildren(juce::Graphics&) override;
    void refreshLive();
private:
    HelixProcessor& proc_;
    int index_;
    juce::Label name_;
    std::unique_ptr<juce::Slider> knob_;
    std::unique_ptr<juce::ComboBox> menu_;
    std::unique_ptr<juce::ToggleButton> toggle_;
    std::unique_ptr<juce::SliderParameterAttachment> knobAtt_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> menuAtt_;
    std::unique_ptr<juce::ButtonParameterAttachment> toggleAtt_;
    float live_ = -1.0f;
};

// Stacked 3D-ish view of the wavetable with the current frame highlighted (the moving position line).
class WavetableView : public juce::Component
{
public:
    WavetableView(HelixProcessor& p, int slot);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void chooseFile(bool sampleToTable);
    HelixProcessor& proc_;
    int slot_;
    juce::TextButton loadButton_{"Load WAV..."}, s2tButton_{"Sample > Table..."};
    juce::ComboBox frames_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

class HelixEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HelixEditor(HelixProcessor&);
    ~HelixEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void showPage(int index);

    HelixProcessor& proc_;
    HelixLookAndFeel lnf_;
    juce::OwnedArray<juce::TextButton> tabs_;
    juce::Viewport viewport_;
    std::unique_ptr<juce::Component> page_;
    std::vector<ParamControl*> controls_;
    std::vector<std::unique_ptr<juce::Component>> owned_;
    std::unique_ptr<WavetableView> wtView_;
    juce::Label cpu_;
    int pageIndex_ = 0;
};
