// helix-mpc/tools/choices.json: every list parameter's entries, for the skin generator (make_skin.py builds the
// tap-to-open list pop-ups from it; the MPC's parameter dump cannot tell lists of more than ~64 entries from
// numbers). This test fails when the file is stale; HELIX_WRITE_CHOICES=1 rewrites it.
#include "TestUtil.h"
#include "HelixMpcApp.h"
#include "helix/Params.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace helix;

static std::string esc(const std::string& s)
{
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o;
}

int main()
{
    std::ostringstream js;
    js << "{\n";
    bool first = true;
    auto add = [&](const std::string& name, const std::vector<std::string>& list) {
        js << (first ? "" : ",\n") << "  \"" << esc(name) << "\": [";
        for (size_t i = 0; i < list.size(); ++i) js << (i ? ", " : "") << "\"" << esc(list[i]) << "\"";
        js << "]";
        first = false;
    };
    for (int i = 0; i < kNumParams; ++i)
        if (paramDef(i).kind == ParamKind::Choice && paramDef(i).choices) add(paramDef(i).name, *paramDef(i).choices);
    for (int i = kMpcReserveEnd; i < kMpcParamEnd; ++i)
    {
        const ParamDef& d = mpcParamDef(i);
        if (d.kind == ParamKind::Choice && d.choices && !d.choices->empty()) add(d.name, *d.choices);
    }
    js << "\n}\n";
    const std::string path = std::string(HELIX_SOURCE_DIR) + "/helix-mpc/tools/choices.json";
    if (std::getenv("HELIX_WRITE_CHOICES"))
    {
        std::ofstream(path, std::ios::binary) << js.str();
        std::printf("wrote %s\n", path.c_str());
    }
    std::ifstream in(path, std::ios::binary);
    std::stringstream cur;
    cur << in.rdbuf();
    CHECK(cur.str() == js.str(), "helix-mpc/tools/choices.json is stale: run HELIX_WRITE_CHOICES=1 build/tests/test_choice_lists");
    TEST_MAIN_END();
}
