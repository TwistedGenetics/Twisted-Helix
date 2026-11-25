#include "PluginEditor.h"
#include "helix/FxLibrary.h"
#include "helix/ModSystem.h"

using namespace helix;

namespace {

int oscOff(int slot) { return slot == 0 ? 0 : P_B_ON - P_A_ON; }
int filtOff(int f) { return f == 0 ? 0 : P_F2_ON - P_F1_ON; }
// Print to wavetable settings, shared by both oscillators' menus for the session
helix::PrintOptions& printOptions() { static helix::PrintOptions o; return o; }

// --------------------------------------------------------------------------------------------- oscillator panel

class OscPanel : public PanelFrame
{
public:
    OscPanel(Page& pg, int slot) : PanelFrame(slot == 0 ? "Osc A" : "Osc B"), page_(pg), slot_(slot)
    {
        const int o = oscOff(slot);
        setPower(std::make_unique<ParamToggle>(pg.proc, P_A_ON + o));
        view_ = pg.own(std::make_unique<WavetableView>(pg.proc, slot), this);
        addAndMakeVisible(table_);
        table_.setTooltip("Wavetable: the 8 built-in tables, then your Wavetables folder");
        table_.onChange = [this] {
            const int id = table_.getSelectedId();
            if (id > 0) page_.proc.setParamFromUI(slot_ == 0 ? P_A_TABLE : P_B_TABLE, (float)(id - 1));
        };
        for (auto* b : {&prevT_, &nextT_, &more_}) addAndMakeVisible(b);
        prevT_.onClick = [this] { step(-1); };
        nextT_.onClick = [this] { step(1); };
        more_.onClick = [this] { moreMenu(); };
        more_.setTooltip("Load a WAV as a wavetable, or make one from a sample");
        oct_ = pg.knob(P_A_OCT + o, this, "Octave");
        semi_ = pg.knob(P_A_SEMI + o, this, "Semi");
        fine_ = pg.knob(P_A_FINE + o, this, "Fine");
        pos_ = pg.knob(P_A_POS + o, this, "WT Pos");
        level_ = pg.knob(P_A_LEVEL + o, this, "Level");
        pan_ = pg.knob(P_A_PAN + o, this, "Pan");
        uni_ = pg.knob(P_A_UNISON + o, this, "Unison");
        det_ = pg.knob(P_A_DETUNE + o, this, "Detune");
        blend_ = pg.knob(P_A_BLEND + o, this, "Blend");
        spread_ = pg.knob(P_A_SPREAD + o, this, "Spread");
        phase_ = pg.knob(P_A_PHASE + o, this, "Phase");
        rand_ = pg.knob(P_A_RAND + o, this, "Rnd Phase");
        w1_ = pg.combo(P_A_W1_MODE + o, this, "Warp 1");
        w1a_ = pg.knob(P_A_W1_AMT + o, this, "Amount");
        w2_ = pg.combo(P_A_W2_MODE + o, this, "Warp 2");
        w2a_ = pg.knob(P_A_W2_AMT + o, this, "Amount");
    }

    void resized() override
    {
        PanelFrame::resized();
        auto title = titleArea();
        more_.setBounds(title.removeFromRight(24).reduced(0, 1));
        nextT_.setBounds(title.removeFromRight(22).reduced(0, 1));
        table_.setBounds(title.removeFromRight(150).reduced(2, 0));
        prevT_.setBounds(title.removeFromRight(22).reduced(0, 1));
        auto c = content();
        view_->setBounds(c.removeFromTop(170));
        c.removeFromTop(6);
        const int rh = (c.getHeight() - 4) / 3;
        layoutRow(c.removeFromTop(rh), {oct_, semi_, fine_, pos_, level_, pan_});
        layoutRow(c.removeFromTop(rh), {uni_, det_, blend_, spread_, phase_, rand_});
        auto r3 = c.removeFromTop(rh);
        const int cw = r3.getWidth() / 6;
        w1_->setBounds(r3.removeFromLeft(cw * 2 - 8).withTrimmedTop(8).withHeight(40).translated(0, 6));
        w1a_->setBounds(r3.removeFromLeft(cw));
        w2_->setBounds(r3.removeFromLeft(cw * 2 - 8).withTrimmedTop(8).withHeight(40).translated(0, 6));
        w2a_->setBounds(r3.removeFromLeft(cw));
    }

    void tick()
    {
        view_->refresh();
        auto& lib = page_.proc.library();
        const int n = lib.count();
        if (n != items_)
        {
            items_ = n;
            table_.clear(juce::dontSendNotification);
            for (int i = 0; i < n; ++i) table_.addItem(lib.nameAt(i), i + 1);
        }
        const int sel = lib.wrap(page_.proc.engine().params().getInt(slot_ == 0 ? P_A_TABLE : P_B_TABLE)) + 1;
        if (table_.getSelectedId() != sel) table_.setSelectedId(sel, juce::dontSendNotification);
    }

private:
    void step(int d)
    {
        const int n = juce::jmax(1, page_.proc.library().count());
        const int cur = page_.proc.library().wrap(page_.proc.engine().params().getInt(slot_ == 0 ? P_A_TABLE : P_B_TABLE));
        page_.proc.setParamFromUI(slot_ == 0 ? P_A_TABLE : P_B_TABLE, (float)((cur + d + n) % n));
    }
    void moreMenu()
    {
        juce::PopupMenu m, s2t;
        m.addItem(1, "Load WAV as wavetable...");
        for (int f : {8, 16, 32, 64, 128, 256}) s2t.addItem(100 + f, juce::String(f) + " frames");
        m.addSubMenu("Sample > Table...", s2t);
        // Print to wavetable: render this patch (sweeping one control) into a new table on this oscillator
        {
            auto& po = printOptions();
            juce::PopupMenu pr, frames, len, note;
            const bool busy = page_.proc.printing();
            for (int src = 0; src < helix::kNumPrintSources; ++src)
                pr.addItem(1000 + src, "Print, sweeping " + juce::String(helix::choices::printSource[(size_t)src]), !busy);
            pr.addSeparator();
            for (int f : {16, 32, 64, 128, 256}) frames.addItem(2000 + f, juce::String(f) + " frames", true, po.frames == f);
            pr.addSubMenu("Frames", frames);
            for (int t : {5, 10, 20, 40, 80}) len.addItem(3000 + t, juce::String(t / 10.0, 1) + " s", true, std::lround(po.seconds * 10) == t);
            pr.addSubMenu("Length", len);
            static const char* nn[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            for (int n = 24; n <= 42; ++n) note.addItem(4000 + n, juce::String(nn[n % 12]) + juce::String(n / 12 - 1), true, po.note == n);
            pr.addSubMenu("Note", note);
            pr.addItem(5000, "Through the effects", true, po.effects);
            const auto st = page_.proc.printStatus();
            if (st.isNotEmpty()) { pr.addSeparator(); pr.addItem(5001, "Last: " + st, false); }
            m.addSubMenu("Print patch to table", pr);
        }
        m.addSeparator();
        m.addItem(2, "Open the Wavetables folder");
        m.addItem(3, "Rescan the Wavetables folder");
        juce::Component::SafePointer<OscPanel> self(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&more_), [self](int r) {
            if (!self || r == 0) return;
            if (r == 2) { HelixProcessor::userWavetableFolder().startAsProcess(); return; }
            if (r == 3) { self->page_.proc.library().rescan(); return; }
            auto& po = printOptions();
            if (r >= 1000 && r < 1000 + helix::kNumPrintSources) { po.source = r - 1000; self->page_.proc.startPrint(self->slot_, po); return; }
            if (r >= 2000 && r < 2300) { po.frames = r - 2000; return; }
            if (r >= 3000 && r < 3100) { po.seconds = (float)(r - 3000) / 10.0f; return; }
            if (r >= 4000 && r < 4100) { po.note = r - 4000; return; }
            if (r == 5000) { po.effects = !po.effects; return; }
            const int frames = r >= 100 ? r - 100 : 0;
            self->chooser_ = std::make_unique<juce::FileChooser>(frames ? "Make a wavetable from a sample" : "Load a wavetable",
                                                                 HelixProcessor::userWavetableFolder(), "*.wav");
            self->chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [self, frames](const juce::FileChooser& fc) {
                                            if (!self) return;
                                            const auto f = fc.getResult();
                                            if (f.existsAsFile())
                                                self->page_.proc.library().requestLoadFile(self->slot_, f.getFullPathName().toStdString(), frames);
                                        });
        });
    }

