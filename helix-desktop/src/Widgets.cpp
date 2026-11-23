#include "Widgets.h"
#include "helix/ModSystem.h"

using namespace helix;

juce::String modDragId(int source) { return "mod:" + juce::String(source); }

int modSourceFromDrag(const juce::var& d)
{
    const juce::String s = d.toString();
    return s.startsWith("mod:") ? s.substring(4).getIntValue() : -1;
}

juce::String modSourceShortName(int s)
{
    if (s >= 0 && s < (int)choices::modSource.size()) return choices::modSource[(size_t)s];
    return "?";
}

// ------------------------------------------------------------------------------------------- knob

ParamKnob::ParamKnob(HelixProcessor& p, int param, juce::String label, Size size)
    : proc_(p), index_(param), label_(label.isEmpty() ? juce::String(paramDef(param).name) : label), size_(size)
{
    const ParamDef& d = paramDef(param);
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider_.getProperties().set("bipolar", d.min < 0.0f && d.max > 0.0f);
    slider_.setPopupDisplayEnabled(false, false, nullptr);
    addAndMakeVisible(slider_);
    att_ = std::make_unique<juce::SliderParameterAttachment>(*p.parameter(param), slider_);
    slider_.setDoubleClickReturnValue(true, p.parameter(param)->getDefaultValue());   // the slider works in 0..1
    slider_.onValueChange = [this] { repaint(); };
    setTooltip(juce::String(d.name) + (d.modulatable ? "  (drop a modulation source here)" : ""));
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom(size_ == Size::Small ? 13 : 16);
    const int s = juce::jmin(r.getWidth(), r.getHeight());
    slider_.setBounds(r.withSizeKeepingCentre(s, s));
}

void ParamKnob::paint(juce::Graphics& g)
{
    const Theme& t = theme();
    auto r = getLocalBounds();
    auto text = r.removeFromBottom(size_ == Size::Small ? 13 : 16);
    const bool active = slider_.isMouseOverOrDragging();
    g.setColour(active ? t.text : t.muted);
    g.setFont(ui::font(size_ == Size::Small ? 10.5f : 11.5f, active));
    const juce::String s = active ? proc_.parameter(index_)->getCurrentValueAsText() : label_;
    g.drawFittedText(s, text, juce::Justification::centred, 1, 0.8f);
}

void ParamKnob::paintOverChildren(juce::Graphics& g)
{
    if (!dropHover_) return;
    g.setColour(theme().mod.withAlpha(0.25f));
    g.fillEllipse(slider_.getBounds().toFloat().reduced(2.0f));
    g.setColour(theme().mod);
    g.drawEllipse(slider_.getBounds().toFloat().reduced(2.0f), 2.0f);
}

void ParamKnob::refresh()
{
    // The static ring: from the knob value to value + (sum of the matrix amounts aimed at this knob).
    float lo = -1.0f, hi = -1.0f, live = -1.0f;
    if (paramDef(index_).modulatable)
    {
        const auto slots = proc_.modSlotsFor(index_);
        if (!slots.empty())
        {
            const float base = proc_.parameter(index_)->getValue();
            float pos = 0.0f, neg = 0.0f;
            for (int m : slots)
            {
                const int b = P_M1_SRC + m * (P_M2_SRC - P_M1_SRC);
                const float amt = proc_.engine().params().get(b + 2);
                const int src = proc_.engine().params().getInt(b);
                // bipolar sources (LFOs, performers, bend, note, random) swing both ways
                const bool bip = (src >= SRC_LFO1 && src <= SRC_PERF2) || src == SRC_PITCHBEND || src == SRC_NOTE || src == SRC_RANDOM;
                if (bip) { pos += std::abs(amt); neg -= std::abs(amt); }
                else if (amt > 0) pos += amt;
                else neg += amt;
            }
            lo = juce::jlimit(0.0f, 1.0f, base + neg);
            hi = juce::jlimit(0.0f, 1.0f, base + pos);
            if (proc_.engine().activeVoices() > 0) live = proc_.engine().liveNormalised(index_);
        }
    }
    if (lo != lastLo_ || hi != lastHi_ || std::abs(live - lastLive_) > 1e-4f)
    {
        lastLo_ = lo; lastHi_ = hi; lastLive_ = live;
        slider_.getProperties().set("modLo", lo);
        slider_.getProperties().set("modHi", hi);
        slider_.getProperties().set("live", live);
        slider_.repaint();
    }
}

bool ParamKnob::isInterestedInDragSource(const SourceDetails& d)
{
    return paramDef(index_).modulatable && modSourceFromDrag(d.description) > 0;
}

void ParamKnob::itemDropped(const SourceDetails& d)
{
    dropHover_ = false;
    const int src = modSourceFromDrag(d.description);
    if (proc_.assignMod(src, index_) < 0)
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "HELIX",
                                               "All 16 modulation slots are in use. Free one on the MATRIX tab.");
    refresh();
    repaint();
}

void ParamKnob::KnobSlider::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { owner.showModMenu(); return; }
    juce::Slider::mouseDown(e);
}

