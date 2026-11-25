// The MPC app logic (helix-mpc/app) driven through a fake IPlatform: pad browse (the Shift+pad replacement),
// triggers, Sample -> Table, program changes, state round trip, CPU readout.
#include "TestUtil.h"
#include "Analysis.h"
#include "HelixMpcApp.h"
#include "helix/FxLibrary.h"
#include "helix/WavIO.h"
#include "helix/WavetableGen.h"
#include <filesystem>
#include <fstream>
#include <map>

using namespace helix;
namespace fs = std::filesystem;

struct FakePlatform : IPlatform
{
    std::string dir;
    std::map<int, float> notified;
    int program = -1;
    const char* platformName() const override { return "fake"; }
    std::string contentDir() const override { return dir; }
    void notifyParameter(int index, float v) override { notified[index] = v; }
    void requestDisplayRefresh() override {}
    int currentProgram() const override { return program; }
};

static void block(HelixMpcApp& app, std::vector<MidiMsg> ev = {}, int n = 256)
{
    std::vector<float> l((size_t)n), r((size_t)n);
    Transport tr;
    app.process(l.data(), r.data(), n, ev.data(), (int)ev.size(), tr);
}

int main()
{
    FakePlatform pf;
    pf.dir = (fs::temp_directory_path() / "helix_mpc_content").string();
    { std::error_code ec; fs::remove_all(pf.dir, ec); }
    { std::error_code ec; fs::create_directories(fs::path(pf.dir) / "Samples", ec); }
    { std::error_code ec; fs::create_directories(fs::path(pf.dir) / "Presets" / "Bass", ec); }
    {
        // A user preset and a sample
        Preset u = factoryPresets()[5]; u.name = "My Wobble";
        std::ofstream(fs::path(pf.dir) / "Presets" / "Bass" / "My Wobble.json") << u.toString();
        std::vector<float> x(44100);
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.5f * std::sin(kTwoPi * 220.0f * (float)i / 44100.0f) * (1.0f + (float)i / 44100.0f);
        writeWavFile((fs::path(pf.dir) / "Samples" / "tone.wav").string(), x.data(), x.size(), 1, 44100);
    }

    HelixMpcApp app(pf, false);   // no worker thread: the test calls tick() itself
    app.prepare(44100.0, 512);
    app.waitIdle();
    CHECK(app.numPresets() == (int)factoryPresets().size() + 1, "presets: %d", app.numPresets());
    CHECK(app.presetName((int)factoryPresets().size()) == "My Wobble", "user preset name");

    // Pad browse: pad 3 (note 38) selects preset 2 and is replayed once the preset is in.
    app.setParam(MP_PAD_BROWSE, 1.0f);
    block(app, {MidiMsg::make(0, 0x90, 38, 100)});
    CHECK(app.engine().activeVoices() == 0, "the browse pad must not play the old preset");
    app.tick();
    CHECK((int)app.getParam(MP_PRESET) == 2, "pad 3 should select preset 2 (got %d)", (int)app.getParam(MP_PRESET));
    CHECK(app.engine().params().get(P_FX_ORDER1) == factoryPresets()[2].params[P_FX_ORDER1] &&
          app.engine().params().get(P_F2_TYPE) == factoryPresets()[2].params[P_F2_TYPE], "preset 2 not applied");
    block(app);
    CHECK(app.engine().activeVoices() == 1, "the auditioned note did not play (%d voices)", app.engine().activeVoices());
    block(app, {MidiMsg::make(0, 0x80, 38, 0)});
    app.setParam(MP_PAD_BROWSE, 0.0f);

    // Load trigger: preset index -> applied, trigger resets itself and the host is told.
    app.setParam(MP_PRESET, 15.0f);
    app.setParam(MP_LOAD, 1.0f);
    app.tick();
    CHECK(app.getParam(MP_LOAD) == 0.0f && pf.notified.count(MP_LOAD), "load trigger did not reset");
    CHECK(app.engine().params().get(P_CHO_ON) == factoryPresets()[15].params[P_CHO_ON], "90s Pad not loaded");
    std::string text;
    CHECK(app.formatValue(MP_PRESET, 15.0f, text) && text == "90s Pad", "preset value text '%s'", text.c_str());

    // Compact value text for the skin's large type (units kept, no "12000.0 Hz").
    const struct { int index; float plain; const char* want; } compact[] = {
        {P_F1_CUTOFF, 12000.0f, "12.0 kHz"}, {P_F1_CUTOFF, 1200.0f, "1.20 kHz"}, {P_F1_CUTOFF, 440.0f, "440 Hz"},
        {P_E1_D, 500.0f, "500 ms"}, {P_E1_D, 1200.0f, "1.20 s"}, {P_A_BLEND, 0.8f, "80%"}, {P_A_FINE, -9.0f, "-9.00 ct"}};
    for (const auto& c : compact)
        CHECK(app.formatValue(c.index, c.plain, text) && text == c.want, "param %d value text '%s', want '%s'", c.index,
              text.c_str(), c.want);
    CHECK(!app.formatValue(P_F1_TYPE, 1.0f, text), "choice parameters keep the SDK's own text");

    // Mutate trigger changes continuous parameters only.
    const float cutoffBefore = app.engine().params().get(P_F1_CUTOFF);
    const float typeBefore = app.engine().params().get(P_F1_TYPE);
    app.setParam(MP_MUTATE, 1.0f);
    app.tick();
    CHECK(app.engine().params().get(P_F1_CUTOFF) != cutoffBefore, "mutate did nothing");
    CHECK(app.engine().params().get(P_F1_TYPE) == typeBefore, "mutate changed a choice");

    // Sample -> Table from Content/Samples into osc B.
    CHECK(app.formatValue(MP_S2T_FILE, 0.0f, text) && text == "tone", "sample name '%s'", text.c_str());
    app.setParam(MP_S2T_SLOT, 1.0f);
    app.setParam(MP_S2T_FRAMES, 1.0f);   // 16
    app.setParam(MP_S2T_GO, 1.0f);
    app.tick();
    app.waitIdle();
    CHECK(app.engine().uiTable(kSlotB) && app.engine().uiTable(kSlotB)->numFrames() == 16, "S>T into osc B");

    // Table index wraps: every position names a table, and state round trips exactly.
    app.engine().params().set(P_A_TABLE, 353.0f);
    app.tick();
    app.waitIdle();
    CHECK(app.formatValue(P_A_TABLE, 353.0f, text) && text == builtinTableNames()[353 % builtinTableNames().size()], "wrapped table name '%s'", text.c_str());
    const std::string state = app.saveState();
    {
        HelixMpcApp other(pf, false);
        other.prepare(44100.0, 512);
        for (int i = 0; i < kNumParams; ++i) other.setParam(i, app.getParam(i));   // what the framework restores
        other.loadState(state);
        other.waitIdle();
        CHECK(other.getParam(P_A_TABLE) == 353.0f, "table index changed on reload (%g)", other.getParam(P_A_TABLE));
        CHECK(other.saveState() == state, "state does not round-trip");
        CHECK(other.engine().uiTable(kSlotB) && other.engine().uiTable(kSlotB)->numFrames() == 16, "S>T table not restored from state");
    }

    // Program change from the host: LFO shapes (not parameters) follow the preset.
    pf.program = 3;
    app.engine().lfoShapes().steps[0][0].store(0.123f);
    app.tick();
    CHECK(app.engine().lfoShapes().steps[0][0].load() == factoryPresets()[3].lfoSteps[0][0], "program change did not apply LFO shapes");

    // CPU readout is pushed to the host.
    for (int i = 0; i < 12; ++i) app.tick();
    CHECK(pf.notified.count(MP_CPU) == 1, "CPU readout never reported");

    // Performer editing: choose step 5, its curve and level appear; tap a curve / turn the level, the step changes.
    app.engine().params().set(P_P1_C5, 4.0f);
    app.engine().params().set(P_P1_S5, -0.5f);
    app.setParam(MP_P1_EDIT, 4.0f);
    app.tick();
    CHECK(app.getParam(MP_P1_CURVE) == 4.0f && app.getParam(MP_P1_LEVEL) == -0.5f, "edit step did not show step 5 (%g, %g)",
          (double)app.getParam(MP_P1_CURVE), (double)app.getParam(MP_P1_LEVEL));
    app.setParam(MP_P1_CURVE, 17.0f);
    app.setParam(MP_P1_LEVEL, 0.75f);
    app.tick();
    CHECK(app.engine().params().get(P_P1_C5) == 17.0f && app.engine().params().get(P_P1_S5) == 0.75f, "curve / level not written");
    CHECK(pf.notified.count(P_P1_C5) == 1, "host not told about the new curve");
    app.engine().params().set(P_P1_C5, 9.0f);   // changed elsewhere (e.g. a preset)
    app.tick();
    CHECK(app.getParam(MP_P1_CURVE) == 9.0f, "curve readout did not follow");
    CHECK(app.engine().params().get(P_P1_C5) == 9.0f, "the readout overwrote the step");
    app.setParam(MP_P1_EDIT, 0.0f);
    app.tick();
    CHECK(app.engine().params().get(P_P1_C5) == 9.0f && app.getParam(MP_P1_CURVE) == app.engine().params().get(P_P1_C1),
          "changing the edit step changed a curve");

    // Dragging step 7's bar selects step 7 for curve editing.
    app.tick();
    app.engine().params().set(P_P1_S7, -0.3f);
    app.tick();
    CHECK((int)app.getParam(MP_P1_EDIT) == 6, "dragging bar 7 did not select it (edit step %d)", (int)app.getParam(MP_P1_EDIT) + 1);
    CHECK(app.getParam(MP_P1_LEVEL) == -0.3f, "level readout did not follow the selected bar");

    // FILTER page list: follows model / type set on the MODEL page, and writes them back when turned.
    CHECK((int)combinedFilterChoices().size() == 115, "filter list has %d entries", (int)combinedFilterChoices().size());
    app.engine().params().set(P_F1_MODEL, 13.0f);   // TB-303
    app.engine().params().set(P_F1_KIND, 2.0f);     // BP
    app.tick();
    CHECK((int)app.getParam(MP_F1_FILTER) == combinedFilterIndex(13, 0, 2), "F1 list did not follow the model (%d)",
          (int)app.getParam(MP_F1_FILTER));
    CHECK(combinedFilterChoices()[(size_t)app.getParam(MP_F1_FILTER)] == "TB-303 BP", "list name %s",
          combinedFilterChoices()[(size_t)app.getParam(MP_F1_FILTER)].c_str());
    app.setParam(MP_F1_FILTER, 3.0f);               // a HELIX type
    app.tick();
    CHECK(app.engine().params().getInt(P_F1_MODEL) == 0 && app.engine().params().getInt(P_F1_TYPE) == 3, "HELIX type not written");
    app.setParam(MP_F1_FILTER, (float)combinedFilterIndex(10, 0, 1));   // MS-20 HP
    app.tick();
    CHECK(app.engine().params().getInt(P_F1_MODEL) == 10 && app.engine().params().getInt(P_F1_KIND) == 1, "Mega model not written");

    // Effects library on the MPC: picking an effect gives its defaults, the knob names / values follow it.
    app.tick();
    app.setParam(P_X1_TYPE, 11.0f);   // Plate
    app.tick();
    CHECK(std::fabs(app.getParam(P_X1_A) - fxlib::knobDefault(11, 0)) < 1e-6f && std::fabs(app.getParam(P_X1_MIX) - fxlib::mixDefault(11)) < 1e-6f,
          "plate defaults not applied (A %.3f)", (double)app.getParam(P_X1_A));
    std::string txt;
    CHECK(app.formatValue(MP_LIBNAME + 2 * fxlib::kNumKnobs + 0, 0.0f, txt) && txt == "Decay", "FX 1 knob A name: %s", txt.c_str());
    CHECK(app.formatValue(P_X1_A, app.getParam(P_X1_A), txt) && txt.find("s") != std::string::npos, "FX 1 A value: %s", txt.c_str());
    CHECK(app.formatValue(MP_LIBNAME + 2 * fxlib::kNumKnobs + 5, 0.0f, txt) && txt == "-", "unused knob F name: %s", txt.c_str());
    // a preset-style change (type and knobs together) keeps its knobs
    app.setParam(P_X1_TYPE, 2.0f);
    app.setParam(P_X1_A, 0.123f);
    app.tick();
    CHECK(std::fabs(app.getParam(P_X1_A) - 0.123f) < 1e-6f, "preset knob overwritten (%.3f)", (double)app.getParam(P_X1_A));

    { std::error_code ec; fs::remove_all(pf.dir, ec); }
    TEST_MAIN_END();
}
