#include "PluginEditor.h"
#include "helix/UiLayout.h"

using namespace helix;

// ------------------------------------------------------------------------------------------- look and feel

HelixLookAndFeel::HelixLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, HelixColours::bg());
    setColour(juce::Label::textColourId, HelixColours::ink());
    setColour(juce::Slider::textBoxTextColourId, HelixColours::ink());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, HelixColours::panel());
    setColour(juce::ComboBox::textColourId, HelixColours::ink());
    setColour(juce::ComboBox::outlineColourId, HelixColours::line());
    setColour(juce::ComboBox::arrowColourId, HelixColours::ink());
    setColour(juce::PopupMenu::backgroundColourId, HelixColours::panel());
    setColour(juce::PopupMenu::textColourId, HelixColours::ink());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, HelixColours::accent());
    setColour(juce::TextButton::buttonColourId, HelixColours::panel());
    setColour(juce::TextButton::buttonOnColourId, HelixColours::ink());
    setColour(juce::TextButton::textColourOffId, HelixColours::ink());
    setColour(juce::TextButton::textColourOnId, HelixColours::bg());
    setColour(juce::ToggleButton::textColourId, HelixColours::ink());
    setColour(juce::ScrollBar::thumbColourId, HelixColours::line());
}

void HelixLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start,
                                        float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(6.0f);
    const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);

    juce::Path track;
    track.addCentredArc(c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour(HelixColours::line());
    g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getProperties().getWithDefault("bipolar", false);
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path value;
    value.addCentredArc(c.x, c.y, r, r, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
    g.setColour(HelixColours::accent());
    g.strokePath(value, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(HelixColours::ink());
    juce::Path needle;
    needle.addRectangle(-1.5f, -r + 6.0f, 3.0f, r * 0.55f);
    g.fillPath(needle, juce::AffineTransform::rotation(angle).translated(c.x, c.y));
}

void HelixLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced(4.0f);
    const bool on = b.getToggleState();
    g.setColour(on ? HelixColours::ink() : HelixColours::panel());
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(HelixColours::ink());
    g.drawRoundedRectangle(r, 6.0f, 1.5f);
    g.setColour(on ? HelixColours::bg() : HelixColours::ink());
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText(on ? "ON" : "OFF", r, juce::Justification::centred);
}

// ------------------------------------------------------------------------------------------- param control

ParamControl::ParamControl(HelixProcessor& proc, int index) : proc_(proc), index_(index)
{
    const ParamDef& d = paramDef(index);
    auto* param = proc.parameter(index);
    name_.setText(d.name, juce::dontSendNotification);
    name_.setJustificationType(juce::Justification::centred);
    name_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    name_.setColour(juce::Label::textColourId, HelixColours::muted());
    addAndMakeVisible(name_);

    if (d.kind == ParamKind::Bool)
    {
        toggle_ = std::make_unique<juce::ToggleButton>();
        addAndMakeVisible(*toggle_);
        toggleAtt_ = std::make_unique<juce::ButtonParameterAttachment>(*param, *toggle_);
    }
    else if (d.kind == ParamKind::Choice)
    {
        menu_ = std::make_unique<juce::ComboBox>();
        menu_->addItemList(param->getAllValueStrings(), 1);
        addAndMakeVisible(*menu_);
        menuAtt_ = std::make_unique<juce::ComboBoxParameterAttachment>(*param, *menu_);
    }
    else
    {
        knob_ = std::make_unique<juce::Slider>(juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow);
        knob_->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 24);
        knob_->setColour(juce::Slider::textBoxTextColourId, HelixColours::ink());
        knob_->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob_->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        knob_->setColour(juce::Slider::textBoxHighlightColourId, HelixColours::accent().withAlpha(0.3f));
        knob_->getProperties().set("bipolar", d.min < 0.0f && d.max > 0.0f);
        addAndMakeVisible(*knob_);
        knobAtt_ = std::make_unique<juce::SliderParameterAttachment>(*param, *knob_);
    }
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    name_.setBounds(r.removeFromTop(20));
    if (knob_) knob_->setBounds(r);
    if (menu_) menu_->setBounds(r.withSizeKeepingCentre(r.getWidth() - 6, 30));
    if (toggle_) toggle_->setBounds(r.withSizeKeepingCentre(70, 34));
}