    Page& page_;
    int slot_;
    WavetableView* view_;
    juce::ComboBox table_;
    juce::TextButton prevT_{"<"}, nextT_{">"}, more_{"..."};
    std::unique_ptr<juce::FileChooser> chooser_;
    int items_ = -1;
    ParamKnob *oct_, *semi_, *fine_, *pos_, *level_, *pan_, *uni_, *det_, *blend_, *spread_, *phase_, *rand_, *w1a_, *w2a_;
    ParamCombo *w1_, *w2_;
};

// --------------------------------------------------------------------------------------------- filter panel

class FilterPanel : public PanelFrame
{
public:
    FilterPanel(Page& pg, int f) : PanelFrame(f == 0 ? "Filter 1" : "Filter 2")
    {
        const int o = filtOff(f);
        setPower(std::make_unique<ParamToggle>(pg.proc, P_F1_ON + o));
        type_ = pg.combo(P_F1_TYPE + o, this);
        const int mo = f * (P_F2_MODEL - P_F1_MODEL);
        model_ = pg.combo(P_F1_MODEL + mo, this);
        kind_ = pg.combo(P_F1_KIND + mo, this);
        slope_ = pg.combo(P_F1_SLOPE + mo, this);
        model_->box().setTooltip("Model: HELIX = the original filter (type in the title bar); the others are the Mega Synth models");
        kind_->box().setTooltip("Filter type for the Mega Synth models");
        slope_->box().setTooltip("Slope for the Mega Synth models (Model = the model's own slope)");
        view_ = pg.own(std::make_unique<FilterResponseView>(pg.proc, f), this);
        cut_ = pg.knob(P_F1_CUTOFF + o, this, "Cutoff", ParamKnob::Size::Large);
        res_ = pg.knob(P_F1_RES + o, this, "Reso");
        drive_ = pg.knob(P_F1_DRIVE + o, this, "Drive");
        key_ = pg.knob(P_F1_KEYTRK + o, this, "Key Trk");
        fat_ = pg.knob(P_F1_FAT + o, this, "Fat");
        level_ = pg.knob(P_F1_LEVEL + o, this, "Level");
    }
    void resized() override
    {
        PanelFrame::resized();
        type_->setBounds(titleArea().removeFromRight(130));
        auto c = content();
        view_->setBounds(c.removeFromLeft(168));
        c.removeFromLeft(6);
        auto models = c.removeFromTop(26);
        const int w3 = models.getWidth() / 3;
        model_->setBounds(models.removeFromLeft(w3 + 20).reduced(2, 0));
        kind_->setBounds(models.removeFromLeft(w3 - 4).reduced(2, 0));
        slope_->setBounds(models.reduced(2, 0));
        c.removeFromTop(2);
        auto top = c.removeFromTop(c.getHeight() / 2);
        layoutRow(top, {cut_, res_, drive_});
        layoutRow(c, {key_, fat_, level_});
    }
    void tick() { view_->refresh(); }
private:
    ParamCombo *type_, *model_, *kind_, *slope_;
    FilterResponseView* view_;
    ParamKnob *cut_, *res_, *drive_, *key_, *fat_, *level_;
};

// --------------------------------------------------------------------------------------------- sub / noise / routing

class SubPanel : public PanelFrame
{
public:
    explicit SubPanel(Page& pg) : PanelFrame("Sub")
    {
        setPower(std::make_unique<ParamToggle>(pg.proc, P_SUB_ON));
        shape_ = pg.combo(P_SUB_SHAPE, this, "Shape");
        oct_ = pg.combo(P_SUB_OCT, this, "Octave");
        direct_ = pg.toggle(P_SUB_DIRECT, this, "Direct");
        direct_->button().setTooltip("Direct out: the sub skips both filters (clean low end)");
        level_ = pg.knob(P_SUB_LEVEL, this, "Level");
        pan_ = pg.knob(P_SUB_PAN, this, "Pan");
    }
    void resized() override
    {
        PanelFrame::resized();
        auto c = content();
        shape_->setBounds(c.removeFromLeft(100).withSizeKeepingCentre(96, 40));
        c.removeFromLeft(4);
        oct_->setBounds(c.removeFromLeft(84).withSizeKeepingCentre(80, 40));
        c.removeFromLeft(8);
        direct_->setBounds(c.removeFromLeft(74).withSizeKeepingCentre(72, 22));
        layoutRow(c, {level_, pan_});
    }
private:
    ParamCombo *shape_, *oct_;
    ParamToggle* direct_;
    ParamKnob *level_, *pan_;
};

class NoisePanel : public PanelFrame
{
public:
    explicit NoisePanel(Page& pg) : PanelFrame("Noise")
    {
        setPower(std::make_unique<ParamToggle>(pg.proc, P_NOISE_ON));
        type_ = pg.combo(P_NOISE_TYPE, this, "Texture");
        track_ = pg.toggle(P_NOISE_PITCH, this, "Track");
        loop_ = pg.toggle(P_NOISE_LOOP, this, "Loop");
        level_ = pg.knob(P_NOISE_LEVEL, this, "Level");
        pan_ = pg.knob(P_NOISE_PAN, this, "Pan");
    }
    void resized() override
    {
        PanelFrame::resized();
        auto c = content();
        type_->setBounds(c.removeFromLeft(118).withSizeKeepingCentre(114, 40));
        c.removeFromLeft(8);
        auto sw = c.removeFromLeft(70);
        track_->setBounds(sw.removeFromTop(sw.getHeight() / 2).withSizeKeepingCentre(68, 22));
        loop_->setBounds(sw.withSizeKeepingCentre(68, 22));
        layoutRow(c, {level_, pan_});
    }
private:
    ParamCombo* type_;
    ParamToggle *track_, *loop_;
    ParamKnob *level_, *pan_;
};

class RoutingPanel : public PanelFrame
{
public:
    explicit RoutingPanel(Page& pg) : PanelFrame("Routing")
    {
        const char* names[] = {"A", "B", "Sub", "Nse"};
        const int f1[] = {P_A_TO_F1, P_B_TO_F1, P_SUB_TO_F1, P_NOISE_TO_F1};
        const int f2[] = {P_A_TO_F2, P_B_TO_F2, P_SUB_TO_F2, P_NOISE_TO_F2};
        for (int i = 0; i < 4; ++i)
        {
            sends_.push_back(pg.knob(f1[i], this, juce::String(names[i]) + ">1", ParamKnob::Size::Small));
            sends_.push_back(pg.knob(f2[i], this, juce::String(names[i]) + ">2", ParamKnob::Size::Small));
        }
        blend_ = pg.knob(P_FILTER_BLEND, this, "Par / Ser");
        blend_->setTooltip("0% = parallel (F1 + F2), 100% = serial (F1 into F2)");
    }
    void resized() override
    {
        PanelFrame::resized();
        auto c = content();
        blend_->setBounds(c.removeFromRight(86));
        c.removeFromRight(6);
        const int w = c.getWidth() / 8;
        for (int i = 0; i < 8; ++i) sends_[(size_t)i]->setBounds(c.getX() + i * w, c.getY(), w, c.getHeight());
    }
private:
    std::vector<ParamKnob*> sends_;
    ParamKnob* blend_;
};

// --------------------------------------------------------------------------------------------- modulation

