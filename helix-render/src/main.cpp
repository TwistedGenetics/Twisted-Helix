// helix-render: render a preset (+ a MIDI file) to WAV, or benchmark the engine.
//
//   helix-render [options]
//     --midi FILE        MIDI file to play (default: a built-in test riff)
//     --out FILE         output WAV (default: helix.wav)
//     --seconds S        length (default: MIDI length + 2 s, or 8 s)
//     --sr RATE          sample rate (default 44100)
//     --block N          host block size (default 128)
//     --set id=value     set a parameter (plain value), repeatable
//     --table A=N        built-in table N for slot A or B
//     --preset FILE      load a preset (.json)
//     --factory NAME|N   load a factory preset by name or index
//     --mutate PCT       mutate the preset by PCT percent (with --seed N)
//     --save-preset FILE write the (possibly mutated) preset
//     --list-factory     list the factory presets
//     --write-factory DIR  write the factory presets as DIR/<Category>/<Name>.json
//     --tables DIR       scan DIR for wavetables (presets that use user tables)
//     --float            write 32-bit float WAV
//     --bench            benchmark the full-spec patch instead of writing audio (see docs/PERF.md)
#include "helix/Engine.h"
#include "helix/Simd.h"
#include "helix/Math.h"
#include "helix/WavIO.h"
#include "helix/WavetableGen.h"
#include "helix/Preset.h"
#include "helix/ModSystem.h"
#include "helix/TableLibrary.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "MidiFile.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace helix;

// The HELIX CPU budget scenario (brief): 8 voices x 2 oscillators x 4 unison + 2 filters + 3 FX, with
// modulation running. Held 8-note chords, re-struck every 2 s so voice starts are included.
static void benchPatch(Engine& e)
{
    auto& p = e.params();
    p.resetToDefaults();
    p.set(P_A_ON, 1); p.set(P_A_POS, 0.66f); p.set(P_A_UNISON, 4); p.set(P_A_DETUNE, 0.25f);
    p.set(P_B_ON, 1); p.set(P_B_POS, 0.4f); p.set(P_B_UNISON, 4); p.set(P_B_DETUNE, 0.2f); p.set(P_B_TO_F1, 1.0f); p.set(P_B_TO_F2, 1.0f);
    p.set(P_A_TO_F2, 1.0f);
    p.set(P_F1_ON, 1); p.set(P_F1_TYPE, 1); p.set(P_F1_CUTOFF, 1500); p.set(P_F1_RES, 0.3f);
    p.set(P_F2_ON, 1); p.set(P_F2_TYPE, 2); p.set(P_F2_CUTOFF, 900); p.set(P_FILTER_BLEND, 0.5f);
    p.set(P_E1_S, 1.0f);
    // LFO 1 -> F1 cutoff, Env 2 -> A position: the modulation path is live
    p.set(P_M1_SRC, 5); p.set(P_M1_DST, (float)[]{ for (int d = 0; d < numModDests(); ++d) if (modDestParam(d) == P_F1_CUTOFF) return d; return 0; }()); p.set(P_M1_AMT, 0.3f);
    p.set(P_M2_SRC, 2); p.set(P_M2_DST, (float)[]{ for (int d = 0; d < numModDests(); ++d) if (modDestParam(d) == P_A_POS) return d; return 0; }()); p.set(P_M2_AMT, 0.5f);
    // 3 FX: distortion, delay, reverb
    p.set(P_DIST_ON, 1); p.set(P_DLY_ON, 1); p.set(P_REV_ON, 1);
    p.set(P_VOICES, 8);
}

static std::vector<TimedMidi> benchNotes(double seconds)
{
    std::vector<TimedMidi> ev;
    const int chord[8] = {36, 43, 48, 51, 55, 58, 60, 63};
    for (double t = 0.0; t < seconds; t += 2.0)
    {
        for (int n : chord) ev.push_back({t, {0x90, (uint8_t)n, 100}});
        for (int n : chord) ev.push_back({t + 1.95, {0x80, (uint8_t)n, 0}});
    }
    return ev;
}

static std::vector<TimedMidi> testRiff()
{
    // Rolling jungle bass riff: notes in seconds at 170 BPM-ish.
    std::vector<TimedMidi> ev;
    const int notes[] = {36, 36, 39, 36, 43, 41, 39, 34};
    const double step = 60.0 / 170.0 / 2.0;
    for (int i = 0; i < 16; ++i)
    {
        const double t = i * step;
        const uint8_t n = (uint8_t)notes[i % 8];
        ev.push_back({t, {0x90, n, (uint8_t)(i % 4 == 0 ? 120 : 90)}});
        ev.push_back({t + step * 0.8, {0x80, n, 0}});
    }
    // A held chord at the end for the release tail
    const double t = 16 * step;
    for (int n : {48, 55, 60, 63}) ev.push_back({t, {0x90, (uint8_t)n, 100}});
    for (int n : {48, 55, 60, 63}) ev.push_back({t + 1.5, {0x80, (uint8_t)n, 0}});
    return ev;
}