void ParamControl::refreshLive()
{
    if (!knob_ || !paramDef(index_).modulatable) return;
    const float v = proc_.engine().liveNormalised(index_);
    if (std::abs(v - live_) > 1e-4f) { live_ = v; repaint(); }
}

void ParamControl::paintOverChildren(juce::Graphics& g)
{
    if (!knob_ || live_ < 0.0f) return;
    const float base = proc_.parameter(index_)->getValue();
    if (std::abs(live_ - base) < 1e-3f) return;
    // Modulation ring: an outer arc from the knob value to the live (modulated) value.
    auto kb = knob_->getBounds().toFloat();
    kb.removeFromBottom(24.0f);
    const auto c = kb.getCentre();
    const float r = juce::jmin(kb.getWidth(), kb.getHeight()) * 0.5f - 1.0f;
    const auto params = knob_->getRotaryParameters();
    const float a0 = params.startAngleRadians + base * (params.endAngleRadians - params.startAngleRadians);
    const float a1 = params.startAngleRadians + live_ * (params.endAngleRadians - params.startAngleRadians);
    juce::Path ring;
    ring.addCentredArc(c.x, c.y, r, r, 0.0f, juce::jmin(a0, a1), juce::jmax(a0, a1), true);
    g.setColour(HelixColours::mod());
    g.strokePath(ring, juce::PathStrokeType(3.0f));
    g.fillEllipse(c.x + r * std::sin(a1) - 3.5f, c.y - r * std::cos(a1) - 3.5f, 7.0f, 7.0f);
}

// ------------------------------------------------------------------------------------------- wavetable view

WavetableView::WavetableView(HelixProcessor& p, int slot) : proc_(p), slot_(slot)
{
    loadButton_.onClick = [this] { chooseFile(false); };
    s2tButton_.onClick = [this] { chooseFile(true); };
    for (int f : {8, 16, 32, 64, 128, 256}) frames_.addItem(juce::String(f) + " frames", f);
    frames_.setSelectedId(64, juce::dontSendNotification);
    addAndMakeVisible(loadButton_);
    addAndMakeVisible(s2tButton_);
    addAndMakeVisible(frames_);
}

void WavetableView::resized()
{
    auto r = getLocalBounds().reduced(10).removeFromTop(30);
    loadButton_.setBounds(r.removeFromRight(130));
    r.removeFromRight(6);
    s2tButton_.setBounds(r.removeFromRight(150));
    r.removeFromRight(6);
    frames_.setBounds(r.removeFromRight(120));
}

void WavetableView::chooseFile(bool s2t)
{
    chooser_ = std::make_unique<juce::FileChooser>(s2t ? "Make a wavetable from a sample" : "Load a wavetable",
                                                   HelixProcessor::userWavetableFolder(), "*.wav");
    const int frames = s2t ? frames_.getSelectedId() : 0;
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, frames](const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc_.library().requestLoadFile(slot_, f.getFullPathName().toStdString(), frames);
                          });
}

