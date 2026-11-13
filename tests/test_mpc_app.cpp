// The MPC app logic (helix-mpc/app) driven through a fake IPlatform: pad browse (the Shift+pad replacement),
// triggers, Sample -> Table, program changes, state round trip, CPU readout.
#include "TestUtil.h"
#include "Analysis.h"
#include "HelixMpcApp.h"
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

    { std::error_code ec; fs::remove_all(pf.dir, ec); }
    TEST_MAIN_END();
}
