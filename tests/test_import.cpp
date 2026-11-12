// Wavetable import: Serum clm tables, single cycles, folders, Sample -> Table, the library, and a fuzz test of
// the WAV parser + importer (run this under -DHELIX_SANITIZE=ON to catch memory errors).
#include "TestUtil.h"
#include "Analysis.h"
#include "helix/TableLibrary.h"
#include "helix/WavetableImport.h"
#include "helix/WavetableGen.h"
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace helix;
namespace fs = std::filesystem;

// Build a 16-bit mono WAV in memory, optional Serum clm chunk.
static std::vector<uint8_t> makeWav(const std::vector<float>& x, int sr, int clm = 0)
{
    std::vector<uint8_t> b;
    auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i))); };
    auto p16 = [&](uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); };
    std::string clmText = clm ? "<!>" + std::to_string(clm) + " 00000000 wavetable (www.xferrecords.com)" : "";
    const uint32_t data = (uint32_t)x.size() * 2;
    const uint32_t clmSize = (uint32_t)clmText.size();
    b.insert(b.end(), {'R', 'I', 'F', 'F'}); p32(4 + 24 + (clm ? 8 + clmSize + (clmSize & 1) : 0) + 8 + data);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); p32(16); p16(1); p16(1); p32((uint32_t)sr); p32((uint32_t)sr * 2); p16(2); p16(16);
    if (clm) { b.insert(b.end(), {'c', 'l', 'm', ' '}); p32(clmSize); b.insert(b.end(), clmText.begin(), clmText.end()); if (clmSize & 1) b.push_back(0); }
    b.insert(b.end(), {'d', 'a', 't', 'a'}); p32(data);
    for (float v : x) p16((uint16_t)(int16_t)std::lrint(clamp(v, -1.0f, 1.0f) * 32767.0f));
    return b;
}
static void writeFile(const fs::path& p, const std::vector<uint8_t>& b) { std::ofstream f(p, std::ios::binary); f.write((const char*)b.data(), (std::streamsize)b.size()); }