void WavetableView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(HelixColours::panel());
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(HelixColours::line());
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);

    auto table = proc_.engine().uiTable(slot_);
    if (!table) return;
    const int nf = table->numFrames();
    const int shown = juce::jmin(nf, 40);
    const float posParam = proc_.engine().liveNormalised(slot_ == 0 ? P_A_POS : P_B_POS);
    const int current = juce::roundToInt(posParam * (float)(nf - 1));
    auto area = r.reduced(16.0f, 14.0f);
    const float depthX = area.getWidth() * 0.18f, depthY = area.getHeight() * 0.35f;
    const float w = area.getWidth() - depthX, h = area.getHeight() - depthY;
    const int points = 128;

    auto drawFrame = [&](int f, float t, juce::Colour col, float thick) {
        const float* s = table->fullFrame(f);
        const float ox = area.getX() + depthX * t, oy = area.getY() + depthY * (1.0f - t);
        juce::Path p;
        for (int i = 0; i <= points; ++i)
        {
            const int idx = (i * kFrameSize / points) & (kFrameSize - 1);
            const float x = ox + w * (float)i / points;
            const float y = oy + h * 0.5f - s[idx] * h * 0.45f;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.setColour(col);
        g.strokePath(p, juce::PathStrokeType(thick));
    };
    for (int k = shown - 1; k >= 0; --k)
    {
        const int f = shown > 1 ? k * (nf - 1) / (shown - 1) : 0;
        drawFrame(f, shown > 1 ? (float)k / (shown - 1) : 0.0f, HelixColours::line(), 1.0f);
    }
    drawFrame(current, nf > 1 ? (float)current / (nf - 1) : 0.0f, HelixColours::accent(), 2.5f);
    g.setColour(HelixColours::ink());
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText(juce::String(table->name()) + "   frame " + juce::String(current + 1) + " / " + juce::String(nf),
               r.reduced(12.0f, 6.0f), juce::Justification::topLeft);
}

// ------------------------------------------------------------------------------------------- presets

PresetBrowser::PresetBrowser(HelixProcessor& p) : proc_(p)
{
    list_.setRowHeight(30);
    list_.setColour(juce::ListBox::backgroundColourId, HelixColours::panel());
    list_.setColour(juce::ListBox::outlineColourId, HelixColours::line());
    list_.setOutlineThickness(1);
    addAndMakeVisible(list_);
    for (auto* b : {&loadB_, &saveB_, &initB_, &mutateB_, &prevB_, &nextB_}) addAndMakeVisible(b);
    current_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    current_.setColour(juce::Label::textColourId, HelixColours::ink());
    addAndMakeVisible(current_);
    loadB_.onClick = [this] { load(list_.getSelectedRow()); };
    saveB_.onClick = [this] { save(); };
    initB_.onClick = [this] { helix::Preset init; proc_.loadPreset(init); refresh(); };
    mutateB_.onClick = [this] {
        helix::Preset m = proc_.currentPreset();
        helix::mutatePreset(m, 0.1f, mutateSeed_++);
        m.name = proc_.presetName().toStdString();
        if (m.name.find(" (mutated)") == std::string::npos) m.name += " (mutated)";
        proc_.loadPreset(m);
        refresh();
    };
    prevB_.onClick = [this] { const int r = juce::jmax(0, list_.getSelectedRow() - 1); list_.selectRow(r); load(r); };
    nextB_.onClick = [this] { const int r = juce::jmin((int)items_.size() - 1, list_.getSelectedRow() + 1); list_.selectRow(r); load(r); };
    refresh();
}

void PresetBrowser::refresh()
{
    items_.clear();
    const auto& f = helix::factoryPresets();
    for (int i = 0; i < (int)f.size(); ++i) items_.push_back({f[(size_t)i].category, f[(size_t)i].name, {}, i});
    for (const auto& file : HelixProcessor::userPresetFolder().findChildFiles(juce::File::findFiles, true, "*.json"))
        items_.push_back({"User / " + file.getParentDirectory().getFileName(), file.getFileNameWithoutExtension(), file, -1});
    list_.updateContent();
    current_.setText(proc_.presetName(), juce::dontSendNotification);
    repaint();
}

void PresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int)items_.size()) return;
    if (selected) g.fillAll(HelixColours::ink());
    g.setColour(selected ? HelixColours::bg() : HelixColours::muted());
    g.setFont(juce::FontOptions(15.0f));
    g.drawText(items_[(size_t)row].category, 10, 0, 140, h, juce::Justification::centredLeft);
    g.setColour(selected ? HelixColours::bg() : HelixColours::ink());
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText(items_[(size_t)row].name, 160, 0, w - 170, h, juce::Justification::centredLeft);
}

