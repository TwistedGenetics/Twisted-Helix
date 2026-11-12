// Writes helix-mpc/ui.json (the SDK's simple page format) from the shared HELIX page layout.
// Each layout section (<= 4 params) becomes one Q-Link block of four; up to four blocks per MPC page.
#include "MpcParams.h"
#include "helix/Json.h"
#include "helix/UiLayout.h"
#include <cstdio>
#include <fstream>
#include <map>

using namespace helix;

static Json control(int index)
{
    Json c = Json::object();
    if (index < 0) { c.set("widget", "empty"); return c; }
    const ParamDef& d = mpcParamDef(index);
    c.set("param", d.name);
    const std::string name = d.name;
    if (d.kind == ParamKind::Bool) c.set("widget", "switch");
    else if (d.kind == ParamKind::Choice)
    {
        if (d.numSteps() <= 4) c.set("widget", "switch");
        else { c.set("widget", "slider"); c.set("overlay", "menu"); }
    }
    else if (d.kind == ParamKind::Int) c.set("widget", "slider");
    else if (name.find("Level") != std::string::npos || name.find("Mix") != std::string::npos || name.find("Sustain") != std::string::npos)
        c.set("widget", "fader");
    else c.set("widget", "knob");
    if ((d.kind == ParamKind::Linear || d.kind == ParamKind::Squared) && d.min < 0.0f && d.max > 0.0f) c.set("polarity", "bipolar");
    return c;
}

int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "ui.json";
    std::vector<UiPage> pages = uiPages();
    // The MPC-only controls go on the BROWSER page, first.
    for (auto& pg : pages)
        if (pg.name == "BROWSER")
            pg.sections.insert(pg.sections.begin(), {
                {"Preset", {MP_PRESET, MP_LOAD, MP_PAD_BROWSE, MP_PRESET_BANK}},
                {"Tools", {MP_MUTATE, MP_CPU, -1, -1}},
                {"Sample > Table", {MP_S2T_FILE, MP_S2T_FRAMES, MP_S2T_SLOT, MP_S2T_GO}},
            });

    static const std::pair<const char*, const char*> shortNames[] = {
        {"OSC A", "OSC A"}, {"OSC B", "OSC B"}, {"SUB/NOISE", "SUB/NSE"}, {"FILTERS", "FILTER"}, {"ENV", "ENV"},
        {"LFO/PERF", "LFO"}, {"MATRIX", "MATRIX"}, {"FX", "FX"}, {"MACROS", "MACROS"}, {"BROWSER", "BROWSE"}};
    Json root = Json::object();
    // Light theme: dark text on a light background (brief: no dark backgrounds with light text).
    Json style = Json::object();
    style.set("background", "fff3f1ec");
    style.set("text", "ff15171a");
    style.set("muted", "ff3b3f45");
    style.set("accent", "ffd6402b");
    Json kit = Json::object();
    kit.set("theme", "mono");
    kit.set("accent", "ffd6402b");
    root.set("kit", kit);
    Json outPages = Json::array();
    int total = 0;
    for (const auto& pg : pages)
    {
        std::string base = pg.name;
        for (auto& sn : shortNames) if (pg.name == sn.first) base = sn.second;
        // Split into MPC pages: a new page when the group changes or after four blocks.
        std::vector<std::pair<std::string, std::vector<const UiSection*>>> chunks;
        for (const auto& sec : pg.sections)
        {
            const std::string grp = sec.group.empty() ? base : sec.group;
            if (chunks.empty() || chunks.back().first != grp || chunks.back().second.size() == 4)
                chunks.push_back({grp, {}});
            chunks.back().second.push_back(&sec);
        }
        std::map<std::string, int> seen, counts;
        for (auto& c : chunks) counts[c.first]++;
        for (auto& c : chunks)
        {
            Json page = Json::object();
            std::string name = c.first;
            if (counts[c.first] > 1) name += " " + std::to_string(++seen[c.first]);
            if (name.size() > 8) name = name.substr(0, 8);
            page.set("name", name);
            Json controls = Json::array();
            for (const UiSection* sec : c.second)
                for (int i = 0; i < 4; ++i)
                    controls.push(control(i < (int)sec->params.size() ? sec->params[(size_t)i] : -1));
            page.set("controls", controls);
            outPages.push(page);
            ++total;
        }
    }
    root.set("pages", outPages);
    root.set("style", style);
    if (total > 32) { std::fprintf(stderr, "too many pages: %d (the MPC allows 32)\n", total); return 1; }
    std::ofstream(out) << root.dump(2);
    std::printf("wrote %s: %d pages\n", out, total);
    return 0;
}
