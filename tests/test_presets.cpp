// Presets: JSON round trip, render null test (save -> load renders bit-identically), Mutate, and a golden
// render compare for every factory preset (tests/golden/factory.json; HELIX_UPDATE_GOLDEN=1 rewrites it).
#include "TestUtil.h"
#include "Analysis.h"
#include "helix/Preset.h"
#include "helix/TableLibrary.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

using namespace helix;

// The reference performance for a preset: a riff for basses and leads, a chord for the rest.
static std::vector<std::pair<double, MidiMsg>> performance(const Preset& p)
{
    std::vector<std::pair<double, MidiMsg>> ev;
    const bool chordy = p.category == "Pad" || p.category == "Atmos" || p.category == "Stab";
    if (chordy)
    {
        for (int n : {48, 55, 60, 63}) ev.push_back({0.05, MidiMsg::make(0, 0x90, (uint8_t)n, 100)});
        for (int n : {48, 55, 60, 63}) ev.push_back({2.0, MidiMsg::make(0, 0x80, (uint8_t)n, 0)});
    }
    else if (p.category == "FX")
    {
        ev.push_back({0.05, MidiMsg::make(0, 0x90, 60, 110)});
        ev.push_back({2.5, MidiMsg::make(0, 0x80, 60, 0)});
    }
    else
    {
        const int notes[] = {36, 36, 39, 41, 43, 41, 39, 34};
        const double step = 60.0 / 170.0 / 2.0;
        for (int i = 0; i < 8; ++i)
        {
            const uint8_t n = (uint8_t)(notes[i] + (p.category == "Lead" ? 24 : 0));
            ev.push_back({0.05 + i * step, MidiMsg::make(0, 0x90, n, (uint8_t)(i % 4 == 0 ? 120 : 90))});
            ev.push_back({0.05 + i * step + step * 0.9, MidiMsg::make(0, 0x80, n, 0)});
        }
    }
    return ev;
}

