// Several wavetable folders: tables in extra folders get "file:<folder>/..." ids and a group, the first folder keeps
// plain "file:" ids, and ids load the right table.
#include "TestUtil.h"
#include "helix/Engine.h"
#include "helix/PrintTable.h"
#include "helix/TableLibrary.h"
#include <filesystem>
#include <vector>

using namespace helix;
namespace fs = std::filesystem;

static void table(const fs::path& p, int frames)
{
    fs::create_directories(p.parent_path());
    std::vector<float> v((size_t)frames * kFrameSize);
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float)((i % kFrameSize) / (double)kFrameSize * 2.0 - 1.0);
    writePrintWav(p.string(), v);
}

int main()
{
    const fs::path base = fs::temp_directory_path() / "helix_table_folders";
    { std::error_code ec; fs::remove_all(base, ec); }
    table(base / "main" / "Mine.wav", 4);
    table(base / "Serum Tables" / "Bass" / "Growler.wav", 8);
    table(base / "Serum Tables" / "Mine.wav", 16);   // same name as in the main folder
    Engine e;
    e.prepare(44100.0, 256);
    TableLibrary lib(e);
    lib.setRoots({(base / "main").string(), (base / "Serum Tables").string()});
    const unsigned before = lib.scanSerial();
    lib.rescan();
    lib.waitIdle();
    CHECK(lib.scanSerial() > before, "scan serial");
    const int a = lib.indexOfId("file:Mine.wav"), b = lib.indexOfId("file:Serum Tables/Mine.wav"), c = lib.indexOfId("file:Serum Tables/Bass/Growler.wav");
    CHECK(a >= 0 && b >= 0 && c >= 0 && a != b, "ids %d %d %d", a, b, c);
    const auto en = lib.entries();
    CHECK(en[(size_t)a].group == "HELIX" && en[(size_t)b].group == "Serum Tables" && en[(size_t)c].group == "Serum Tables/Bass",
          "groups '%s' '%s' '%s'", en[(size_t)a].group.c_str(), en[(size_t)b].group.c_str(), en[(size_t)c].group.c_str());
    CHECK(a < b, "HELIX's own folder first");
    lib.requestLoadId(0, "file:Serum Tables/Mine.wav");
    lib.waitIdle();
    e.collectGarbage();
    std::vector<float> l(256), r(256);
    e.process(l.data(), r.data(), 256, nullptr, 0, Transport{});
    CHECK(e.uiTable(0) && e.uiTable(0)->numFrames() == 16, "loaded the wrong Mine.wav (%d frames)", e.uiTable(0) ? e.uiTable(0)->numFrames() : -1);
    CHECK(paramDef(P_A_TABLE).max == (float)(kMaxTables - 1), "table range");
    { std::error_code ec; fs::remove_all(base, ec); }
    TEST_MAIN_END();
}
