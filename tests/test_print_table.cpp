// Print to wavetable: slices are whole, phase-aligned cycles; a sweep changes the table from first to last frame;
// the WAV comes back as the same table; silence is refused.
#include "TestUtil.h"
#include "helix/PrintTable.h"
#include "helix/WavetableImport.h"
#include <cmath>
#include <filesystem>
#include <vector>

using namespace helix;

static double frameDiff(const std::vector<float>& t, int a, int b)
{
    double d = 0, e = 0;
    for (int i = 0; i < kFrameSize; ++i)
    {
        const float x = t[(size_t)a * kFrameSize + i], y = t[(size_t)b * kFrameSize + i];
        d += (x - y) * (x - y); e += x * x;
    }
    return d / std::max(e, 1e-12);
}

// share of the frame's energy above the 6th harmonic (one frame = one cycle, so DFT bin k = harmonic k)
static double bright(const std::vector<float>& t, int f)
{
    double hi = 0, all = 0;
    for (int k = 1; k <= 128; ++k)
    {
        double re = 0, im = 0;
        for (int i = 0; i < kFrameSize; ++i)
        {
            const double ph = 2.0 * M_PI * k * i / kFrameSize;
            re += t[(size_t)f * kFrameSize + i] * std::cos(ph); im -= t[(size_t)f * kFrameSize + i] * std::sin(ph);
        }
        const double e = re * re + im * im;
        all += e; if (k > 6) hi += e;
    }
    return hi / std::max(all, 1e-12);
}

int main()
{
    // 1. A static patch: every slice is the same cycle (period lands exactly on kFrameSize).
    Preset p;
    p.params[P_RS_LOCK] = 1.0f;
    p.params[P_A_POS] = 0.6f;
    PrintOptions o;
    o.source = kPrintTime;
    o.frames = 16;
    o.seconds = 1.0f;
    o.effects = true;
    std::vector<float> raw;
    std::string err;
    WavetablePtr t = printToTable(p, nullptr, nullptr, o, "test", &raw, &err);
    CHECK(t != nullptr, "static print failed: %s", err.c_str());
    CHECK(t->numFrames() == 16, "frames %d", t->numFrames());
    CHECK(raw.size() == (size_t)16 * kFrameSize, "raw size");
    CHECK(frameDiff(raw, 4, 15) < 1e-3, "static patch slices differ (%.5f): cycles not aligned", frameDiff(raw, 4, 15));

    // 2. Growl sweep on a dark patch: the last frame is brighter than the first.
    Preset g = p;
    g.params[P_F1_CUTOFF] = 300.0f;
    g.params[P_B_ON] = 1.0f;
    o.source = kPrintGrowl;
    o.frames = 32;
    WavetablePtr tg = printToTable(g, nullptr, nullptr, o, "growl", &raw, &err);
    CHECK(tg != nullptr, "growl print failed: %s", err.c_str());
    CHECK(bright(raw, 31) > bright(raw, 0) * 1.5, "growl sweep did not brighten (%g vs %g)", bright(raw, 31), bright(raw, 0));
    CHECK(frameDiff(raw, 0, 31) > 0.05, "sweep did not change the table");

    // 3. WAV round trip.
    const auto dir = std::filesystem::temp_directory_path() / "helix_print_test";
    std::filesystem::remove_all(dir);
    const std::string path = nextPrintPath(dir.string());
    CHECK(!path.empty() && path.find("Print 001.wav") != std::string::npos, "path %s", path.c_str());
    CHECK(writePrintWav(path, raw), "write failed");
    CHECK(nextPrintPath(dir.string()).find("Print 002.wav") != std::string::npos, "numbering");
    WavetablePtr back = importWavetableFile(path, &err);
    CHECK(back && back->numFrames() == 32, "re-import: %s", err.c_str());
    double d = 0;
    for (int i = 0; i < kFrameSize; ++i) d = std::max(d, (double)std::fabs(back->fullFrame(31)[i] - tg->fullFrame(31)[i]));
    CHECK(d < 1e-3, "re-imported table differs (%g)", d);
    std::filesystem::remove_all(dir);

    // 4. Silence is refused; source map covers every source.
    Preset s = p;
    s.params[P_A_ON] = 0.0f;
    s.params[P_B_ON] = 0.0f;
    o.source = kPrintTime;
    CHECK(printToTable(s, nullptr, nullptr, o, "silent", nullptr, &err) == nullptr, "silent print accepted");
    CHECK(choices::printSource.size() == (size_t)kNumPrintSources, "printSource list size");
    for (int k = 1; k < kNumPrintSources; ++k) CHECK(printSourceParam(k) >= 0, "source %d has no param", k);
    CHECK(printNote(10) == 24 && printNote(60) == 42, "note clamp");
    TEST_MAIN_END();
}