static Stereo renderPreset(const Preset& p, double seconds = 3.0, int block = 128)
{
    const double sr = 44100.0;
    Engine e;
    e.prepare(sr, block);
    TableLibrary lib(e);
    applyPreset(p, e, &lib);
    lib.waitIdle();
    auto ev = performance(p);
    std::sort(ev.begin(), ev.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Stereo out;
    const size_t total = (size_t)(seconds * sr);
    std::vector<float> l((size_t)block), r((size_t)block);
    std::vector<MidiMsg> bev;
    size_t ei = 0;
    Transport tr; tr.valid = true; tr.playing = true; tr.bpm = 170.0;
    for (size_t pos = 0; pos < total; pos += (size_t)block)
    {
        bev.clear();
        while (ei < ev.size() && (size_t)(ev[ei].first * sr) < pos + (size_t)block)
        {
            MidiMsg m = ev[ei].second;
            const size_t at = (size_t)(ev[ei].first * sr);
            m.offset = at > pos ? (int)(at - pos) : 0;
            bev.push_back(m);
            ++ei;
        }
        tr.ppq = (double)pos / sr * tr.bpm / 60.0;
        e.process(l.data(), r.data(), block, bev.data(), (int)bev.size(), tr);
        out.l.insert(out.l.end(), l.begin(), l.end());
        out.r.insert(out.r.end(), r.begin(), r.end());
    }
    return out;
}

// Fingerprint: RMS per 100 ms (both channels, dB) and the spectral centroid of each 0.5 s.
static Json fingerprint(const Stereo& s)
{
    Json j = Json::object();
    Json rmsL = Json::array(), rmsR = Json::array(), cent = Json::array();
    const size_t hop = 4410;
    for (size_t a = 0; a + hop <= s.l.size(); a += hop)
    {
        rmsL.push(std::round(gainToDb((float)rms(std::vector<float>(s.l.begin() + (long)a, s.l.begin() + (long)(a + hop)))) * 100.0) / 100.0);
        rmsR.push(std::round(gainToDb((float)rms(std::vector<float>(s.r.begin() + (long)a, s.r.begin() + (long)(a + hop)))) * 100.0) / 100.0);
    }
    for (size_t a = 0; a + 16384 <= s.l.size(); a += 22050)
    {
        auto mag = magnitudeSpectrum(s.l.data() + a, 16384);
        double num = 0, den = 0;
        for (size_t b = 1; b < mag.size(); ++b) { num += b * 44100.0 / 16384.0 * mag[b]; den += mag[b]; }
        cent.push(den > 1e-9 ? std::round(num / den) : 0.0);
    }
    j.set("rms_l", rmsL); j.set("rms_r", rmsR); j.set("centroid", cent);
    return j;
}

int main()
{
    const auto& factory = factoryPresets();
    CHECK(factory.size() == 24, "expected 24 factory presets, have %zu", factory.size());
    std::set<std::string> names;
    for (const Preset& p : factory) names.insert(p.name);
    CHECK(names.size() == factory.size(), "factory preset names must be unique");

    // 1. JSON null test: save -> load -> save gives identical text, and identical values.
    for (const Preset& p : factory)
    {
        const std::string a = p.toString();
        Preset q; std::string err;
        CHECK(Preset::fromString(a, q, &err), "%s: %s", p.name.c_str(), err.c_str());
        CHECK(q.toString() == a, "%s: JSON round trip differs", p.name.c_str());
        bool same = true;
        for (int i = 0; i < kNumParams; ++i) same &= std::memcmp(&p.params[i], &q.params[i], sizeof(float)) == 0;
        CHECK(same, "%s: parameter values changed in the round trip", p.name.c_str());
    }

    // 2. Render null test: the original and the reloaded preset render bit-identically.
    for (int k : {0, 2, 10, 15, 22})
    {
        const Preset& p = factory[(size_t)k];
        Preset q; Preset::fromString(p.toString(), q);
        Stereo a = renderPreset(p, 1.5), b = renderPreset(q, 1.5);
        CHECK(a.l == b.l && a.r == b.r, "%s: reloaded preset does not null against the original", p.name.c_str());
        // Capture from a live engine nulls too.
        Engine e; TableLibrary lib(e); applyPreset(p, e, &lib); lib.waitIdle();
        Preset c = capturePreset(e, &lib);
        c.name = p.name; c.category = p.category; c.author = p.author; c.comment = p.comment;
        CHECK(c.toString() == p.toString(), "%s: capture after apply differs", p.name.c_str());
    }

    // 3. Mutate 10%: every continuous parameter moves by at most 10% (normalised), structure stays, deterministic.
    {
        const Preset& p = factory[1];
        Preset m = p; mutatePreset(m, 0.1f, 42);
        Preset m2 = p; mutatePreset(m2, 0.1f, 42);
        int moved = 0;
        for (int i = 0; i < kNumParams; ++i)
        {
            const ParamDef& d = paramDef(i);
            const float dn = std::fabs(d.toNormalised(m.params[i]) - d.toNormalised(p.params[i]));
            CHECK(dn <= 0.1001f, "mutate moved %s by %.3f", d.id, dn);
            if (dn > 1e-6f) ++moved;
            if (d.kind == ParamKind::Choice || d.kind == ParamKind::Bool || d.kind == ParamKind::Int || i == P_MASTER)
                CHECK(m.params[i] == p.params[i], "mutate changed structural param %s", d.id);
        }
        std::printf("mutate 10%%: %d parameters moved\n", moved);
        CHECK(moved > 100, "mutate moved too few parameters (%d)", moved);
        CHECK(m.toString() == m2.toString(), "mutate is not deterministic for a seed");
        Stereo s = renderPreset(m, 1.0);
        CHECK(allFinite(s.l) && rms(s.l) > 1e-3, "mutated preset is broken");
    }

    // 4. Golden render compare.
    const std::string path = std::string(HELIX_SOURCE_DIR) + "/tests/golden/factory.json";
    const bool update = std::getenv("HELIX_UPDATE_GOLDEN") != nullptr;
    Json golden;
    {
        std::ifstream f(path);
        std::stringstream ss; ss << f.rdbuf();
        if (!update && !Json::parse(ss.str(), golden)) { std::printf("no golden file at %s: run with HELIX_UPDATE_GOLDEN=1\n", path.c_str()); ++g_failures; }
    }
    Json fresh = Json::object();
    for (const Preset& p : factory)
    {
        Stereo s = renderPreset(p);
        CHECK(allFinite(s.l) && allFinite(s.r), "%s renders NaN/Inf", p.name.c_str());
        float peak = 0; for (float v : s.l) peak = std::max(peak, std::fabs(v));
        CHECK(peak < 1.0f, "%s clips (peak %.2f)", p.name.c_str(), peak);
        if (std::getenv("HELIX_PRINT_PEAKS")) std::printf("PEAK %s %.2f\n", p.name.c_str(), gainToDb(peak));
        const Json fp = fingerprint(s);
        fresh.set(p.name, fp);
        if (update || golden.isNull()) continue;
        const Json& g = golden.get(p.name);
        if (g.isNull()) { CHECK(false, "%s missing from the golden file", p.name.c_str()); continue; }
        double worstDb = 0, worstCent = 0;
        for (const char* ch : {"rms_l", "rms_r"})
            for (size_t i = 0; i < fp.get(ch).size() && i < g.get(ch).size(); ++i)
            {
                const double a = fp.get(ch)[i].number(), b = g.get(ch)[i].number();
                if (a < -60.0 && b < -60.0) continue;   // both silent enough
                worstDb = std::max(worstDb, std::fabs(a - b));
            }
        for (size_t i = 0; i < fp.get("centroid").size() && i < g.get("centroid").size(); ++i)
        {
            const double a = fp.get("centroid")[i].number(), b = g.get("centroid")[i].number();
            if (b > 50.0) worstCent = std::max(worstCent, std::fabs(a - b) / b);
        }
        CHECK(worstDb < 1.0 && worstCent < 0.08, "%s differs from golden: %.2f dB, centroid %.1f%%", p.name.c_str(), worstDb, worstCent * 100.0);
    }
    if (update)
    {
        std::ofstream(path) << fresh.dump(1);
        std::printf("golden file written: %s\n", path.c_str());
    }
    TEST_MAIN_END();
}
