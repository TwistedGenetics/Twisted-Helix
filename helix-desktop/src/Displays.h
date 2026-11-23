// HELIX editor displays: the 3D wavetable view, the filter response, envelope / LFO / performer views.
#pragma once
#include "Widgets.h"
#include "helix/Filter.h"

// Wavetable display. 3D: every frame (up to 48 shown) stacked in perspective, back to front, each one filled so
// it hides the frames behind it, the current frame lit with a glow and drawn through the oscillator's phase warps.
// 2D: the current frame large, with its neighbours faint. Click the 3D / 2D tag to switch.
class WavetableView : public juce::Component
{
public:
    WavetableView(HelixProcessor& p, int slot);
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    // Repaints only when the table, position or warps changed (called by the editor's timer).
    void refresh();
private:
    float position() const;
    HelixProcessor& proc_;
    int slot_;
    bool threeD_ = true;
    juce::Rectangle<int> modeTag_;
    const void* lastTable_ = nullptr;
    float lastPos_ = -1, lastW_[4] = {-1, -1, -1, -1};
};

// Filter magnitude response, measured from the real filter's impulse response (recomputed when settings change).
class FilterResponseView : public juce::Component
{
public:
    FilterResponseView(HelixProcessor& p, int filter) : proc_(p), filter_(filter) {}
    void paint(juce::Graphics&) override;
    void refresh();
private:
    void compute();
    HelixProcessor& proc_;
    int filter_;
    float key_[6] = {-1, -1, -1, -1, -1, -1};
    std::vector<float> db_;   // response at log-spaced frequencies 20 Hz..20 kHz
};

class EnvelopeView : public juce::Component
{
public:
    EnvelopeView(HelixProcessor& p, int env) : proc_(p), env_(env) {}
    void paint(juce::Graphics&) override;
    void refresh();
private:
    HelixProcessor& proc_;
    int env_;
    float key_[7] = {};
    std::vector<float> curve_;
    int releaseAt_ = 0;
};

// LFO shape. Steps shape: drag to set the steps. Breakpoint shape: drag points, double-click to add,
// right-click to remove.
class LfoView : public juce::Component
{
public:
    LfoView(HelixProcessor& p, int lfo) : proc_(p), lfo_(lfo) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragPoint_ = -1; }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void refresh() { repaint(); }
private:
    float valueAt(float phase) const;
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(8.0f, 10.0f); }
    void editSteps(const juce::MouseEvent&);
    HelixProcessor& proc_;
    int lfo_;
    int dragPoint_ = -1;
};

// Performer steps as bars with their step shapes; drag to set values, right-click a bar to choose its shape.
class PerformerView : public juce::Component
{
public:
    PerformerView(HelixProcessor& p, int perf) : proc_(p), perf_(perf) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void refresh() { repaint(); }
private:
    void setStep(const juce::MouseEvent&);
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(8.0f, 10.0f); }
    HelixProcessor& proc_;
    int perf_;
};

void drawDisplayBackground(juce::Graphics& g, juce::Rectangle<float> r);