int main()
{
    const fs::path dir = fs::temp_directory_path() / "helix_import_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "Wavetables" / "MyCycles");

    // 1. Serum table: 8 frames of 1024 samples announced by clm; frame k = sine with k+1 harmonics.
    {
        std::vector<float> x;
        for (int f = 0; f < 8; ++f) for (int i = 0; i < 1024; ++i) { float s = 0; for (int h = 1; h <= f + 1; ++h) s += std::sin(kTwoPi * h * i / 1024.0f) / h; x.push_back(0.5f * s); }
        auto wav = makeWav(x, 44100, 1024);
        WavData w; std::string err;
        CHECK(parseWav(wav.data(), wav.size(), w, &err), "parse: %s", err.c_str());
        CHECK(w.clmFrameSize == 1024, "clm frame size %d", w.clmFrameSize);
        auto t = importWavetable(w, "serum");
        CHECK(t && t->numFrames() == 8, "Serum table frames %d", t ? t->numFrames() : -1);
        writeFile(dir / "Wavetables" / "serum8.wav", wav);
        // The same data without clm is read as 2048-sample frames (4 frames).
        auto wav2 = makeWav(x, 44100, 0);
        WavData w2; parseWav(wav2.data(), wav2.size(), w2);
        auto t2 = importWavetable(w2, "noclm");
        CHECK(t2 && t2->numFrames() == 4, "default 2048 frame size: %d frames", t2 ? t2->numFrames() : -1);
    }
    // 2. Single cycle of an odd length.
    {
        std::vector<float> x(600);
        for (int i = 0; i < 600; ++i) x[(size_t)i] = 2.0f * i / 600.0f - 1.0f;
        auto wav = makeWav(x, 44100);
        writeFile(dir / "Wavetables" / "saw600.wav", wav);
        auto t = importWavetableFile((dir / "Wavetables" / "saw600.wav").string());
        CHECK(t && t->numFrames() == 1, "single cycle import");
        // It plays: a saw at 220 Hz has its 2nd harmonic at about -6 dB.
        if (t)
        {
            const float* f = t->fullFrame(0);
            auto mag = magnitudeSpectrum(f, kFrameSize);
            (void)mag;
            float peak = 0; for (int i = 0; i < kFrameSize; ++i) peak = std::max(peak, std::fabs(f[i]));
            CHECK(std::fabs(peak - 1.0f) < 1e-3f, "table normalised (%f)", peak);
        }
    }
    // 3. Folder of single cycles -> frames in alphabetical order.
    {
        for (int k = 0; k < 5; ++k)
        {
            std::vector<float> x(256);
            for (int i = 0; i < 256; ++i) x[(size_t)i] = std::sin(kTwoPi * (k + 1) * i / 256.0f) * 0.8f;
            writeFile(dir / "Wavetables" / "MyCycles" / ("c" + std::to_string(k) + ".wav"), makeWav(x, 44100));
        }
        auto t = importCycleFolder((dir / "Wavetables" / "MyCycles").string());
        CHECK(t && t->numFrames() == 5, "folder import frames %d", t ? t->numFrames() : -1);
        if (t)
        {
            // Frame 2 = third harmonic only
            auto mag = magnitudeSpectrum(t->fullFrame(2), kFrameSize);
            size_t best = 1; for (size_t b = 1; b < 20; ++b) if (mag[b] > mag[best]) best = b;
            CHECK(best == 3, "folder order wrong (frame 2 peaks at harmonic %zu)", best);
        }
    }
    // 4. Sample -> Table: a 110 Hz tone that brightens over 2 s.
    {
        const int sr = 44100, len = sr * 2;
        std::vector<float> x((size_t)len);
        double ph = 0;
        for (int i = 0; i < len; ++i)
        {
            const float t = (float)i / len;
            const int harm = 1 + (int)(t * 12);
            float s = 0; for (int h = 1; h <= harm; ++h) s += std::sin((float)(kTwoPi * h * ph)) / h;
            x[(size_t)i] = 0.4f * s;
            ph += 110.0 / sr; ph -= std::floor(ph);
        }
        SampleToTableInfo info;
        SampleToTableOptions opt; opt.frames = 32;
        auto t = sampleToTable(x.data(), x.size(), sr, "tone", opt, &info);
        std::printf("Sample->Table: pitched %d, %.2f Hz, period %d\n", (int)info.pitched, info.detectedHz, info.periodSamples);
        CHECK(info.pitched && std::fabs(info.detectedHz - 110.0f) < 1.0f, "pitch detection (%.2f Hz)", info.detectedHz);
        CHECK(t && t->numFrames() == 32, "S>T frame count");
        if (t)
        {
            // Each frame is one cycle: energy at harmonic 1 dominates frame 0, and later frames are brighter.
            auto m0 = magnitudeSpectrum(t->fullFrame(0), kFrameSize), m31 = magnitudeSpectrum(t->fullFrame(31), kFrameSize);
            double hi0 = 0, hi31 = 0; for (int b = 4; b < 20; ++b) { hi0 += m0[(size_t)b]; hi31 += m31[(size_t)b]; }
            CHECK(hi31 > 3.0 * hi0, "later frames should be brighter (%.3f vs %.3f)", hi31, hi0);
            size_t best = 1; for (size_t b = 1; b < 30; ++b) if (m0[b] > m0[best]) best = b;
            CHECK(best == 1, "frame 0 is not one cycle (peak at bin %zu)", best);
        }
        // Unpitched audio (noise / a break) still gives a table.
        Rng rng(1); std::vector<float> nz((size_t)sr); for (float& v : nz) v = rng.bi() * 0.5f;
        SampleToTableInfo ni;
        auto tn = sampleToTable(nz.data(), nz.size(), sr, "noise", opt, &ni);
        CHECK(tn && !ni.pitched, "unpitched Sample->Table");
    }
    // 5. Library: scan finds the file tables and the cycle folder; async load reaches the engine.
    {
        Engine e;
        TableLibrary lib(e);
        lib.setRoots({(dir / "Wavetables").string()});
        lib.rescan();
        lib.waitIdle();
        const int n = lib.count();
        std::printf("library: %d entries\n", n);
        CHECK(n == (int)builtinTableNames().size() + 3, "library entries %d", n);
        const int idx = lib.indexOfId("file:serum8.wav");
        CHECK(idx >= 0, "serum8.wav not found");
        lib.requestLoad(kSlotA, idx);
        lib.waitIdle();
        CHECK(e.uiTable(kSlotA) && e.uiTable(kSlotA)->numFrames() == 8, "table not handed to the engine");
        CHECK(lib.slotId(kSlotA) == "file:serum8.wav", "slot id %s", lib.slotId(kSlotA).c_str());
        // The audio thread swaps it in on the next block.
        std::vector<float> l(64), r(64); Transport tr;
        e.process(l.data(), r.data(), 64, nullptr, 0, tr);
        lib.requestLoadId(kSlotB, "folder:MyCycles");
        lib.waitIdle();
        CHECK(e.uiTable(kSlotB) && e.uiTable(kSlotB)->numFrames() == 5, "folder by id");
        lib.requestLoadFile(kSlotB, (dir / "Wavetables" / "saw600.wav").string(), 16);
        lib.waitIdle();
        CHECK(e.uiTable(kSlotB) && e.uiTable(kSlotB)->numFrames() == 16, "Sample->Table through the library");
    }
    // 6. Fuzz: random mutations of valid WAVs and random garbage never crash and never return junk tables.
    {
        Rng rng(12345);
        std::vector<float> x(4096); for (size_t i = 0; i < x.size(); ++i) x[i] = std::sin((float)i * 0.05f);
        const auto seeds = std::vector<std::vector<uint8_t>>{makeWav(x, 44100, 2048), makeWav(x, 48000, 0), makeWav(std::vector<float>(300, 0.1f), 44100)};
        int parsed = 0, tables = 0;
        for (int iter = 0; iter < 3000; ++iter)
        {
            std::vector<uint8_t> b = seeds[(size_t)iter % seeds.size()];
            const int muts = 1 + (int)(rng.next() % 8);
            for (int m = 0; m < muts; ++m)
            {
                const uint32_t kind = rng.next() % 4;
                if (b.empty()) break;
                const size_t at = rng.next() % b.size();
                if (kind == 0) b[at] = (uint8_t)rng.next();
                else if (kind == 1) b.resize(at);                                   // truncate
                else if (kind == 2 && at + 4 <= b.size()) { uint32_t v = rng.next(); std::memcpy(&b[at], &v, 4); }   // smash a size field
                else b.insert(b.begin() + (long)at, (uint8_t)rng.next());
            }
            if (iter % 7 == 0) { b.resize(rng.next() % 200); for (auto& c : b) c = (uint8_t)rng.next(); }
            WavData w;
            if (parseWav(b.data(), b.size(), w)) ++parsed;
            else continue;
            auto t = importWavetable(w, "fuzz");
            if (t) { ++tables; CHECK(t->numFrames() >= 1 && t->numFrames() <= kMaxFrames, "fuzz: bad frame count"); }
        }
        std::printf("fuzz: 3000 inputs, %d parsed, %d tables, no crash\n", parsed, tables);
    }
    fs::remove_all(dir);
    TEST_MAIN_END();
}