class ModPanel : public PanelFrame
{
public:
    explicit ModPanel(Page& pg) : PanelFrame(""), page_(pg)
    {
        const char* main[] = {"ENV 1", "ENV 2", "ENV 3", "ENV 4", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "PERF 1", "PERF 2"};
        for (int i = 0; i < 10; ++i)
        {
            auto* chip = pg.own(std::make_unique<ModChip>(SRC_ENV1 + i, main[i]), this);
            chip->onClick = [this, i] { select(i); };
            chips_.push_back(chip);
        }
        const std::pair<int, const char*> extra[] = {{SRC_VELOCITY, "VEL"}, {SRC_PRESSURE, "PRS"}, {SRC_CHAN_AT, "AT"},
                                                      {SRC_MODWHEEL, "MW"}, {SRC_NOTE, "NOTE"}, {SRC_RANDOM, "RND"}};
        for (auto& e : extra)
        {
            auto* chip = pg.own(std::make_unique<ModChip>(e.first, e.second), this);
            extras_.push_back(chip);
        }
        // one page of controls per source
        for (int e = 0; e < 4; ++e)
        {
            auto* holder = pg.own(std::make_unique<juce::Component>(), this);
            const int b = P_E1_A + e * (P_E2_A - P_E1_A);
            Pg p;
            p.holder = holder;
            p.view = pg.own(std::make_unique<EnvelopeView>(pg.proc, e), holder);
            for (int k = 0; k < 7; ++k)
                p.row[k < 4 ? 0 : 1].push_back(pg.knob(b + k, holder, juce::StringArray{"Attack", "Decay", "Sustain", "Release", "A Curve", "D Curve", "R Curve"}[k]));
            pages_.push_back(p);
        }
        for (int l = 0; l < 4; ++l)
        {
            auto* holder = pg.own(std::make_unique<juce::Component>(), this);
            const int b = P_L1_SHAPE + l * (P_L2_SHAPE - P_L1_SHAPE);
            Pg p;
            p.holder = holder;
            p.view = pg.own(std::make_unique<LfoView>(pg.proc, l), holder);
            p.combos = {pg.combo(b, holder, "Shape"), pg.combo(b + 1, holder, "Mode")};
            p.syncToggle = pg.toggle(b + 2, holder, "Sync");
            p.rate = pg.knob(b + 3, holder, "Rate");
            p.div = pg.combo(b + 4, holder, "Division");
            p.syncParam = b + 2;
            p.row[1] = {pg.knob(b + 5, holder, "Rise"), pg.knob(b + 6, holder, "Smooth"), pg.knob(b + 7, holder, "Phase"), pg.knob(b + 8, holder, "Steps")};
            pages_.push_back(p);
        }
        for (int q = 0; q < 2; ++q)
        {
            auto* holder = pg.own(std::make_unique<juce::Component>(), this);
            const int b = q == 0 ? P_P1_DIV : P_P2_DIV;
            Pg p;
            p.holder = holder;
            auto* pv = pg.own(std::make_unique<PerformerView>(pg.proc, q), holder);
            p.view = pv;
            p.combos = {pg.combo(b, holder, "Step"), pg.combo(b + 2, holder, "Mode")};
            p.row[1] = {pg.knob(b + 1, holder, "Length")};
            // Massive-style curve library: drag a curve onto a step, or pick one and click a step's lane
            p.lib = pg.own(std::make_unique<CurveLibrary>(), holder);
            p.lib->setTooltip("Curve library: drag a curve onto a step, or click one and then click the steps' lane "
                              "under the display. Drag in the display to set each step's level.");
            pv->setLibrary(p.lib);
            pages_.push_back(p);
        }
        // Modulation oscillator (Massive-style): page 10, chosen with its own button
        {
            auto* holder = pg.own(std::make_unique<juce::Component>(), this);
            Pg p;
            p.holder = holder;
            p.view = pg.own(std::make_unique<juce::Component>(), holder);
            moOn_ = pg.toggle(P_MO_ON, p.view, "Mod Osc");
            moOn_->button().setTooltip("Modulation oscillator: a sine at the note's pitch + Pitch");
            moPitch_ = pg.knob(P_MO_PITCH, p.view, "Pitch", ParamKnob::Size::Large);
            p.combos = {pg.combo(P_MO_RM_TGT, holder, "Ring Mod"), pg.combo(P_MO_PM_TGT, holder, "Phase"),
                        pg.combo(P_MO_POS_TGT, holder, "Position"), pg.combo(P_MO_FM_TGT, holder, "Filter FM")};
            p.row[1] = {pg.knob(P_MO_RM_AMT, holder, "RM Amt"), pg.knob(P_MO_PM_AMT, holder, "Phase Amt"),
                        pg.knob(P_MO_POS_AMT, holder, "Pos Amt"), pg.knob(P_MO_FM_AMT, holder, "FM Amt")};
            pages_.push_back(p);
            moBtn_ = pg.own(std::make_unique<juce::TextButton>("MOD OSC"), this);
            moBtn_->setClickingTogglesState(false);
            moBtn_->onClick = [this] { select(kMoPage); };
        }
        select(0);
    }

    void select(int i)
    {
        current_ = i;
        for (int k = 0; k < (int)pages_.size(); ++k) pages_[(size_t)k].holder->setVisible(k == i);
        for (int k = 0; k < (int)chips_.size(); ++k) chips_[(size_t)k]->setSelected(k == i);
        if (moBtn_) moBtn_->setToggleState(i == kMoPage, juce::dontSendNotification);
        resized();
    }

    void resized() override
    {
        auto c = getLocalBounds().reduced(8, 6);
        auto top = c.removeFromTop(24);
        if (moBtn_) { moBtn_->setBounds(top.removeFromRight(68)); top.removeFromRight(4); }
        for (auto* ch : chips_) { ch->setBounds(top.removeFromLeft(46)); top.removeFromLeft(2); }
        top.removeFromLeft(4);
        for (auto* ch : extras_) { ch->setBounds(top.removeFromLeft(32)); top.removeFromLeft(2); }
        c.removeFromTop(6);
        for (int pi = 0; pi < (int)pages_.size(); ++pi)
        {
            auto& p = pages_[(size_t)pi];
            p.holder->setBounds(c);
            auto r = p.holder->getLocalBounds();
            if (pi == kMoPage)
            {
                // left: on / off and Pitch; right: one column per route (target menu over its amount)
                auto left = r.removeFromLeft(180);
                p.view->setBounds(left);
                auto lv = p.view->getLocalBounds();
                moOn_->setBounds(lv.removeFromTop(30).withSizeKeepingCentre(110, 24));
                moPitch_->setBounds(lv);
                r.removeFromLeft(8);
                const int w = r.getWidth() / 4;
                for (int k = 0; k < 4; ++k)
                {
                    auto col = juce::Rectangle<int>(r.getX() + k * w, r.getY(), w, r.getHeight()).reduced(3, 0);
                    p.combos[(size_t)k]->setBounds(col.removeFromTop(44));
                    p.row[1][(size_t)k]->setBounds(col);
                }
                continue;
            }
            p.view->setBounds(r.removeFromLeft(340));
            r.removeFromLeft(8);
            auto r1 = r.removeFromTop(r.getHeight() / 2);
            if (!p.combos.empty())
            {
                auto cr = r1;
                for (auto* cb : p.combos) { cb->setBounds(cr.removeFromLeft(104).withSizeKeepingCentre(100, 40)); cr.removeFromLeft(4); }
                if (p.syncToggle) { p.syncToggle->setBounds(cr.removeFromLeft(62).withSizeKeepingCentre(60, 22)); cr.removeFromLeft(4); }
                if (p.rate) p.rate->setBounds(cr.removeFromLeft(80));
                if (p.div) p.div->setBounds(p.rate ? p.rate->getBounds().withSizeKeepingCentre(78, 40) : cr.removeFromLeft(80));
            }
            else layoutRow(r1, {p.row[0].size() > 0 ? p.row[0][0] : nullptr, p.row[0].size() > 1 ? p.row[0][1] : nullptr,
                                p.row[0].size() > 2 ? p.row[0][2] : nullptr, p.row[0].size() > 3 ? p.row[0][3] : nullptr});
            if (p.lib)
            {
                p.row[1][0]->setBounds(r.removeFromLeft(80));
                r.removeFromLeft(6);
                p.lib->setBounds(r.reduced(0, 4));
                continue;
            }
            const int n = (int)p.row[1].size();
            if (n)
            {
                const int w = r.getWidth() / 4;
                for (int k = 0; k < n; ++k) p.row[1][(size_t)k]->setBounds(r.getX() + k * w, r.getY(), w, r.getHeight());
            }
        }
    }

