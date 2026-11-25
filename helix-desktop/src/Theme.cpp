#include "Theme.h"

static const Theme kThemes[] = {
    // Copper: charcoal panels, copper for values and the live wave, teal for modulation.
    {"Copper", true,
     juce::Colour(0xff111317), juce::Colour(0xff1a1d23), juce::Colour(0xff22262e), juce::Colour(0xff2f343e),
     juce::Colour(0xffeceae6), juce::Colour(0xff8b919c), juce::Colour(0xffe8793a), juce::Colour(0xfff2b35b),
     juce::Colour(0xff34d1c4), juce::Colour(0xff2b6f72), juce::Colour(0xff363b46), juce::Colour(0xff0c0e11),
     juce::Colour(0xff1f2329)},
    // Paper: light background, dark text (the MPC look).
    {"Paper", false,
     juce::Colour(0xfff3f1ec), juce::Colour(0xffffffff), juce::Colour(0xfff8f6f2), juce::Colour(0xffcfcac0),
     juce::Colour(0xff15171a), juce::Colour(0xff5d6168), juce::Colour(0xffd6402b), juce::Colour(0xffb5651d),
     juce::Colour(0xff1f6fd1), juce::Colour(0xff9db4c6), juce::Colour(0xffdcd8cf), juce::Colour(0xfffbfaf7),
     juce::Colour(0xffeceae4)},
    // Massive: navy panels framed in light grey, white text, blue accents, black displays (slate between panels so
    // any text drawn there stays readable)
    {"Massive", true,
     juce::Colour(0xff3b4050), juce::Colour(0xff222538), juce::Colour(0xff2a2e45), juce::Colour(0xffc9cdd3),
     juce::Colour(0xffffffff), juce::Colour(0xffc6ccda), juce::Colour(0xff46acec), juce::Colour(0xfff2c230),
     juce::Colour(0xff6ce070), juce::Colour(0xff1c3a55), juce::Colour(0xff424763), juce::Colour(0xff0c0e1a),
     juce::Colour(0xff262b42)},
};

static int g_theme = 2;
const Theme& theme() { return kThemes[g_theme]; }
int themeIndex() { return g_theme; }
void setThemeIndex(int i) { g_theme = juce::jlimit(0, numThemes() - 1, i); }
int numThemes() { return (int)(sizeof kThemes / sizeof kThemes[0]); }
const Theme& themeAt(int i) { return kThemes[juce::jlimit(0, numThemes() - 1, i)]; }

