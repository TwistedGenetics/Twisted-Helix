// Pad scenes: recall (instant and morphed), Scene Pads notes, store, preset round trip; the filth mod wheel.
#include "TestUtil.h"
#include "helix/Engine.h"
#include "helix/Preset.h"
#include <cmath>
#include <vector>

using namespace helix;

static void run(Engine& e, int blocks, const MidiMsg* m = nullptr, int nm = 0)
{
    std::vector<float> l(256), r(256);
    Transport tr;
    for (int b = 0; b < blocks; ++b) e.process(l.data(), r.data(), 256, b == 0 ? m : nullptr, b == 0 ? nm : 0, tr);
}

static double zcRate(Engine& e, bool filth)
{
    e.params().set(P_FILTH, filth ? 1.0f : 0.0f);
    std::vector<float> l(256), r(256), out;
    Transport tr;
    MidiMsg ev[2] = {MidiMsg::make(0, 0xB0, 1, 127), MidiMsg::make(0, 0x90, 36, 110)};
    for (int b = 0; b < 100; ++b)
    {
        e.process(l.data(), r.data(), 256, b == 0 ? ev : nullptr, b == 0 ? 2 : 0, tr);
        if (b > 50) out.insert(out.end(), l.begin(), l.end());
    }
    MidiMsg off = MidiMsg::make(0, 0x80, 36, 0);
    run(e, 200, &off, 1);
    int z = 0;
    for (size_t i = 1; i < out.size(); ++i) z += (out[i - 1] < 0) != (out[i] < 0);
    return z;
}

int main()
{
    Engine e;
    e.prepare(44100.0, 256);
    e.setRealtime(false);
    CHECK(std::fabs(e.scenes().v[15][3].load() - 1.0f) < 1e-6f && e.scenes().v[0][0].load() == 0.0f, "default ladder");

    // instant recall
    e.params().set(P_SCENE_MORPH, 0.0f);
    for (int m = 0; m < kNumMacros; ++m) e.scenes().v[3][m].store(0.1f * (float)m);
    e.recallScene(3);
    run(e, 2);
    CHECK(std::fabs(e.params().get(P_MACRO6) - 0.5f) < 1e-6f && e.lastScene() == 3, "instant recall (%f)", e.params().get(P_MACRO6));

    // morph: half way the knobs have not moved yet, after it they have
    e.params().set(P_SCENE_MORPH, 500.0f);
    e.recallScene(15);
    run(e, 40);   // ~230 ms
    CHECK(std::fabs(e.params().get(P_MACRO6) - 0.5f) < 1e-6f, "morph wrote the knobs early");
    run(e, 60);
    CHECK(std::fabs(e.params().get(P_MACRO6) - 1.0f) < 1e-6f, "morph did not land (%f)", e.params().get(P_MACRO6));

    // Scene Pads: pad A05 (note 40) recalls scene 5 and plays nothing
    e.params().set(P_SCENE_MORPH, 0.0f);
    e.params().set(P_SCENE_PADS, 1.0f);
    MidiMsg pad = MidiMsg::make(0, 0x90, 40, 100);
    run(e, 2, &pad, 1);
    CHECK(e.lastScene() == 4 && e.activeVoices() == 0, "scene pad: scene %d, voices %d", e.lastScene(), e.activeVoices());
    CHECK(std::fabs(e.params().get(P_MACRO1) - 4.0f / 15.0f) < 1e-6f, "scene pad values");
    MidiMsg key = MidiMsg::make(0, 0x90, 60, 100);
    run(e, 2, &key, 1);
    CHECK(e.activeVoices() == 1, "notes outside the pads must still play");
    MidiMsg keyOff = MidiMsg::make(0, 0x80, 60, 0);
    run(e, 200, &keyOff, 1);
    e.params().set(P_SCENE_PADS, 0.0f);

    // store, preset round trip
    e.params().set(P_MACRO2, 0.42f);
    e.storeScene(9);
    CHECK(std::fabs(e.scenes().v[9][1].load() - 0.42f) < 1e-6f, "store");
    Preset p = capturePreset(e, nullptr), q;
    CHECK(Preset::fromString(p.toString(), q) && std::fabs(q.scenes[9][1] - 0.42f) < 1e-6f && std::fabs(q.scenes[3][5] - 0.5f) < 1e-6f,
          "scenes round trip");
    Engine f;
    applyPreset(q, f, nullptr);
    CHECK(std::fabs(f.scenes().v[9][1].load() - 0.42f) < 1e-6f, "apply scenes");

    // filth: mod wheel up with Filth on opens and drives a dark patch
    Engine g;
    g.prepare(44100.0, 256);
    g.setRealtime(false);
    g.params().set(P_F1_CUTOFF, 300.0f);
    g.params().set(P_SUB_ON, 0.0f);
    const double clean = zcRate(g, false), dirty = zcRate(g, true);
    CHECK(dirty > clean * 1.3, "filth did not open the sound (%g vs %g)", dirty, clean);
    TEST_MAIN_END();
}