    void tick()
    {
        auto& p = pages_[(size_t)current_];
        if (auto* e = dynamic_cast<EnvelopeView*>(p.view)) e->refresh();
        if (auto* l = dynamic_cast<LfoView*>(p.view)) l->refresh();
        if (auto* q = dynamic_cast<PerformerView*>(p.view)) q->refresh();
        if (p.syncParam >= 0)
        {
            const bool sync = page_.proc.engine().params().getBool(p.syncParam);
            p.rate->setVisible(!sync);
            p.div->setVisible(sync);
        }
    }

private:
    struct Pg
    {
        juce::Component* holder = nullptr;
        juce::Component* view = nullptr;
        std::vector<ParamKnob*> row[2];
        std::vector<ParamCombo*> combos;
        ParamToggle* syncToggle = nullptr;
        ParamKnob* rate = nullptr;
        ParamCombo* div = nullptr;
        CurveLibrary* lib = nullptr;
        int syncParam = -1;
    };
    static constexpr int kMoPage = 10;
    Page& page_;
    std::vector<ModChip*> chips_, extras_;
    std::vector<Pg> pages_;
    int current_ = 0;
    juce::TextButton* moBtn_ = nullptr;
    ParamToggle* moOn_ = nullptr;
    ParamKnob* moPitch_ = nullptr;
};

class MacroPanel : public PanelFrame
{
public:
    explicit MacroPanel(Page& pg) : PanelFrame("Macros")
    {
        for (int m = 0; m < kNumMacros; ++m)
        {
            chips_.push_back(pg.own(std::make_unique<ModChip>(SRC_MACRO1 + m, "M" + juce::String(m + 1)), this));
            knobs_.push_back(pg.knob(P_MACRO1 + m, this, "Macro " + juce::String(m + 1)));
        }
    }
    void resized() override
    {
        PanelFrame::resized();
        auto c = content();
        const int w = c.getWidth() / 4, h = c.getHeight() / 2;
        for (int m = 0; m < kNumMacros; ++m)
        {
            auto cell = juce::Rectangle<int>(c.getX() + (m % 4) * w, c.getY() + (m / 4) * h, w, h).reduced(3, 1);
            chips_[(size_t)m]->setBounds(cell.removeFromTop(18).withSizeKeepingCentre(46, 18));
            knobs_[(size_t)m]->setBounds(cell);
        }
    }
private:
    std::vector<ModChip*> chips_;
    std::vector<ParamKnob*> knobs_;
};

// --------------------------------------------------------------------------------------------- OSC tab

class OscTab : public Page
{
public:
    explicit OscTab(HelixProcessor& p) : Page(p)
    {
        oscA_ = own(std::make_unique<OscPanel>(*this, 0));
        oscB_ = own(std::make_unique<OscPanel>(*this, 1));
        f1_ = own(std::make_unique<FilterPanel>(*this, 0));
        f2_ = own(std::make_unique<FilterPanel>(*this, 1));
        sub_ = own(std::make_unique<SubPanel>(*this));
        noise_ = own(std::make_unique<NoisePanel>(*this));
        route_ = own(std::make_unique<RoutingPanel>(*this));
        mod_ = own(std::make_unique<ModPanel>(*this));
        macros_ = own(std::make_unique<MacroPanel>(*this));
    }
    void resized() override
    {
        oscA_->setBounds(0, 0, 384, 400);
        oscB_->setBounds(392, 0, 384, 400);
        f1_->setBounds(784, 0, 400, 198);
        f2_->setBounds(784, 202, 400, 198);
        sub_->setBounds(0, 406, 384, 96);
        noise_->setBounds(392, 406, 384, 96);
        route_->setBounds(784, 406, 400, 96);
        mod_->setBounds(0, 508, 776, 200);
        macros_->setBounds(784, 508, 400, 200);
    }
    void tick() override
    {
        Page::tick();
        oscA_->tick(); oscB_->tick(); f1_->tick(); f2_->tick(); mod_->tick();
    }
private:
    OscPanel *oscA_, *oscB_;
    FilterPanel *f1_, *f2_;
    SubPanel* sub_;
    NoisePanel* noise_;
    RoutingPanel* route_;
    ModPanel* mod_;
    MacroPanel* macros_;
};

// --------------------------------------------------------------------------------------------- FX tab

struct FxDef { const char* name; int on; std::vector<int> combos; std::vector<int> knobs; int syncToggle = -1; };

const std::vector<FxDef>& fxDefs()
{
    static const std::vector<FxDef> d = {
        {"Distortion", P_DIST_ON, {P_DIST_TYPE}, {P_DIST_DRIVE, P_DIST_TONE, P_DIST_BITS, P_DIST_RATE, P_DIST_MIX}},
        {"Chorus", P_CHO_ON, {P_CHO_MODE}, {P_CHO_RATE, P_CHO_DEPTH, P_CHO_MIX}},
        {"Phaser", P_PH_ON, {}, {P_PH_RATE, P_PH_DEPTH, P_PH_FREQ, P_PH_FEEDBACK, P_PH_MIX}},
        {"Delay", P_DLY_ON, {P_DLY_MODE, P_DLY_DIV}, {P_DLY_TIME, P_DLY_FEEDBACK, P_DLY_TONE, P_DLY_MIX}, P_DLY_SYNC},
        {"Reverb", P_REV_ON, {P_REV_TYPE}, {P_REV_SIZE, P_REV_DECAY, P_REV_DAMP, P_REV_PREDELAY, P_REV_MIX}},
        {"OTT", P_OTT_ON, {}, {P_OTT_DEPTH, P_OTT_TIME, P_OTT_UP, P_OTT_DOWN, P_OTT_GAIN}},
        {"EQ", P_EQ_ON, {}, {P_EQ_LOW_FREQ, P_EQ_LOW_GAIN, P_EQ_MID_FREQ, P_EQ_MID_GAIN, P_EQ_MID_Q, P_EQ_HIGH_FREQ, P_EQ_HIGH_GAIN}},
    };
    return d;
}