void PresetBrowser::load(int row)
{
    if (row < 0 || row >= (int)items_.size()) return;
    const Item& it = items_[(size_t)row];
    if (it.factory >= 0) proc_.setCurrentProgram(it.factory);
    else
    {
        helix::Preset p; std::string err;
        if (helix::Preset::fromString(it.file.loadFileAsString().toStdString(), p, &err)) { if (p.name.empty()) p.name = it.name.toStdString(); proc_.loadPreset(p); }
        else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "HELIX", "Could not load the preset: " + juce::String(err));
    }
    current_.setText(proc_.presetName(), juce::dontSendNotification);
}

void PresetBrowser::save()
{
    saveDialog_ = std::make_unique<juce::AlertWindow>("Save preset", "Name and category", juce::MessageBoxIconType::NoIcon);
    saveDialog_->addTextEditor("name", proc_.presetName(), "Name");
    saveDialog_->addComboBox("category", {"Bass", "Lead", "Pad", "Atmos", "Stab", "FX", "Misc"}, "Category");
    saveDialog_->addButton("Save", 1);
    saveDialog_->addButton("Cancel", 0);
    saveDialog_->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
        if (result == 1 && saveDialog_)
        {
            auto name = saveDialog_->getTextEditorContents("name").trim();
            auto* cat = saveDialog_->getComboBoxComponent("category");
            const auto category = cat ? cat->getText() : juce::String("Misc");
            if (name.isNotEmpty())
            {
                helix::Preset p = proc_.currentPreset();
                p.name = name.toStdString();
                p.category = category.toStdString();
                const auto dir = HelixProcessor::userPresetFolder().getChildFile(category);
                dir.createDirectory();
                dir.getChildFile(juce::File::createLegalFileName(name) + ".json").replaceWithText(p.toString());
                refresh();
            }
        }
        saveDialog_.reset();
    }), false);
}

void PresetBrowser::paint(juce::Graphics& g)
{
    g.setColour(HelixColours::muted());
    g.setFont(juce::FontOptions(15.0f));
    g.drawText("PRESET", 0, 0, 200, 20, juce::Justification::left);
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop(56);
    head.removeFromTop(20);
    current_.setBounds(head.removeFromLeft(head.getWidth() / 2));
    for (auto* b : {&mutateB_, &initB_, &saveB_, &loadB_, &nextB_, &prevB_}) { b->setBounds(head.removeFromRight(b == &prevB_ || b == &nextB_ ? 44 : 110).reduced(3)); }
    r.removeFromTop(6);
    list_.setBounds(r);
}

// ------------------------------------------------------------------------------------------- editor

namespace {
class SectionBox : public juce::Component
{
public:
    explicit SectionBox(juce::String title) : title_(std::move(title)) {}
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(4.0f);
        g.setColour(HelixColours::panel());
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(HelixColours::line());
        g.drawRoundedRectangle(r, 8.0f, 1.0f);
        g.setColour(HelixColours::ink());
        g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        g.drawText(title_.toUpperCase(), r.reduced(10.0f, 4.0f).removeFromTop(20.0f), juce::Justification::left);
    }
private:
    juce::String title_;
};
}

HelixEditor::HelixEditor(HelixProcessor& p) : AudioProcessorEditor(&p), proc_(p)
{
    setLookAndFeel(&lnf_);
    const auto& pages = uiPages();
    for (int i = 0; i < (int)pages.size(); ++i)
    {
        auto* b = tabs_.add(new juce::TextButton(pages[(size_t)i].name));
        b->setClickingTogglesState(false);
        b->onClick = [this, i] { showPage(i); };
        addAndMakeVisible(b);
    }
    cpu_.setJustificationType(juce::Justification::centredRight);
    cpu_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    addAndMakeVisible(cpu_);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    setResizable(true, true);
    setResizeLimits(900, 560, 2400, 1600);
    setSize(1180, 760);
    // HELIX_START_PAGE=<n> opens another page first (screenshots and UI checks).
    const char* start = std::getenv("HELIX_START_PAGE");
    showPage(start ? juce::jlimit(0, (int)pages.size() - 1, std::atoi(start)) : 0);
    startTimerHz(30);
}

