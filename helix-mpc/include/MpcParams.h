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
    // 0.3.5 the effects library slots' knob names (read-only: their text is the name of knob A-F for the slot's
    // effect; the app flips the value when the effect changes so the screen redraws). Slot s, knob k: MP_LIBNAME + 6s + k
    MP_LIBNAME,
    // 0.4.2 Print to wavetable: render the patch (sweeping a control) into a new table in Wavetables/Prints
    MP_PR_SRC = MP_LIBNAME + 24, MP_PR_FRAMES, MP_PR_LEN, MP_PR_NOTE, MP_PR_SLOT, MP_PR_FX, MP_PR_GO,
    MP_PR_STATUS,                 // read-only: "Printing...", the new table's name or the error (value flips to redraw)
    // 0.4.4 character oscillator knob names (read-only, follow the engine like the effect slots' names)
    MP_CONAME,
    // 0.4.5 pad scenes: the scene shown / recalled (touching one recalls it), and Store (into the shown scene)
    MP_SCENE = MP_CONAME + 6, MP_SCENE_STORE,
    // 0.4.7 user slots: 16 entries at the end of the MPC's preset menu; Save writes the sound into the chosen one
    MP_SAVE_SLOT, MP_SAVE_GO,
    // 0.4.8 naming before saving: a cursor, the letter under it (jog wheel / Q-Link), the name shown, Clear
    MP_NAME_CURSOR, MP_NAME_CHAR, MP_NAME_SHOW, MP_NAME_CLEAR,
    kMpcParamEnd
};
constexpr int kMpcNumParams = kMpcParamEnd;
constexpr int kNumPresetBanks = 8;
constexpr int kMaxPresets = 256;
constexpr int kNumUserSlots = 128;  // "U001" .. "U128" programs, files in Content/User Slots
constexpr int kMaxNameLen = 20;

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
