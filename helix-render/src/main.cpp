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
//     --float            write 32-bit float WAV
//     --bench            benchmark instead of writing audio (see docs/PERF.md)
#include "helix/Engine.h"
#include "helix/Simd.h"
#include "helix/Math.h"
#include "helix/WavIO.h"
#include "helix/WavetableGen.h"
#include "MidiFile.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace helix;

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
    bool float32 = false, bench = false;
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

    Engine engine;
    engine.prepare(o.sr, o.block);
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
        if (!o.bench) for (int i = 0; i < n; ++i) { out.push_back(l[(size_t)i]); out.push_back(r[(size_t)i]); }
        engine.collectGarbage();
    }

    const double audioSecs = (double)total / o.sr;
    if (o.bench)
    {
        std::printf("bench: %.1f s audio in %.3f s CPU -> %.2f%% of one core (worst block %.1f%%), simd=%s\n",
                    audioSecs, busy, 100.0 * busy / audioSecs, 100.0 * worst, simdBackendName());
        return 0;
    }
    if (!writeWavFile(o.out, out.data(), out.size() / 2, 2, o.sr, o.float32)) { std::fprintf(stderr, "cannot write %s\n", o.out.c_str()); return 1; }
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    std::printf("wrote %s: %.2f s, peak %.2f dBFS, render %.1f%% of real time\n", o.out.c_str(), audioSecs,
                gainToDb(peak), 100.0 * busy / audioSecs);
    return 0;
}