class FxTab : public Page
{
public:
    explicit FxTab(HelixProcessor& p) : Page(p)
    {
        rack_ = own(std::make_unique<PanelFrame>("FX rack (top to bottom)"));
        for (int s = 0; s < kNumFxSlots; ++s)
        {
            Row r;
            r.power = std::make_unique<juce::ToggleButton>();
            r.name = std::make_unique<juce::TextButton>();
            r.up = std::make_unique<juce::TextButton>("^");
            r.down = std::make_unique<juce::TextButton>("v");
            r.name->setClickingTogglesState(false);
            r.name->onClick = [this, s] { select(typeAt(s)); };
            r.up->onClick = [this, s] { swap(s, s - 1); };
            r.down->onClick = [this, s] { swap(s, s + 1); };
            r.power->onClick = [this, s] {
                const auto& d = fxDefs()[(size_t)typeAt(s)];
                proc.setParamFromUI(d.on, rows_[(size_t)s].power->getToggleState() ? 1.0f : 0.0f);
            };
            for (juce::Component* c : {(juce::Component*)r.power.get(), (juce::Component*)r.name.get(), (juce::Component*)r.up.get(), (juce::Component*)r.down.get()})
                rack_->addAndMakeVisible(c);
            rows_.push_back(std::move(r));
        }
        for (int t = 0; t < (int)fxDefs().size(); ++t)
        {
            const auto& d = fxDefs()[(size_t)t];
            auto* panel = own(std::make_unique<PanelFrame>(d.name));
            panel->setPower(std::make_unique<ParamToggle>(proc, d.on));
            Detail det;
            det.panel = panel;
            for (int c : d.combos) det.combos.push_back(combo(c, panel, paramDef(c).name));
            if (d.syncToggle >= 0) det.sync = toggle(d.syncToggle, panel, "Sync");
            for (int k : d.knobs) det.knobs.push_back(knob(k, panel, {}, ParamKnob::Size::Large));
            details_.push_back(det);
        }
        info_ = own(std::make_unique<juce::Label>());
        info_->setJustificationType(juce::Justification::topLeft);
        info_->setFont(ui::font(13.0f));
        // Effects library: Insert 1 / 2 before the rack, FX 1 / 2 after it
        lib_ = own(std::make_unique<PanelFrame>("Effects   (Insert 1 > Insert 2 > FX 1 > FX 2)"));
        static const char* slotNames[fxlib::kNumSlots] = {"INSERT 1", "INSERT 2", "FX 1", "FX 2"};
        for (int sl = 0; sl < fxlib::kNumSlots; ++sl)
        {
            const int b = P_I1_TYPE + sl * (P_I2_TYPE - P_I1_TYPE);
            LibRow lr;
            lr.base = b;
            lr.title = std::make_unique<juce::Label>(juce::String(), slotNames[sl]);
            lr.title->setFont(ui::font(12.0f, true));
            lib_->addAndMakeVisible(*lr.title);
            lr.type = combo(b, lib_);
            lr.mix = knob(b + 1, lib_, "Mix", ParamKnob::Size::Small);
            for (int k = 0; k < fxlib::kNumKnobs; ++k) lr.knobs.push_back(knob(b + 2 + k, lib_, "-", ParamKnob::Size::Small));
            lr.lastType = proc.engine().params().getInt(b);
            for (int i = 0; i <= fxlib::kNumKnobs; ++i) lr.lastKnobs[i] = proc.engine().params().get(b + 1 + i);
            libRows_.push_back(std::move(lr));
        }
        // Sub guardian: below the split stays mono and clean and skips every effect
        sg_ = own(std::make_unique<PanelFrame>("Sub guardian   (below the split: mono, clean, no effects)"));
        sgOn_ = toggle(P_SG_ON, sg_, "On");
        sgKnobs_ = {knob(P_SG_FREQ, sg_, "Split", ParamKnob::Size::Small), knob(P_SG_LEVEL, sg_, "Sub Level", ParamKnob::Size::Small),
                    knob(P_SG_WARMTH, sg_, "Warmth", ParamKnob::Size::Small)};
        select(0);
    }

    void resized() override
    {
        rack_->setBounds(0, 0, 300, 708);
        auto c = rack_->content().withTrimmedTop(6);
        for (auto& r : rows_)
        {
            auto row = c.removeFromTop(52).reduced(0, 4);
            r.power->setBounds(row.removeFromLeft(22));
            r.down->setBounds(row.removeFromRight(30));
            row.removeFromRight(3);
            r.up->setBounds(row.removeFromRight(30));
            row.removeFromRight(6);
            r.name->setBounds(row);
        }
        for (auto& d : details_)
        {
            d.panel->setBounds(308, 0, 876, 300);
            auto r = d.panel->content().reduced(4);
            auto left = r.removeFromLeft(180);
            for (auto* cb : d.combos) { cb->setBounds(left.removeFromTop(48).withHeight(44)); left.removeFromTop(6); }
            if (d.sync) d.sync->setBounds(left.removeFromTop(26));
            const int n = (int)d.knobs.size();
            const int w = juce::jmin(120, r.getWidth() / juce::jmax(1, n));
            for (int k = 0; k < n; ++k) d.knobs[(size_t)k]->setBounds(r.getX() + k * w, r.getY() + 20, w, 130);
        }
        info_->setBounds(316, 306, 860, 44);
        // 0.3.6: the rack lives in the effect slots (its effects are the HELIX types), so only the slots show
        hideRack();
        lib_->setBounds(0, 0, 1184, 600);
        sg_->setBounds(0, 606, 1184, 102);
        {
            auto c = sg_->content();
            sgOn_->setBounds(c.removeFromLeft(90).withSizeKeepingCentre(80, 26));
            for (auto* k : sgKnobs_) k->setBounds(c.removeFromLeft(110));
        }
        auto lc = lib_->content().withTrimmedTop(8);
        for (auto& lr : libRows_)
        {
            auto row = lc.removeFromTop(lc.getHeight() / (int)(&libRows_.back() - &lr + 1));
            lr.title->setBounds(row.removeFromLeft(70));
            lr.type->setBounds(row.removeFromLeft(150).withSizeKeepingCentre(146, 26));
            row.removeFromLeft(6);
            const int w = row.getWidth() / (1 + fxlib::kNumKnobs);
            lr.mix->setBounds(row.removeFromLeft(w));
            for (auto* k : lr.knobs) k->setBounds(row.removeFromLeft(w));
        }
    }

    void tick() override
    {
        Page::tick();
        for (int s = 0; s < kNumFxSlots; ++s)
        {
            const int t = typeAt(s);
            const auto& d = fxDefs()[(size_t)t];
            auto& r = rows_[(size_t)s];
            const juce::String label = juce::String(s + 1) + "   " + d.name;
            if (r.name->getButtonText() != label) r.name->setButtonText(label);
            r.name->setToggleState(t == selected_, juce::dontSendNotification);
            r.power->setToggleState(proc.engine().params().getBool(d.on), juce::dontSendNotification);
            r.power->setVisible(true);
        }
        for (auto& lr : libRows_)
        {
            const auto& pr = proc.engine().params();
            const int type = pr.getInt(lr.base);
            // A new effect chosen in a slot starts from its own defaults (not when the knobs changed with it: a preset)
            if (type != lr.lastType)
            {
                bool moved = false;
                for (int i = 0; i <= fxlib::kNumKnobs; ++i) moved |= pr.get(lr.base + 1 + i) != lr.lastKnobs[i];
                if (!moved)
                {
                    proc.setParamFromUI(lr.base + 1, fxlib::mixDefault(type));
                    for (int k = 0; k < fxlib::kNumKnobs; ++k) proc.setParamFromUI(lr.base + 2 + k, fxlib::knobDefault(type, k));
                }
            }
            for (int i = 0; i <= fxlib::kNumKnobs; ++i) lr.lastKnobs[i] = pr.get(lr.base + 1 + i);
            float kn[fxlib::kNumKnobs];
            for (int i = 0; i < fxlib::kNumKnobs; ++i) kn[i] = pr.get(lr.base + 2 + i);
            for (int k = 0; k < fxlib::kNumKnobs; ++k)
            {
                const auto nm = fxlib::knobName(type, k, kn);
                lr.knobs[(size_t)k]->setLabel(nm.empty() ? juce::String("-") : juce::String(nm));
                lr.knobs[(size_t)k]->setEnabled(!nm.empty());
                lr.knobs[(size_t)k]->setAlpha(nm.empty() ? 0.35f : 1.0f);
            }
            lr.mix->setEnabled(type != 0);
            lr.mix->setAlpha(type != 0 ? 1.0f : 0.35f);
            lr.lastType = type;
        }
    }

private:
    // The effect in rack slot s: the slot params name effects; repeats are skipped, missing ones appended
    // (the same rule the engine uses).
    std::vector<int> order() const
    {
        std::vector<int> o;
        bool used[7] = {};
        for (int s = 0; s < kNumFxSlots; ++s)
        {
            const int t = juce::jlimit(0, 6, proc.engine().params().getInt(P_FX_ORDER1 + s));
            if (!used[t]) { used[t] = true; o.push_back(t); }
        }
        for (int t = 0; t < 7; ++t) if (!used[t]) o.push_back(t);
        return o;
    }
    int typeAt(int s) const { return order()[(size_t)s]; }
    void swap(int a, int b)
    {
        if (b < 0 || b >= kNumFxSlots) return;
        auto o = order();
        std::swap(o[(size_t)a], o[(size_t)b]);
        for (int s = 0; s < kNumFxSlots; ++s) proc.setParamFromUI(P_FX_ORDER1 + s, (float)o[(size_t)s]);
    }
    void hideRack()
    {
        rack_->setVisible(false);
        info_->setVisible(false);
        for (auto& d : details_) d.panel->setVisible(false);
    }