void HelixLookAndFeel::applyTheme()
{
    const Theme& t = theme();
    setColour(juce::ResizableWindow::backgroundColourId, t.bg);
    setColour(juce::Label::textColourId, t.text);
    setColour(juce::ComboBox::backgroundColourId, t.panel2);
    setColour(juce::ComboBox::textColourId, t.text);
    setColour(juce::ComboBox::outlineColourId, t.edge);
    setColour(juce::ComboBox::arrowColourId, t.muted);
    setColour(juce::PopupMenu::backgroundColourId, t.panel2);
    setColour(juce::PopupMenu::textColourId, t.text);
    setColour(juce::PopupMenu::headerTextColourId, t.muted);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, t.accent);
    setColour(juce::PopupMenu::highlightedTextColourId, t.dark ? t.bg : juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, t.panel2);
    setColour(juce::TextButton::buttonOnColourId, t.accent);
    setColour(juce::TextButton::textColourOffId, t.text);
    setColour(juce::TextButton::textColourOnId, t.dark ? t.bg : juce::Colours::white);
    setColour(juce::ToggleButton::textColourId, t.text);
    setColour(juce::ListBox::backgroundColourId, t.panel2);
    setColour(juce::ListBox::outlineColourId, t.edge);
    setColour(juce::ScrollBar::thumbColourId, t.edge);
    setColour(juce::TextEditor::backgroundColourId, t.panel2);
    setColour(juce::TextEditor::textColourId, t.text);
    setColour(juce::TextEditor::outlineColourId, t.edge);
    setColour(juce::AlertWindow::backgroundColourId, t.panel);
    setColour(juce::AlertWindow::textColourId, t.text);
    setColour(juce::Slider::textBoxTextColourId, t.text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

// Rotary knob: a track arc, the value arc (from the centre for bipolar knobs), a modulation range arc outside it
// (slider properties "modLo"/"modHi", normalised) and a live dot ("live", < 0 = none).
void HelixLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                        juce::Slider& s)
{
    const Theme& t = theme();
    auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(5.0f);
    const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = start + pos * (end - start);
    const float thick = juce::jmax(2.5f, r * 0.13f);

    // body: a domed cap with a soft shadow, lit from the top left, and a faint accent rim (glass, 0.5.1)
    const bool massive = t.name == "Massive";              // Massive: white knob caps with a dark pointer
    const juce::Colour cap = massive ? juce::Colour(0xffeceef2) : t.dark ? juce::Colour(0xff1a1d24) : t.panel2;
    const auto capR = juce::Rectangle<float>(c.x - r * 0.68f, c.y - r * 0.68f, r * 1.36f, r * 1.36f);
    {
        juce::Path capPath;
        capPath.addEllipse(capR);
        juce::DropShadow(juce::Colours::black.withAlpha(0.55f), juce::jmax(3, (int)(r * 0.22f)), {0, (int)juce::jmax(2.0f, r * 0.10f)})
            .drawForPath(g, capPath);
    }
    g.setGradientFill(juce::ColourGradient(cap.brighter(0.35f), c.x - r * 0.30f, c.y - r * 0.42f,
                                           cap.darker(massive ? 0.28f : 0.45f), c.x + r * 0.55f, c.y + r * 0.62f, true));
    g.fillEllipse(capR);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(massive ? 0.55f : 0.18f), c.x, capR.getY(),
                                           juce::Colours::white.withAlpha(0.0f), c.x, c.y, false));
    g.fillEllipse(capR.reduced(r * 0.10f).withHeight(r * 0.62f));
    g.setColour(t.accent.withAlpha(0.35f));
    g.drawEllipse(capR, 1.0f);

    juce::Path track;
    track.addCentredArc(c.x, c.y, r - thick * 0.5f, r - thick * 0.5f, 0.0f, start, end, true);
    g.setColour(t.track);
    g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getProperties().getWithDefault("bipolar", false);
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path val;
    val.addCentredArc(c.x, c.y, r - thick * 0.5f, r - thick * 0.5f, 0.0f, juce::jmin(from, ang), juce::jmax(from, ang), true);
    {
        juce::Path lit;
        juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(lit, val);
        if (s.isEnabled() && std::abs(ang - from) > 0.01f)
            juce::DropShadow(t.accent.withAlpha(0.75f), juce::jmax(4, (int)(thick * 1.6f)), {0, 0}).drawForPath(g, lit);
        g.setColour(s.isEnabled() ? t.accent : t.muted);
        g.fillPath(lit);
        juce::Path core;   // bright core along the arc
        juce::PathStrokeType(thick * 0.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(core, val);
        if (s.isEnabled()) { g.setColour(t.accent.brighter(0.6f).withAlpha(0.8f)); g.fillPath(core); }
    }

    // modulation range (outside ring)
    const float lo = (float)(double)s.getProperties().getWithDefault("modLo", -1.0);
    const float hi = (float)(double)s.getProperties().getWithDefault("modHi", -1.0);
    if (lo >= 0.0f && hi >= 0.0f && std::abs(hi - lo) > 0.002f)
    {
        juce::Path m;
        const float rr = r + 2.5f;
        m.addCentredArc(c.x, c.y, rr, rr, 0.0f, start + lo * (end - start), start + hi * (end - start), true);
        g.setColour(t.mod);
        g.strokePath(m, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float live = (float)(double)s.getProperties().getWithDefault("live", -1.0);
    if (live >= 0.0f)
    {
        const float a = start + live * (end - start), rr = r + 2.5f;
        g.setColour(t.mod);
        g.fillEllipse(c.x + rr * std::sin(a) - 3.0f, c.y - rr * std::cos(a) - 3.0f, 6.0f, 6.0f);
    }

    // pointer
    juce::Path p;
    p.addRoundedRectangle(-1.4f, -r * 0.62f, 2.8f, r * 0.36f, 1.4f);
    g.setColour(massive ? juce::Colour(0xff181b2a) : t.text);
    g.fillPath(p, juce::AffineTransform::rotation(ang).translated(c.x, c.y));
}

void HelixLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        juce::Slider::SliderStyle, juce::Slider& s)
{
    const Theme& t = theme();
    auto b = juce::Rectangle<float>((float)x, (float)y + h * 0.5f - 3.0f, (float)w, 6.0f);
    g.setColour(t.track);
    g.fillRoundedRectangle(b, 3.0f);
    const bool bipolar = s.getProperties().getWithDefault("bipolar", false);
    const float from = bipolar ? b.getCentreX() : b.getX();
    g.setColour(t.accent);
    g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmin(from, pos), b.getY(), std::abs(pos - from), b.getHeight()), 3.0f);
    g.setColour(t.text);
    g.fillEllipse(pos - 5.0f, b.getCentreY() - 5.0f, 10.0f, 10.0f);
}

void HelixLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const Theme& t = theme();
    auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(0.5f);
    ui::glassFill(g, r, 4.0f, t.panel2, 0.12f, 0.25f);
    ui::glassSheen(g, r, 4.0f, 0.10f);
    g.setColour(box.hasKeyboardFocus(false) ? t.accent : t.edge);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float)w - 11.0f, ay = (float)h * 0.5f;
    arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(t.muted);
    g.fillPath(arrow);
}

juce::Font HelixLookAndFeel::getComboBoxFont(juce::ComboBox& b) { return ui::font(juce::jmin(13.0f, b.getHeight() * 0.6f)); }

void HelixLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(4, 0, box.getWidth() - 20, box.getHeight());
    label.setFont(getComboBoxFont(box));
}

void HelixLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const Theme& t = theme();
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    const juce::Colour base = on ? t.accent : (down ? t.edge : (over ? t.panel2.brighter(t.dark ? 0.08f : -0.04f) : t.panel2));
    if (on)
    {
        juce::Path glowPath;
        glowPath.addRoundedRectangle(r, 4.0f);
        juce::DropShadow(t.accent.withAlpha(0.6f), 8, {0, 0}).drawForPath(g, glowPath);
    }
    ui::glassFill(g, r, 4.0f, base, 0.18f, 0.28f);
    ui::glassSheen(g, r, 4.0f, on ? 0.30f : 0.12f);
    g.setColour(on ? t.accent.brighter(0.3f) : t.edge);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
}

juce::Font HelixLookAndFeel::getTextButtonFont(juce::TextButton&, int h) { return ui::font(juce::jmin(13.5f, h * 0.55f), true); }

// Power switch: a small round button with a lamp.
void HelixLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    const Theme& t = theme();
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const float d = juce::jmin(r.getHeight(), 16.0f);
    auto dot = juce::Rectangle<float>(r.getX() + 1.0f, r.getCentreY() - d / 2, d, d);
    if (on)
    {
        juce::Path lamp;
        lamp.addEllipse(dot);
        juce::DropShadow(t.accent.withAlpha(0.9f), 7, {0, 0}).drawForPath(g, lamp);
    }
    const juce::Colour lampCol = on ? t.accent : (over ? t.edge : t.track);
    g.setGradientFill(juce::ColourGradient(lampCol.brighter(0.6f), dot.getX() + d * 0.35f, dot.getY() + d * 0.3f,
                                           lampCol.darker(0.3f), dot.getRight(), dot.getBottom(), true));
    g.fillEllipse(dot);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawEllipse(dot, 1.0f);
    if (b.getButtonText().isNotEmpty())
    {
        g.setColour(on ? t.text : t.muted);
        g.setFont(ui::font(12.0f, true));
        g.drawText(b.getButtonText(), r.withTrimmedLeft(d + 6.0f), juce::Justification::centredLeft);
    }
}

void HelixLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    const Theme& t = theme();
    g.fillAll(t.panel2);
    g.setColour(t.edge);
    g.drawRect(0, 0, w, h);
}

juce::Font HelixLookAndFeel::getPopupMenuFont() { return ui::font(14.0f); }

void HelixLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                     int thumbStart, int thumbSize, bool, bool)
{
    const Theme& t = theme();
    auto r = vertical ? juce::Rectangle<int>(x + w / 2 - 3, thumbStart, 6, thumbSize) : juce::Rectangle<int>(thumbStart, y + h / 2 - 3, thumbSize, 6);
    g.setColour(t.edge);
    g.fillRoundedRectangle(r.toFloat(), 3.0f);
}