struct Options
{
    std::string midi, out = "helix.wav";
    double seconds = -1;
    int sr = 44100, block = 128;
    bool float32 = false, bench = false, listFactory = false;
    std::string preset, factory, savePreset, writeFactory, tablesDir;
    float mutate = 0.0f;
    uint32_t seed = 1;
    std::vector<std::pair<std::string, float>> sets;
    std::vector<std::pair<int, int>> tables;
};

static bool parseArgs(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--midi") { const char* v = next(); if (!v) return false; o.midi = v; }
        else if (a == "--out") { const char* v = next(); if (!v) return false; o.out = v; }
        else if (a == "--seconds") { const char* v = next(); if (!v) return false; o.seconds = std::atof(v); }
        else if (a == "--sr") { const char* v = next(); if (!v) return false; o.sr = std::atoi(v); }
        else if (a == "--block") { const char* v = next(); if (!v) return false; o.block = std::atoi(v); }
        else if (a == "--float") o.float32 = true;
        else if (a == "--preset") { const char* v = next(); if (!v) return false; o.preset = v; }
        else if (a == "--factory") { const char* v = next(); if (!v) return false; o.factory = v; }
        else if (a == "--mutate") { const char* v = next(); if (!v) return false; o.mutate = (float)std::atof(v) / 100.0f; }
        else if (a == "--seed") { const char* v = next(); if (!v) return false; o.seed = (uint32_t)std::strtoul(v, nullptr, 10); }
        else if (a == "--save-preset") { const char* v = next(); if (!v) return false; o.savePreset = v; }
        else if (a == "--write-factory") { const char* v = next(); if (!v) return false; o.writeFactory = v; }
        else if (a == "--tables") { const char* v = next(); if (!v) return false; o.tablesDir = v; }
        else if (a == "--list-factory") o.listFactory = true;
        else if (a == "--bench") o.bench = true;
        else if (a == "--set")
        {
            const char* v = next(); if (!v) return false;
            std::string s = v; size_t eq = s.find('=');
            if (eq == std::string::npos) return false;
            o.sets.push_back({s.substr(0, eq), (float)std::atof(s.c_str() + eq + 1)});
        }
        else if (a == "--table")
        {
            const char* v = next(); if (!v || std::strlen(v) < 3 || v[1] != '=') return false;
            o.tables.push_back({v[0] == 'B' || v[0] == 'b' ? 1 : 0, std::atoi(v + 2)});
        }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return false; }
    }
    return o.sr >= 8000 && o.sr <= 192000 && o.block >= 1 && o.block <= 8192;
}