    void select(int t)
    {
        selected_ = t;
        for (int k = 0; k < (int)details_.size(); ++k) details_[(size_t)k].panel->setVisible(false);   // (rack hidden: 0.3.6)
        static const char* help[] = {
            "Tube: warm and asymmetric.  Hard Clip: square-edged.  Fold: wavefolder for growls.  Crush: bit depth and sample rate "
            "(12 bit / 26 kHz = SP-1200, lower for S950 grit).  Asym: half-wave crunch.  Tone is a low-pass after the distortion.",
            "Chorus: two voices.  Ensemble: three shimmering voices per side, the classic string-machine sound.",
            "Six-stage stereo phaser. Negative feedback gives a hollower sweep.",
            "Stereo or Ping-Pong. With Sync on, the time is a note value from the host tempo. Tone darkens each repeat. "
            "The dry sound always stays; Mix adds the echoes.",
            "Plate: bright and dense.  Hall: bigger and longer.",
            "Three-band upward and downward compressor (split at 120 Hz and 2.5 kHz): the modern bass sound. "
            "The most CPU-hungry effect.",
            "Low shelf, bell and high shelf, +-18 dB."};
        info_->setText(juce::String("Signal flows down the rack. Use ^ v to reorder, the lamp to switch an effect on.  ") + help[t],
                       juce::dontSendNotification);
        info_->setColour(juce::Label::textColourId, theme().muted);
    }

    struct LibRow
    {
        int base = 0, lastType = -1;
        float lastKnobs[fxlib::kNumKnobs + 1] = {};
        std::unique_ptr<juce::Label> title;
        ParamCombo* type = nullptr;
        ParamKnob* mix = nullptr;
        std::vector<ParamKnob*> knobs;
    };
    PanelFrame* lib_ = nullptr;
    std::vector<LibRow> libRows_;
    PanelFrame* sg_ = nullptr;
    ParamToggle* sgOn_ = nullptr;
    std::vector<ParamKnob*> sgKnobs_;
    struct Row { std::unique_ptr<juce::ToggleButton> power; std::unique_ptr<juce::TextButton> name, up, down; };
    struct Detail { PanelFrame* panel; std::vector<ParamCombo*> combos; std::vector<ParamKnob*> knobs; ParamToggle* sync = nullptr; };
    PanelFrame* rack_;
    std::vector<Row> rows_;
    std::vector<Detail> details_;
    juce::Label* info_;
    int selected_ = 0;
};

// --------------------------------------------------------------------------------------------- MATRIX tab

class MatrixTab : public Page
{
public:
    explicit MatrixTab(HelixProcessor& p) : Page(p)
    {
        frame_ = own(std::make_unique<PanelFrame>("Modulation matrix  (or drag a source onto any knob)"));
        for (int m = 0; m < kNumModSlots; ++m)
        {
            const int b = P_M1_SRC + m * (P_M2_SRC - P_M1_SRC);
            Row r;
            r.src = combo(b, frame_);
            r.dst = combo(b + 1, frame_);
            r.aux = combo(b + 3, frame_);
            r.curve = knob(b + 4, frame_, "Curve", ParamKnob::Size::Small);
            r.amount = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            r.amount->setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
            r.amount->getProperties().set("bipolar", true);
            frame_->addAndMakeVisible(*r.amount);
            r.att = std::make_unique<juce::SliderParameterAttachment>(*p.parameter(b + 2), *r.amount);
            r.clear = std::make_unique<juce::TextButton>("x");
            r.clear->setTooltip("Clear this slot");
            r.clear->onClick = [this, m] { proc.clearModSlot(m); };
            frame_->addAndMakeVisible(*r.clear);
            rows_.push_back(std::move(r));
        }
    }
    void resized() override
    {
        frame_->setBounds(0, 0, 1184, 708);
        auto c = frame_->content().withTrimmedTop(18);
        heads_ = c.removeFromTop(0);
        for (auto& r : rows_)
        {
            auto row = c.removeFromTop(41).reduced(0, 4);
            row.removeFromLeft(34);
            r.src->setBounds(row.removeFromLeft(190).withSizeKeepingCentre(186, 26));
            row.removeFromLeft(10);
            r.dst->setBounds(row.removeFromLeft(240).withSizeKeepingCentre(236, 26));
            row.removeFromLeft(10);
            r.amount->setBounds(row.removeFromLeft(330));
            row.removeFromLeft(10);
            r.aux->setBounds(row.removeFromLeft(190).withSizeKeepingCentre(186, 26));
            row.removeFromLeft(10);
            r.curve->setBounds(row.removeFromLeft(80).withTrimmedTop(-4).withTrimmedBottom(-4));
            r.clear->setBounds(row.removeFromRight(30).withSizeKeepingCentre(28, 24));
        }
    }
    void paint(juce::Graphics& g) override
    {
        Page::paint(g);
    }
    void paintOverChildren(juce::Graphics& g) override
    {
        const Theme& t = theme();
        auto c = frame_->getBounds().reduced(8, 6).withTrimmedTop(24);
        g.setColour(t.muted);
        g.setFont(ui::font(11.0f, true));
        const int y = c.getY();
        g.drawText("#", c.getX(), y, 30, 16, juce::Justification::centredLeft);
        g.drawText("SOURCE", c.getX() + 34, y, 190, 16, juce::Justification::centredLeft);
        g.drawText("DESTINATION", c.getX() + 234, y, 240, 16, juce::Justification::centredLeft);
        g.drawText("AMOUNT", c.getX() + 484, y, 330, 16, juce::Justification::centredLeft);
        g.drawText("AUX (scales the amount)", c.getX() + 824, y, 190, 16, juce::Justification::centredLeft);
        g.setFont(ui::font(12.0f, true));
        for (int m = 0; m < kNumModSlots; ++m)
        {
            const bool used = proc.engine().params().getInt(P_M1_SRC + m * (P_M2_SRC - P_M1_SRC)) != 0;
            g.setColour(used ? t.mod : t.muted);
            g.drawText(juce::String(m + 1), c.getX(), y + 18 + m * 41 + 4, 30, 33, juce::Justification::centredLeft);
        }
    }
    void tick() override { Page::tick(); repaint(); }
private:
    struct Row { ParamCombo *src, *dst, *aux; ParamKnob* curve; std::unique_ptr<juce::Slider> amount; std::unique_ptr<juce::SliderParameterAttachment> att; std::unique_ptr<juce::TextButton> clear; };
    PanelFrame* frame_;
    std::vector<Row> rows_;
    juce::Rectangle<int> heads_;
};

// --------------------------------------------------------------------------------------------- GLOBAL tab

