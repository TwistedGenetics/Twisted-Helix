// MPC-only parameters, appended after a reserved block so the core list can grow without moving them.
// Indices are permanent once released (MPC projects store parameters by index).
#pragma once
#include "helix/Params.h"

namespace helix {

constexpr int kMpcReserveEnd = 448;              // core params live in [0, kNumParams); fillers up to here
static_assert(kNumParams <= kMpcReserveEnd, "the core parameter list outgrew the MPC reserve");

enum MpcParam : int
{
    MP_PRESET = kMpcReserveEnd,   // browse: factory + Content/Presets
    MP_LOAD,                      // trigger: load the browsed preset
    MP_PAD_BROWSE,                // pads 1-16 pick + audition presets (replaces "Shift + pad", which plugins cannot see)
    MP_PRESET_BANK,               // which 16 presets the pads browse
    MP_MUTATE,                    // trigger: mutate 10%
    MP_CPU,                       // read-only CPU readout (value text "23%")
    MP_S2T_FILE,                  // Sample -> Table: file in Content/Samples
    MP_S2T_FRAMES,                // 8..256
    MP_S2T_SLOT,                  // A / B
    MP_S2T_GO,                    // trigger
    // 0.3.3 Massive-style performer editing: pick a step, then its curve (library) and level. These mirror
    // P1/P2 Shape n and Step n of the chosen step (the worker keeps both sides in step).
    MP_P1_EDIT, MP_P1_CURVE, MP_P1_LEVEL,
    MP_P2_EDIT, MP_P2_CURVE, MP_P2_LEVEL,
    // 0.3.4 one list per filter for the FILTER page screen: the 13 HELIX types, then every Mega model x type.
    // Mirrors F Model / F Type / F Filter Type (the worker keeps both sides in step).
    MP_F1_FILTER, MP_F2_FILTER,
    kMpcParamEnd
};
constexpr int kMpcNumParams = kMpcParamEnd;
constexpr int kNumPresetBanks = 8;
constexpr int kMaxPresets = 256;

// Definition of an MPC parameter: core params, fillers (hidden, id "reserved_N") and the MPC-only ones.
const ParamDef& mpcParamDef(int index);
bool mpcIsFiller(int index);
// The combined filter list (MP_F1_FILTER): HELIX types first, then Mega model x filter type.
const std::vector<std::string>& combinedFilterChoices();
int combinedFilterIndex(int model, int helixType, int megaKind);
void combinedFilterSplit(int index, int& model, int& helixType, int& megaKind);
// S2T frame counts for MP_S2T_FRAMES
int s2tFrames(int choice);

} // namespace helix