HelixEditor::~HelixEditor()
{
    stopTimer();
    viewport_.setViewedComponent(nullptr, false);
    controls_.clear();
    owned_.clear();
    wtView_.reset();
    browser_.reset();
    page_.reset();
    setLookAndFeel(nullptr);
}

void HelixEditor::showPage(int index)
{
    pageIndex_ = index;
    for (int i = 0; i < tabs_.size(); ++i) tabs_[i]->setToggleState(i == index, juce::dontSendNotification);
    viewport_.setViewedComponent(nullptr, false);
    controls_.clear();
    owned_.clear();     // children first, then their parent page
    wtView_.reset();
    browser_.reset();
    page_ = std::make_unique<juce::Component>();

    const auto& pg = uiPages()[(size_t)index];
    const int width = juce::jmax(860, viewport_.getWidth() - 12);
    const int cols = width >= 1100 ? 3 : 2;
    const int boxW = width / cols, boxH = 150;
    int y = 0;
    if (pg.name == "OSC A" || pg.name == "OSC B")
    {
        wtView_ = std::make_unique<WavetableView>(proc_, pg.name == "OSC A" ? 0 : 1);
        wtView_->setBounds(4, 4, width - 8, 220);
        page_->addAndMakeVisible(*wtView_);
        y = 232;
    }
    if (pg.name == "BROWSER")
    {
        browser_ = std::make_unique<PresetBrowser>(proc_);
        browser_->setBounds(4, 4, width - 8, 360);
        page_->addAndMakeVisible(*browser_);
        y = 372;
    }
    for (size_t s = 0; s < pg.sections.size(); ++s)
    {
        const auto& sec = pg.sections[s];
        auto* box = new SectionBox(sec.title);
        owned_.emplace_back(box);
        const int col = (int)(s % (size_t)cols), row = (int)(s / (size_t)cols);
        box->setBounds(col * boxW, y + row * boxH, boxW, boxH);
        page_->addAndMakeVisible(box);
        const int cw = (boxW - 24) / 4;
        for (size_t k = 0; k < sec.params.size(); ++k)
        {
            if (sec.params[k] < 0) continue;
            auto* c = new ParamControl(proc_, sec.params[k]);
            owned_.emplace(owned_.begin(), c);   // destroyed before its box
            c->setBounds(12 + (int)k * cw, 26, cw, boxH - 34);
            box->addAndMakeVisible(c);
            controls_.push_back(c);
        }
    }
    const int rows = ((int)pg.sections.size() + cols - 1) / cols;
    page_->setSize(width, y + rows * boxH + 8);
    viewport_.setViewedComponent(page_.get(), false);
}

void HelixEditor::paint(juce::Graphics& g)
{
    g.fillAll(HelixColours::bg());
    g.setColour(HelixColours::ink());
    g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    g.drawText("HELIX", 16, 8, 140, 36, juce::Justification::left);
}

void HelixEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop(52);
    top.removeFromLeft(140);
    cpu_.setBounds(top.removeFromRight(190).reduced(8));
    const int tw = top.getWidth() / juce::jmax(1, tabs_.size());
    for (auto* t : tabs_) t->setBounds(top.removeFromLeft(tw).reduced(2, 8));
    viewport_.setBounds(r.reduced(8));
    if (page_) showPage(pageIndex_);
}

void HelixEditor::timerCallback()
{
    for (auto* c : controls_) c->refreshLive();
    if (wtView_) wtView_->repaint();
    juce::String cpu = "CPU " + juce::String(juce::roundToInt(proc_.engine().cpuLoad() * 100.0f)) + "%";
    if (proc_.engine().heavyFxActive()) cpu += " OTT";
    if (proc_.engine().guardLevel() > 0) cpu += " ECO" + juce::String(proc_.engine().guardLevel());
    cpu_.setText(cpu, juce::dontSendNotification);
}