class GlobalTab : public Page, public juce::ListBoxModel
{
public:
    GlobalTab(HelixProcessor& p, HelixEditor& ed) : Page(p), editor_(ed)
    {
        voice_ = own(std::make_unique<PanelFrame>("Voices"));
        poly_ = combo(P_POLY_MODE, voice_, "Poly mode");
        glideMode_ = combo(P_GLIDE_MODE, voice_, "Glide mode");
        steal_ = combo(P_STEAL, voice_, "Steal");
        vk_ = {knob(P_VOICES, voice_, "Voices", ParamKnob::Size::Large), knob(P_GLIDE, voice_, "Glide", ParamKnob::Size::Large),
               knob(P_BEND_RANGE, voice_, "Bend Range", ParamKnob::Size::Large), knob(P_VEL_SENS, voice_, "Vel Sens", ParamKnob::Size::Large)};
        engine_ = own(std::make_unique<PanelFrame>("Engine"));
        quality_ = combo(P_QUALITY, engine_, "Quality");
        guard_ = toggle(P_AUTO_SCALE, engine_, "CPU Guard");
        themeBox_ = own(std::make_unique<juce::ComboBox>(), engine_);
        for (int i = 0; i < numThemes(); ++i) themeBox_->addItem(themeAt(i).name, i + 1);
        themeBox_->setSelectedId(themeIndex() + 1, juce::dontSendNotification);
        themeBox_->onChange = [this] { const int i = themeBox_->getSelectedId() - 1; juce::MessageManager::callAsync([e = &editor_, i] { e->applyTheme(i); }); };
        // Reese: drifting detune between Osc A and B, the Growl macro and its amounts, Phase Lock
        reese_ = own(std::make_unique<PanelFrame>("Reese   (Growl moves cutoff, drive, positions and warps together)"));
        reeseLock_ = toggle(P_RS_LOCK, reese_, "Phase Lock");
        reeseTop_ = {knob(P_RS_DRIFT, reese_, "Drift"), knob(P_RS_DRIFT_RATE, reese_, "Drift Rate"),
                     knob(P_RS_GROWL, reese_, "Growl")};
        reeseAmt_ = {knob(P_RS_G_CUT, reese_, "Cutoff", ParamKnob::Size::Small), knob(P_RS_G_DRIVE, reese_, "Drive", ParamKnob::Size::Small),
                     knob(P_RS_G_POS, reese_, "Position", ParamKnob::Size::Small), knob(P_RS_G_WARP, reese_, "Warp", ParamKnob::Size::Small)};
        presets_ = own(std::make_unique<PanelFrame>("Presets"));
        list_ = own(std::make_unique<juce::ListBox>("presets", this), presets_);
        list_->setRowHeight(28);
        for (auto* b : {&load_, &rescan_}) presets_->addAndMakeVisible(b);
        load_.onClick = [this] { proc.loadPresetEntry(list_->getSelectedRow()); };
        rescan_.onClick = [this] { proc.refreshPresetList(); list_->updateContent(); repaint(); };
    }
    void resized() override
    {
        voice_->setBounds(0, 0, 580, 240);
        auto c = voice_->content();
        auto row = c.removeFromTop(52);
        layoutRow(row.withHeight(44), {poly_, glideMode_, steal_}, 12);
        c.removeFromTop(8);
        layoutRow(c, {vk_[0], vk_[1], vk_[2], vk_[3]});
        engine_->setBounds(0, 248, 580, 200);
        reese_->setBounds(0, 456, 580, 252);
        {
            auto rc = reese_->content();
            auto top = rc.removeFromTop(120);
            reeseLock_->setBounds(top.removeFromRight(120).withSizeKeepingCentre(110, 26));
            layoutRow(top, {reeseTop_[0], reeseTop_[1], reeseTop_[2]});
            layoutRow(rc, {reeseAmt_[0], reeseAmt_[1], reeseAmt_[2], reeseAmt_[3]});
        }
        auto e = engine_->content();
        auto r1 = e.removeFromTop(52);
        quality_->setBounds(r1.removeFromLeft(180).withHeight(44));
        r1.removeFromLeft(16);
        guard_->setBounds(r1.removeFromLeft(140).withTrimmedTop(14).withHeight(24));
        r1.removeFromLeft(16);
        themeBox_->setBounds(r1.removeFromLeft(160).withTrimmedTop(14).withHeight(26));
        cpuArea_ = e.withTrimmedTop(16);
        presets_->setBounds(588, 0, 596, 708);
        auto pr = presets_->content();
        auto bar = pr.removeFromBottom(34);
        load_.setBounds(bar.removeFromLeft(120).reduced(0, 3));
        bar.removeFromLeft(6);
        rescan_.setBounds(bar.removeFromLeft(140).reduced(0, 3));
        pr.removeFromBottom(6);
        list_->setBounds(pr);
    }
    void paintOverChildren(juce::Graphics& g) override
    {
        const Theme& t = theme();
        auto a = cpuArea_.translated(engine_->getX(), engine_->getY());
        g.setColour(t.muted);
        g.setFont(ui::font(11.0f, true));
        g.drawText("THEME", engine_->getX() + 8 + 180 + 16 + 140 + 16, engine_->getY() + 30, 160, 14, juce::Justification::centredLeft);
        g.drawText("CPU PER VOICE (share of one core)", a.removeFromTop(16), juce::Justification::centredLeft);
        a.removeFromTop(6);
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto row = a.removeFromTop(26).reduced(0, 4);
            g.setColour(t.muted);
            g.setFont(ui::font(11.0f));
            g.drawText("Voice " + juce::String(v + 1), row.removeFromLeft(60), juce::Justification::centredLeft);
            const float load = proc.engine().voiceLoad(v);
            g.setColour(t.track);
            g.fillRoundedRectangle(row.toFloat(), 3.0f);
            g.setColour(t.accent);
            g.fillRoundedRectangle(row.toFloat().withWidth(juce::jmin(1.0f, load * 10.0f) * (float)row.getWidth()), 3.0f);
            g.setColour(t.text);
            g.drawText(juce::String(load * 100.0f, 2) + "%", row.reduced(6, 0), juce::Justification::centredRight);
        }
        g.setColour(t.muted);
        g.drawText("Bars are scaled so a full bar = 10% of a core.", a.removeFromTop(18), juce::Justification::centredLeft);
    }
    void tick() override
    {
        Page::tick();
        if (shownRows_ != (int)proc.presetList().size()) { shownRows_ = (int)proc.presetList().size(); list_->updateContent(); }
        repaint(cpuArea_.translated(engine_->getX(), engine_->getY()));
    }
private:
    int getNumRows() override { return (int)proc.presetList().size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        const Theme& t = theme();
        const auto& l = proc.presetList();
        if (row < 0 || row >= (int)l.size()) return;
        const bool current = row == proc.currentPresetEntry();
        if (selected) g.fillAll(t.accent.withAlpha(0.25f));
        g.setColour(t.muted);
        g.setFont(ui::font(12.0f));
        g.drawText(l[(size_t)row].category, 10, 0, 110, h, juce::Justification::centredLeft);
        g.setColour(current ? t.accent : t.text);
        g.setFont(ui::font(14.0f, true));
        g.drawText(l[(size_t)row].name, 120, 0, w - 130, h, juce::Justification::centredLeft);
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { proc.loadPresetEntry(row); }

    HelixEditor& editor_;
    PanelFrame *voice_, *engine_, *presets_;
    ParamCombo *poly_, *glideMode_, *steal_, *quality_;
    ParamToggle* guard_;
    std::vector<ParamKnob*> vk_;
    juce::ComboBox* themeBox_;
    juce::ListBox* list_;
    PanelFrame* reese_ = nullptr;
    ParamToggle* reeseLock_ = nullptr;
    std::vector<ParamKnob*> reeseTop_, reeseAmt_;
    juce::TextButton load_{"Load"}, rescan_{"Refresh list"};
    juce::Rectangle<int> cpuArea_;
    int shownRows_ = -1;
};

} // namespace

// --------------------------------------------------------------------------------------------- editor

HelixEditor::HelixEditor(HelixProcessor& p) : AudioProcessorEditor(&p), proc_(p)
{
    const double savedScale = juce::jlimit(0.7, 2.0, proc_.settings().getDoubleValue("scale", 1.0));
    setThemeIndex(proc_.settings().getIntValue("theme", 2));   // Massive unless the user chose another
    lnf_.applyTheme();
    setLookAndFeel(&lnf_);
    addAndMakeVisible(content_);
    content_.setSize(kW, kH);

    buildHeader();
    tabs_.push_back(std::make_unique<OscTab>(proc_));
    tabs_.push_back(std::make_unique<FxTab>(proc_));
    tabs_.push_back(std::make_unique<MatrixTab>(proc_));
    tabs_.push_back(std::make_unique<GlobalTab>(proc_, *this));
    const char* names[] = {"OSC", "FX", "MATRIX", "GLOBAL"};
    for (int i = 0; i < 4; ++i)
    {
        auto* b = tabButtons_.add(new juce::TextButton(names[i]));
        b->onClick = [this, i] { showTab(i); };
        content_.addAndMakeVisible(b);
        content_.addChildComponent(*tabs_[(size_t)i]);
        tabs_[(size_t)i]->setBounds(8, 84, kW - 16, kH - 92);
    }
    setResizable(true, true);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double)kW / kH);
    setResizeLimits(840, 560, 2400, 1600);
    setSize(juce::roundToInt(kW * savedScale), juce::roundToInt(kH * savedScale));
    constructed_ = true;   // from now on, window size changes are the user's: remember them
    // HELIX_START_PROGRAM=<n> loads factory preset n, HELIX_START_PAGE=<n> opens another tab first
    // (screenshots and UI checks).
    if (const char* prog = std::getenv("HELIX_START_PROGRAM")) proc_.setCurrentProgram(std::atoi(prog));
    const char* start = std::getenv("HELIX_START_PAGE");
    showTab(start ? juce::jlimit(0, 3, std::atoi(start)) : 0);
    startTimerHz(30);
}

