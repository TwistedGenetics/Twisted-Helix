// The HELIX page layout, shared by the desktop editor and the MPC skin generator. Pure data.
// Sections hold up to four parameters, so each one maps onto one Q-Link block of four on the MPC.
#pragma once
#include <string>
#include <vector>

namespace helix {

struct UiSection
{
    std::string title;
    std::vector<int> params;    // ParamId values, at most 4; -1 = empty slot
};

struct UiPage
{
    std::string name;           // OSC A | OSC B | SUB/NOISE | FILTERS | ENV | LFO/PERF | MATRIX | FX | MACROS | BROWSER
    std::vector<UiSection> sections;
};

const std::vector<UiPage>& uiPages();

} // namespace helix
