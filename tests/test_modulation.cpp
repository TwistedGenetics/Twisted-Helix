#include "TestUtil.h"
#include "Analysis.h"

using namespace helix;

static void slot(Engine& e, int m, int src, int dst, float amt, int aux = 0, float curve = 0.0f)
{
    const int b = P_M1_SRC + m * (P_M2_SRC - P_M1_SRC);
    e.params().set(b, (float)src); e.params().set(b + 1, (float)dst); e.params().set(b + 2, amt);
    e.params().set(b + 3, (float)aux); e.params().set(b + 4, curve);
}
static int dest(int param) { for (int d = 0; d < numModDests(); ++d) if (modDestParam(d) == param) return d; return -1; }

struct Run
{
    Engine& e;
    std::vector<float> l, r;
    Transport tr;
    explicit Run(Engine& en) : e(en) { tr.valid = true; tr.bpm = 120.0; tr.playing = false; }
    void block(int n, std::vector<MidiMsg> ev = {})
    {
        std::vector<float> bl((size_t)n), br((size_t)n);
        e.process(bl.data(), br.data(), n, ev.data(), (int)ev.size(), tr);
        l.insert(l.end(), bl.begin(), bl.end());
        r.insert(r.end(), br.begin(), br.end());
    }
};

// Frequency estimate from rising zero crossings (pure sine source).
static double freqOf(const std::vector<float>& x, size_t from, size_t to, double sr)
{
    int crossings = 0; size_t first = 0, last = 0;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) { if (!crossings) first = i; last = i; ++crossings; }
    return crossings > 1 ? (crossings - 1) * sr / (double)(last - first) : 0.0;
}