HelixEditor::~HelixEditor()
{
    stopTimer();
    tabs_.clear();
    master_.reset();
    setLookAndFeel(nullptr);
}

void HelixEditor::buildHeader()
{
    for (auto* b : {&prev_, &next_, &presetBox_, &save_, &mutate_, &init_}) content_.addAndMakeVisible(b);
    prev_.onClick = [this] { stepPreset(-1); };
    next_.onClick = [this] { stepPreset(1); };
    presetBox_.onClick = [this] { showPresetMenu(); };
    save_.onClick = [this] { savePreset(); };
    init_.onClick = [this] { helix::Preset init; proc_.loadPreset(init); };
    mutate_.setTooltip("Nudge every continuous knob by up to 10%");
    mutate_.onClick = [this] {
        helix::Preset m = proc_.currentPreset();
        helix::mutatePreset(m, 0.1f, mutateSeed_++ * 7919u + (uint32_t)juce::Time::currentTimeMillis());
        m.name = proc_.presetName().toStdString();
        if (m.name.find(" *") == std::string::npos) m.name += " *";
        proc_.loadPreset(m);
    };
    master_ = std::make_unique<ParamKnob>(proc_, P_MASTER, "Master", ParamKnob::Size::Small);
    content_.addAndMakeVisible(*master_);
    cpu_.setJustificationType(juce::Justification::centredRight);
    cpu_.setFont(ui::font(12.0f, true));
    content_.addAndMakeVisible(cpu_);
}

void HelixEditor::showTab(int index)
{
    tab_ = index;
    for (int i = 0; i < (int)tabs_.size(); ++i)
    {
        tabs_[(size_t)i]->setVisible(i == index);
        tabButtons_[i]->setToggleState(i == index, juce::dontSendNotification);
    }
}

void HelixEditor::applyTheme(int index)
{
    setThemeIndex(index);
    proc_.settings().setValue("theme", index);
    lnf_.applyTheme();
    sendLookAndFeelChange();
    repaint();
}

void HelixEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme().bg);
    // the header is drawn in the content's (unscaled) coordinates
    const float s = (float)getWidth() / (float)kW;
    g.addTransform(juce::AffineTransform::scale(s));
    const Theme& t = theme();
    g.setColour(t.panel);
    g.fillRect(0, 0, kW, 46);
    g.setColour(t.edge);
    g.drawHorizontalLine(46, 0.0f, (float)kW);
    // logo: a small double helix and the name
    juce::Path helix1, helix2;
    for (int i = 0; i <= 40; ++i)
    {
        const float x = 16.0f + (float)i * 0.75f, ph = (float)i / 40.0f * juce::MathConstants<float>::twoPi * 1.5f;
        const float y1 = 23.0f + std::sin(ph) * 8.0f, y2 = 23.0f - std::sin(ph) * 8.0f;
        if (i == 0) { helix1.startNewSubPath(x, y1); helix2.startNewSubPath(x, y2); }
        else { helix1.lineTo(x, y1); helix2.lineTo(x, y2); }
    }
    g.setColour(t.accent);
    g.strokePath(helix1, juce::PathStrokeType(2.2f));
    g.setColour(t.mod);
    g.strokePath(helix2, juce::PathStrokeType(2.2f));
    g.setColour(t.text);
    g.setFont(ui::font(22.0f, true));
    g.drawText("HELIX", 54, 8, 100, 30, juce::Justification::centredLeft);
}

void HelixEditor::resized()
{
    const float s = (float)getWidth() / (float)kW;
    content_.setTransform(juce::AffineTransform::scale(s));
    content_.setBounds(0, 0, kW, kH);
    if (constructed_) proc_.settings().setValue("scale", (double)s);
    prev_.setBounds(330, 9, 28, 28);
    presetBox_.setBounds(362, 9, 300, 28);
    next_.setBounds(666, 9, 28, 28);
    save_.setBounds(708, 9, 64, 28);
    mutate_.setBounds(776, 9, 76, 28);
    init_.setBounds(856, 9, 52, 28);
    cpu_.setBounds(1040, 9, 150, 28);
    master_->setBounds(970, 2, 60, 44);
    int x = 8;
    for (auto* b : tabButtons_) { b->setBounds(x, 54, 92, 24); x += 96; }
}

void HelixEditor::timerCallback()
{
    if (tab_ >= 0 && tab_ < (int)tabs_.size()) tabs_[(size_t)tab_]->tick();
    master_->refresh();
    const juce::String name = proc_.presetName();
    if (presetBox_.getButtonText() != name) presetBox_.setButtonText(name);
    juce::String cpu = "CPU " + juce::String(juce::roundToInt(proc_.engine().cpuLoad() * 100.0f)) + "%";
    if (proc_.engine().heavyFxActive()) cpu += "  OTT";
    if (proc_.engine().guardLevel() > 0) cpu += "  ECO" + juce::String(proc_.engine().guardLevel());
    if (cpu_.getText() != cpu) cpu_.setText(cpu, juce::dontSendNotification);
    cpu_.setColour(juce::Label::textColourId, proc_.engine().guardLevel() > 0 ? theme().accent : theme().muted);
}

void HelixEditor::showPresetMenu()
{
    proc_.refreshPresetList();
    const auto& l = proc_.presetList();
    juce::PopupMenu m;
    std::map<juce::String, juce::PopupMenu> cats;
    std::vector<juce::String> order;
    for (int i = 0; i < (int)l.size(); ++i)
    {
        const auto& e = l[(size_t)i];
        const juce::String cat = e.factory >= 0 ? e.category : "User / " + e.category;
        if (!cats.count(cat)) order.push_back(cat);
        cats[cat].addItem(i + 1, e.name, true, i == proc_.currentPresetEntry());
    }
    for (auto& c : order) m.addSubMenu(c, cats[c]);
    m.addSeparator();
    m.addItem(10000, "Open the presets folder");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBox_), [this](int r) {
        if (r == 10000) HelixProcessor::userPresetFolder().startAsProcess();
        else if (r > 0) proc_.loadPresetEntry(r - 1);
    });
}

void HelixEditor::stepPreset(int d)
{
    const int n = (int)proc_.presetList().size();
    if (n == 0) return;
    const int cur = proc_.currentPresetEntry();
    proc_.loadPresetEntry(cur < 0 ? 0 : (cur + d + n) % n);
}

void HelixEditor::savePreset()
{
    saveDialog_ = std::make_unique<juce::AlertWindow>("Save preset", "Name and category", juce::MessageBoxIconType::NoIcon);
    saveDialog_->addTextEditor("name", proc_.presetName().upToFirstOccurrenceOf(" *", false, false), "Name");
    saveDialog_->addComboBox("category", {"Bass", "Lead", "Pad", "Atmos", "Stab", "FX", "Misc"}, "Category");
    saveDialog_->addButton("Save", 1);
    saveDialog_->addButton("Cancel", 0);
    saveDialog_->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
        if (result == 1 && saveDialog_)
        {
            const auto name = saveDialog_->getTextEditorContents("name").trim();
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
                proc_.refreshPresetList();
                helix::Preset reload;
                helix::Preset::fromString(p.toString(), reload);
                proc_.loadPreset(reload);
            }
        }
        saveDialog_.reset();
    }), false);
}