int main(int argc, char** argv)
{
    Options o;
    if (!parseArgs(argc, argv, o))
    {
        std::fprintf(stderr, "usage: helix-render [--midi f.mid] [--out f.wav] [--seconds s] [--sr r] [--block n]\n"
                             "                    [--set id=value]... [--table A=n] [--float] [--bench]\n");
        return 2;
    }

    const auto& factory = factoryPresets();
    if (o.listFactory)
    {
        for (size_t i = 0; i < factory.size(); ++i) std::printf("%2zu  %-8s %s\n", i, factory[i].category.c_str(), factory[i].name.c_str());
        return 0;
    }
    if (!o.writeFactory.empty())
    {
        namespace fs = std::filesystem;
        for (const Preset& p : factory)
        {
            const fs::path dir = fs::path(o.writeFactory) / p.category;
            fs::create_directories(dir);
            std::string file = p.name;
            for (char& c : file) if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '-';
            std::ofstream(dir / (file + ".json")) << p.toString();
        }
        std::printf("wrote %zu presets to %s\n", factory.size(), o.writeFactory.c_str());
        return 0;
    }

    Engine engine;
    engine.prepare(o.sr, o.block);
    engine.setRealtime(o.bench);   // offline renders never depend on CPU speed; --bench measures the real thing
    TableLibrary library(engine);
    if (!o.tablesDir.empty()) { library.setRoots({o.tablesDir}); library.rescan(); library.waitIdle(); }

    Preset preset;
    bool havePreset = false;
    if (!o.preset.empty())
    {
        std::ifstream f(o.preset);
        std::stringstream ss; ss << f.rdbuf();
        std::string err;
        if (!Preset::fromString(ss.str(), preset, &err)) { std::fprintf(stderr, "preset: %s\n", err.c_str()); return 1; }
        havePreset = true;
    }
    else if (!o.factory.empty())
    {
        char* end = nullptr;
        const long idx = std::strtol(o.factory.c_str(), &end, 10);
        if (end && *end == '\0' && idx >= 0 && idx < (long)factory.size()) { preset = factory[(size_t)idx]; havePreset = true; }
        else for (const Preset& p : factory) if (p.name == o.factory) { preset = p; havePreset = true; }
        if (!havePreset) { std::fprintf(stderr, "no factory preset '%s' (try --list-factory)\n", o.factory.c_str()); return 2; }
    }
    if (havePreset)
    {
        if (o.mutate > 0.0f) mutatePreset(preset, o.mutate, o.seed);
        applyPreset(preset, engine, &library);
        library.waitIdle();
        if (!o.savePreset.empty()) std::ofstream(o.savePreset) << preset.toString();
    }
    for (auto& t : o.tables)
        engine.setTable(t.first, makeBuiltinTable((BuiltinTable)std::max(0, std::min(t.second, (int)BuiltinTable::Count - 1))));
    for (auto& s : o.sets)
    {
        const int idx = paramIndexFromId(s.first);
        if (idx < 0) { std::fprintf(stderr, "unknown parameter id '%s'\n", s.first.c_str()); return 2; }
        engine.params().set(idx, s.second);
    }

    std::vector<TimedMidi> events;
    if (!o.midi.empty())
    {
        std::string err;
        if (!readMidiFile(o.midi, events, &err)) { std::fprintf(stderr, "MIDI: %s\n", err.c_str()); return 1; }
    }
    else if (o.bench && !havePreset)
    {
        benchPatch(engine);
        for (auto& st : o.sets) engine.params().set(paramIndexFromId(st.first), st.second);   // --set overrides
        events = benchNotes(o.seconds > 0 ? o.seconds : 20.0);
    }
    else events = testRiff();
    double len = o.seconds;
    if (len <= 0) len = (events.empty() ? 6.0 : events.back().seconds) + 2.0;

    const size_t total = (size_t)(len * o.sr);
    std::vector<float> l((size_t)o.block), r((size_t)o.block), out;
    if (!o.bench) out.reserve(total * 2);
    std::vector<MidiMsg> blockEv;
    blockEv.reserve(1024);
    size_t ei = 0;
    Transport tr; tr.valid = true; tr.playing = true; tr.bpm = 170.0;

    double busy = 0.0, worst = 0.0;
    std::vector<double> loads;
    loads.reserve(total / (size_t)o.block + 1);
    for (size_t pos = 0; pos < total; pos += (size_t)o.block)
    {
        const int n = (int)std::min((size_t)o.block, total - pos);
        blockEv.clear();
        while (ei < events.size() && (size_t)(events[ei].seconds * o.sr) < pos + (size_t)n)
        {
            const size_t at = (size_t)(events[ei].seconds * o.sr);
            MidiMsg m; m.offset = at > pos ? (int)(at - pos) : 0;
            std::memcpy(m.data, events[ei].data, 3);
            blockEv.push_back(m);
            ++ei;
        }
        tr.ppq = (double)pos / o.sr * tr.bpm / 60.0;
        const auto t0 = std::chrono::steady_clock::now();
        engine.process(l.data(), r.data(), n, blockEv.data(), (int)blockEv.size(), tr);
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        busy += dt;
        worst = std::max(worst, dt / ((double)n / o.sr));
        loads.push_back(dt / ((double)n / o.sr));
        if (!o.bench) for (int i = 0; i < n; ++i) { out.push_back(l[(size_t)i]); out.push_back(r[(size_t)i]); }
        engine.collectGarbage();
    }

    const double audioSecs = (double)total / o.sr;
    if (o.bench)
    {
        std::vector<double> sorted = loads;
        std::sort(sorted.begin(), sorted.end());
        const double p99 = sorted.empty() ? 0.0 : sorted[std::min(sorted.size() - 1, (size_t)(sorted.size() * 0.99))];
        float voiceShare = 0.0f;
        for (int v = 0; v < kMaxVoices; ++v) voiceShare += engine.voiceLoad(v);
        std::printf("bench: %.1f s audio, block %d, sr %d, simd=%s\n", audioSecs, o.block, o.sr, simdBackendName());
        std::printf("  mean %.2f%%  p99 %.2f%%  worst %.2f%% of one core  (voices %.2f%%, guard level %d)\n",
                    100.0 * busy / audioSecs, 100.0 * p99, 100.0 * worst, 100.0 * voiceShare, engine.guardLevel());
        return 0;
    }
    if (!writeWavFile(o.out, out.data(), out.size() / 2, 2, o.sr, o.float32)) { std::fprintf(stderr, "cannot write %s\n", o.out.c_str()); return 1; }
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    std::printf("wrote %s: %.2f s, peak %.2f dBFS, render %.1f%% of real time\n", o.out.c_str(), audioSecs,
                gainToDb(peak), 100.0 * busy / audioSecs);
    return 0;
}
