// HELIX editor widgets: parameter knobs (drop a modulation source on them), menus, switches, panel frames and
// the draggable modulation-source chips.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Theme.h"

// Drag description for a modulation source: "mod:<helix::ModSource>".
juce::String modDragId(int source);
int modSourceFromDrag(const juce::var& description);   // -1 if not a modulation source
juce::String modSourceShortName(int source);

class ParamKnob : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient
{
public:
    enum class Size { Small, Normal, Large };
    ParamKnob(HelixProcessor& p, int param, juce::String label = {}, Size size = Size::Normal);
    void resized() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    // Called by the editor ~30 times a second: modulation range ring and live value.
    void refresh();
    int param() const { return index_; }

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override { dropHover_ = true; repaint(); }
    void itemDragExit(const SourceDetails&) override { dropHover_ = false; repaint(); }
    void itemDropped(const SourceDetails&) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    struct KnobSlider : juce::Slider
    {
        ParamKnob& owner;
        explicit KnobSlider(ParamKnob& o) : owner(o) {}
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent&) override { owner.repaint(); }
        void mouseExit(const juce::MouseEvent&) override { owner.repaint(); }
    };
    void showModMenu();
    HelixProcessor& proc_;
    int index_;
    juce::String label_;
    Size size_;
    KnobSlider slider_{*this};
    std::unique_ptr<juce::SliderParameterAttachment> att_;
    bool dropHover_ = false;
    float lastLo_ = -2, lastHi_ = -2, lastLive_ = -2;
};

class ParamCombo : public juce::Component
{
public:
    ParamCombo(HelixProcessor& p, int param, juce::String caption = {});
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::ComboBox& box() { return box_; }
private:
    juce::String caption_;
    juce::ComboBox box_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> att_;
};

class ParamToggle : public juce::Component
{
public:
    ParamToggle(HelixProcessor& p, int param, juce::String text = {});
    void resized() override { button_.setBounds(getLocalBounds()); }
    juce::ToggleButton& button() { return button_; }
private:
    juce::ToggleButton button_;
    std::unique_ptr<juce::ButtonParameterAttachment> att_;
};

// A rounded panel with a title strip; an optional power switch sits left of the title.
class PanelFrame : public juce::Component
{
public:
    explicit PanelFrame(juce::String title) : title_(std::move(title)) {}
    void paint(juce::Graphics&) override;
    void setPower(std::unique_ptr<ParamToggle> t);
    // Area under the title strip.
    juce::Rectangle<int> content() const { return getLocalBounds().reduced(8, 6).withTrimmedTop(24); }
    juce::Rectangle<int> titleArea() const { return getLocalBounds().reduced(8, 4).removeFromTop(22); }
    void resized() override;
protected:
    juce::String title_;
    std::unique_ptr<ParamToggle> power_;
};

// A modulation source label you can drag onto any knob. Optionally selectable (the mod tabs).
class ModChip : public juce::Component
{
public:
    ModChip(int source, juce::String text) : source_(source), text_(std::move(text)) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setSelected(bool s) { selected_ = s; repaint(); }
    std::function<void()> onClick;
    int source() const { return source_; }
private:
    int source_;
    juce::String text_;
    bool selected_ = false, dragging_ = false;
};

// Lays children out in a row of equal cells.
void layoutRow(juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int gap = 4);