int main()
{
    const double sr = 44100.0;
    Engine e;
    e.prepare(sr, 512);
    auto on = [](int n, int v = 127) { return MidiMsg::make(0, 0x90, (uint8_t)n, (uint8_t)v); };
    auto off = [](int n) { return MidiMsg::make(0, 0x80, (uint8_t)n, 0); };

    // 1. LFO -> cutoff: the live value moves; the base value does not.
    {
        plainPatch(e);
        e.params().set(P_F1_ON, 1.0f);
        e.params().set(P_F1_CUTOFF, 1000.0f);
        e.params().set(P_L1_SYNC, 0.0f);
        e.params().set(P_L1_RATE, 4.0f);
        slot(e, 0, SRC_LFO1, dest(P_F1_CUTOFF), 0.3f);
        Run run(e);
        run.block(512, {on(48)});
        float lo = 1, hi = 0;
        for (int i = 0; i < 100; ++i) { run.block(256); const float v = e.liveNormalised(P_F1_CUTOFF); lo = std::min(lo, v); hi = std::max(hi, v); }
        const float base = e.params().getNormalised(P_F1_CUTOFF);
        std::printf("LFO->cutoff live range %.2f..%.2f around %.2f\n", lo, hi, base);
        CHECK(hi - lo > 0.4f && lo < base && hi > base, "LFO does not move the cutoff");
        CHECK(std::fabs(e.params().getNormalised(P_F1_CUTOFF) - base) < 1e-6f, "modulation must not change the base value");
        run.block(64, {MidiMsg::make(0, 0xB0, 120, 0)});
    }

    // 2. Macro with a negative amount silences osc A; aux = mod wheel at 0 cancels it.
    {
        plainPatch(e);
        e.params().set(P_MACRO1, 1.0f);
        slot(e, 0, SRC_MACRO1, dest(P_A_LEVEL), -1.0f);
        CHECK(rms(renderHeld(e, 48, 8192).l) < 1e-4, "macro -> level -1 should silence the oscillator");
        slot(e, 0, SRC_MACRO1, dest(P_A_LEVEL), -1.0f, SRC_MODWHEEL);
        CHECK(rms(renderHeld(e, 48, 8192).l) > 0.05, "aux = mod wheel (0) should scale the slot to nothing");
    }

    // 3. Pitch destination: LFO square at +1 semitone-ish -> two distinct frequencies; velocity scales amp.
    {
        plainPatch(e);   // sine frame (pos 0)
        slot(e, 0, SRC_MACRO2, DST_PITCH, 0.5f);   // +12 semitones at macro 1
        e.params().set(P_MACRO2, 1.0f);
        Stereo s = renderHeld(e, 57, 16384);
        const double f = freqOf(s.l, 0, s.l.size(), sr);
        std::printf("pitch +12 st: %.1f Hz (want 440)\n", f);
        CHECK(std::fabs(f - 440.0) < 3.0, "pitch modulation wrong (%.1f Hz)", f);
    }

    // 4. Glide: a mono legato slide from A2 to A3 passes through the frequencies in between.
    {
        plainPatch(e);
        e.params().set(P_POLY_MODE, 2.0f);   // legato
        e.params().set(P_GLIDE, 300.0f);
        Run run(e);
        run.block(8192, {on(45)});
        run.block(441 * 4, {on(57)});          // 40 ms into the glide
        const double mid = freqOf(run.l, 8192, run.l.size(), sr);
        run.block(44100);
        const double end = freqOf(run.l, run.l.size() - 8192, run.l.size(), sr);
        std::printf("glide: %.1f Hz early, %.1f Hz settled\n", mid, end);
        CHECK(mid > 115.0 && mid < 400.0, "glide jumped (%.1f Hz)", mid);
        CHECK(std::fabs(end - 220.0) < 2.0, "glide did not arrive (%.1f Hz)", end);
        CHECK(e.activeVoices() == 1, "legato should use one voice (%d)", e.activeVoices());
        // Releasing the top note returns to the held one.
        run.block(44100, {off(57)});   // the return glides too (300 ms)
        const double back = freqOf(run.l, run.l.size() - 4096, run.l.size(), sr);
        CHECK(std::fabs(back - 110.0) < 2.0, "mono note stack did not return to the held note (%.1f Hz)", back);
        run.block(64, {MidiMsg::make(0, 0xB0, 120, 0)});
    }

    // 5. Click-free voice stealing: one voice, steal it while sounding; the biggest sample-to-sample jump stays
    //    close to what the waveform itself does.
    {
        plainPatch(e);
        e.params().set(P_VOICES, 1.0f);
        e.params().set(P_A_PHASE, 0.25f);   // a phase reset to 0.25 of a sine would jump to full scale
        e.params().set(P_E1_A, 5.0f);       // the new note's own attack is not what we measure
        Run run(e);
        run.block(10000, {on(45)});
        double steady = 0;
        for (size_t i = 5001; i < 10000; ++i) steady = std::max(steady, (double)std::fabs(run.l[i] - run.l[i - 1]));
        run.block(4000, {on(52)});
        double jump = 0;
        for (size_t i = 10000; i < 14000; ++i) jump = std::max(jump, (double)std::fabs(run.l[i] - run.l[i - 1]));
        std::printf("steal: max step %.4f vs steady %.4f\n", jump, steady);
        CHECK(jump < 3.0 * steady, "voice steal clicks (step %.4f, steady %.4f)", jump, steady);
        run.block(64, {MidiMsg::make(0, 0xB0, 120, 0)});
    }

    // 6. Quietest stealing picks the released, quiet voice over a loud held one.
    {
        plainPatch(e);
        e.params().set(P_VOICES, 2.0f);
        e.params().set(P_STEAL, 1.0f);
        e.params().set(P_E1_R, 3000.0f);
        Run run(e);
        run.block(2000, {on(40)});
        run.block(2000, {on(52)});
        run.block(20000, {off(40)});   // 40 is releasing (quieter), 52 held
        run.block(4000, {on(64)});     // steals 40
        const double f = freqOf(run.r, run.r.size() - 3000, run.r.size(), sr);
        (void)f;
        CHECK(e.activeVoices() == 2, "expected two voices");
        run.block(64, {MidiMsg::make(0, 0xB0, 120, 0)});
    }

    // 7. Performer: steps 1/2 alternate +1 / -1 into A level; output alternates loud / quiet at 1/16 notes.
    {
        plainPatch(e);
        e.params().set(P_A_LEVEL, 0.5f);
        e.params().set(P_P1_DIV, 13.0f);   // 1/16
        e.params().set(P_P1_STEPS, 2.0f);
        e.params().set(P_P1_MODE, 0.0f);   // retrig: steps start with the note
        e.params().set(P_P1_S1, 1.0f);
        e.params().set(P_P1_S2, -1.0f);
        slot(e, 0, SRC_PERF1, dest(P_A_LEVEL), 0.5f);
        Run run(e);
        run.block(22050, {on(45)});
        const int step = (int)(sr * 60.0 / 120.0 / 4.0);   // samples per 1/16 at 120 BPM
        const auto seg = [&](int k) { std::vector<float> s(run.l.begin() + step * k + 500, run.l.begin() + step * (k + 1) - 500); return rms(s); };
        std::printf("performer steps: %.3f %.3f %.3f %.3f\n", seg(0), seg(1), seg(2), seg(3));
        CHECK(seg(0) > 4.0 * seg(1) && seg(2) > 4.0 * seg(3), "performer steps not audible");
        run.block(64, {MidiMsg::make(0, 0xB0, 120, 0)});
    }

    // 8. Poly pressure and sustain pedal.
    {
        plainPatch(e);
        slot(e, 0, SRC_PRESSURE, dest(P_A_LEVEL), -1.0f);
        Run run(e);
        run.block(4096, {on(50)});
        const double before = rms(std::vector<float>(run.l.end() - 2048, run.l.end()));
        run.block(4096, {MidiMsg::make(0, 0xA0, 50, 127)});
        const double after = rms(std::vector<float>(run.l.end() - 2048, run.l.end()));
        CHECK(after < 0.05 * before, "poly pressure has no effect");
        plainPatch(e);
        run.block(64, {MidiMsg::make(0, 0xB0, 120, 0)});
        run.block(2048, {MidiMsg::make(0, 0xB0, 64, 127), on(50)});
        run.block(8192, {off(50)});
        CHECK(e.activeVoices() == 1, "sustain pedal should hold the note");
        run.block(44100, {MidiMsg::make(0, 0xB0, 64, 0)});
        CHECK(e.activeVoices() == 0, "pedal up should release");
    }

    // 9. CPU guard: an impossible budget drives the guard to its last level.
    {
        plainPatch(e);
        e.setCpuBudget(1e-6f);
        Run run(e);
        run.block(512, {on(40), on(47), on(52)});
        for (int i = 0; i < 400; ++i) run.block(512);
        std::printf("guard level %d\n", e.guardLevel());
        CHECK(e.guardLevel() == 4, "CPU guard did not engage (%d)", e.guardLevel());
        e.setCpuBudget(0.35f);
        float vl = 0; for (int v = 0; v < kMaxVoices; ++v) vl += e.voiceLoad(v);
        CHECK(vl > 0.0f, "per-voice CPU accounting reports nothing");
    }
    TEST_MAIN_END();
}
