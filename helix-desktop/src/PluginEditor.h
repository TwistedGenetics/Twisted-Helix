// The HELIX desktop editor: one window with tabs, in the style of the big modern wavetable synths.
//   OSC     two oscillators with 3D wavetable views, two filters with response curves, sub, noise, routing,
//           the modulation sources (envelopes, LFOs, performers) and the macros
//   FX      the effects rack (order, on/off) and the selected effect
//   MATRIX  the 16 modulation slots
//   GLOBAL  voices, engine quality, theme, presets
// Drag any modulation source chip (ENV 2, LFO 1, M1, VEL...) onto a knob to modulate it; right-click a knob to
// change or remove its modulation.
#pragma once
#include "Displays.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "Widgets.h"
#include <map>
#include <memory>
#include <vector>

class HelixEditor;

// Owns the components it creates and the knobs that need refreshing.
class Page : public juce::Component
{
public:
    explicit Page(HelixProcessor& p) : proc(p) {}
    // Children were created (and stored) before the panels that hold them: destroy in that order.
    ~Page() override { for (auto& c : owned) c.reset(); }
    HelixProcessor& proc;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<ParamKnob*> knobs;
    template <typename T> T* own(std::unique_ptr<T> c, juce::Component* parent = nullptr)
    {
        T* raw = c.get();
        (parent ? parent : this)->addAndMakeVisible(*raw);
        if constexpr (std::is_base_of_v<ParamKnob, T>) knobs.push_back(raw);
        owned.push_back(std::move(c));
        return raw;
    }
    ParamKnob* knob(int param, juce::Component* parent, juce::String label = {}, ParamKnob::Size s = ParamKnob::Size::Normal)
    {
        return own(std::make_unique<ParamKnob>(proc, param, label, s), parent);
    }
    ParamCombo* combo(int param, juce::Component* parent, juce::String caption = {})
    {
        return own(std::make_unique<ParamCombo>(proc, param, caption), parent);
    }
    ParamToggle* toggle(int param, juce::Component* parent, juce::String text)
    {
        return own(std::make_unique<ParamToggle>(proc, param, text), parent);
    }
    virtual void tick() { for (auto* k : knobs) k->refresh(); }
};

class HelixEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    static constexpr int kW = 1200, kH = 800;
    explicit HelixEditor(HelixProcessor&);
    ~HelixEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void showTab(int index);
    void applyTheme(int index);
    HelixProcessor& processor() { return proc_; }

private:
    void timerCallback() override;
    void buildHeader();
    void showPresetMenu();
    void stepPreset(int delta);
    void savePreset();

    HelixProcessor& proc_;
    HelixLookAndFeel lnf_;
    juce::TooltipWindow tooltips_{this, 600};
    juce::Component content_;                 // the 1200 x 800 layout, scaled to the window
    std::vector<std::unique_ptr<Page>> tabs_;
    juce::OwnedArray<juce::TextButton> tabButtons_;
    int tab_ = 0;
    bool constructed_ = false;

    // header
    juce::TextButton prev_{"<"}, next_{">"}, presetBox_, save_{"Save"}, mutate_{"Mutate"}, init_{"Init"};
    std::unique_ptr<ParamKnob> master_;
    juce::Label cpu_;
    std::unique_ptr<juce::AlertWindow> saveDialog_;
    uint32_t mutateSeed_ = 1;
};