void ParamKnob::showModMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader(paramDef(index_).name);
    m.addItem(1, "Reset to default");
    const auto slots = proc_.modSlotsFor(index_);
    if (!slots.empty())
    {
        m.addSeparator();
        m.addSectionHeader("Modulated by");
        for (int s : slots)
        {
            const int b = P_M1_SRC + s * (P_M2_SRC - P_M1_SRC);
            const juce::String src = modSourceShortName(proc_.engine().params().getInt(b));
            const int amt = juce::roundToInt(proc_.engine().params().get(b + 2) * 100.0f);
            juce::PopupMenu sub;
            for (int a : {-100, -50, -25, -10, 10, 25, 50, 100}) sub.addItem(1000 + s * 100 + (a + 100) / 5, juce::String(a) + "%");
            sub.addSeparator();
            sub.addItem(100 + s, "Remove");
            m.addSubMenu(src + "  (slot " + juce::String(s + 1) + ", " + juce::String(amt) + "%)", sub);
        }
    }
    juce::Component::SafePointer<ParamKnob> self(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [self](int r) {
        if (!self || r == 0) return;
        auto& proc = self->proc_;
        if (r == 1) proc.setParamFromUI(self->index_, paramDef(self->index_).def);
        else if (r >= 100 && r < 100 + kNumModSlots) proc.clearModSlot(r - 100);
        else if (r >= 1000)
        {
            const int slot = (r - 1000) / 100, amt = ((r - 1000) % 100) * 5 - 100;
            proc.setParamFromUI(P_M1_SRC + slot * (P_M2_SRC - P_M1_SRC) + 2, (float)amt / 100.0f);
        }
        self->refresh();
    });
}

// ------------------------------------------------------------------------------------------- combo / toggle

ParamCombo::ParamCombo(HelixProcessor& p, int param, juce::String caption) : caption_(std::move(caption))
{
    box_.addItemList(p.parameter(param)->getAllValueStrings(), 1);
    addAndMakeVisible(box_);
    att_ = std::make_unique<juce::ComboBoxParameterAttachment>(*p.parameter(param), box_);
}

void ParamCombo::resized()
{
    auto r = getLocalBounds();
    if (caption_.isNotEmpty()) r.removeFromTop(14);
    box_.setBounds(r.withSizeKeepingCentre(r.getWidth(), juce::jmin(r.getHeight(), 24)));
}

void ParamCombo::paint(juce::Graphics& g)
{
    if (caption_.isEmpty()) return;
    g.setColour(theme().muted);
    g.setFont(ui::font(10.5f));
    g.drawText(caption_, getLocalBounds().removeFromTop(14), juce::Justification::centredLeft);
}

ParamToggle::ParamToggle(HelixProcessor& p, int param, juce::String text)
{
    button_.setButtonText(text);
    addAndMakeVisible(button_);
    att_ = std::make_unique<juce::ButtonParameterAttachment>(*p.parameter(param), button_);
}

// ------------------------------------------------------------------------------------------- panel / chip

void PanelFrame::setPower(std::unique_ptr<ParamToggle> t)
{
    power_ = std::move(t);
    addAndMakeVisible(*power_);
    // Only place the switch: this runs from subclass constructors, where a virtual resized() would reach
    // members that do not exist yet.
    power_->setBounds(titleArea().removeFromLeft(18));
}

void PanelFrame::resized()
{
    if (power_) power_->setBounds(titleArea().removeFromLeft(18));
}

void PanelFrame::paint(juce::Graphics& g)
{
    const Theme& t = theme();
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(t.panel);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(t.edge);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    auto title = titleArea();
    if (power_) title.removeFromLeft(22);
    g.setColour(t.text);
    g.setFont(ui::font(12.5f, true));
    g.drawText(title_.toUpperCase(), title, juce::Justification::centredLeft);
}

void ModChip::paint(juce::Graphics& g)
{
    const Theme& t = theme();
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(selected_ ? t.mod.withAlpha(t.dark ? 0.22f : 0.15f) : t.panel2);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(selected_ ? t.mod : t.edge);
    g.drawRoundedRectangle(r, 4.0f, selected_ ? 1.5f : 1.0f);
    // drag grip
    g.setColour(t.mod.withAlpha(0.8f));
    for (int i = 0; i < 3; ++i) g.fillEllipse(r.getX() + 3.5f, r.getCentreY() - 4.5f + (float)i * 3.5f, 2.0f, 2.0f);
    g.setColour(selected_ ? t.text : t.muted);
    g.setFont(ui::font(10.5f, true));
    g.drawFittedText(text_, getLocalBounds().withTrimmedLeft(7).withTrimmedRight(1), juce::Justification::centred, 1, 0.7f);
}

void ModChip::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ || e.getDistanceFromDragStart() < 4) return;
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        dragging_ = true;
        juce::Image img(juce::Image::ARGB, getWidth(), getHeight(), true);
        {
            juce::Graphics g(img);
            paint(g);
        }
        c->startDragging(modDragId(source_), this, juce::ScaledImage(img), true);
    }
}

void ModChip::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_ && onClick) onClick();
    dragging_ = false;
}

void layoutRow(juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int gap)
{
    const int n = (int)items.size();
    if (n == 0) return;
    const int w = (area.getWidth() - gap * (n - 1)) / n;
    int x = area.getX();
    for (auto* c : items)
    {
        if (c) c->setBounds(x, area.getY(), w, area.getHeight());
        x += w + gap;
    }
}
