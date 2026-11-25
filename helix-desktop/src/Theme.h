// HELIX desktop look: colour themes and the look-and-feel that draws knobs, menus and buttons.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

struct Theme
{
    juce::String name;
    bool dark;
    juce::Colour bg, panel, panel2, edge, text, muted, accent, accent2, mod, waveBack, track, display, displayGrid;
};

// 0 = Copper (dark), 1 = Paper (light), 2 = Massive (default).
const Theme& theme();
int themeIndex();
void setThemeIndex(int index);
int numThemes();
const Theme& themeAt(int index);

class HelixLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HelixLookAndFeel() { applyTheme(); }
    void applyTheme();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                       int thumbSize, bool over, bool down) override;
};

namespace ui {
inline juce::Font font(float size, bool bold = false) { return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)); }

// Glass (0.5.1): a body lit from above (lighter top, deeper bottom) and a sheen across its top half.
inline void glassFill(juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour base, float lift = 0.16f, float depth = 0.32f)
{
    g.setGradientFill(juce::ColourGradient(base.brighter(lift), 0.0f, r.getY(), base.darker(depth), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle(r, corner);
}
inline void glassSheen(juce::Graphics& g, juce::Rectangle<float> r, float corner, float strength = 0.14f)
{
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(r.withHeight(r.getHeight() * 0.5f).toNearestInt());
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(strength), 0.0f, r.getY(),
                                           juce::Colours::white.withAlpha(0.0f), 0.0f, r.getY() + r.getHeight() * 0.5f, false));
    g.fillRoundedRectangle(r, corner);
    g.setColour(juce::Colours::white.withAlpha(strength * 1.6f));
    g.drawHorizontalLine((int)r.getY() + 1, r.getX() + corner, r.getRight() - corner);
}
}
