// The thin platform layer between HELIX and a host device. Everything HELIX needs from "the MPC" goes through
// this interface, so the MPC-specific code is one small adapter (src/HelixMpc.cpp, MPC Plugin SDK) and the rest
// (HelixMpcApp) can be tested on a desktop with a fake platform.
//
// What each part maps to on the MPC (MPC Plugin SDK, MPC OS 3.10.1+), see docs/PLATFORM.md:
//   audio I/O   -> processReplacing, float, in place, 44.1 kHz, 32..512 frames per call
//   MIDI in     -> instrument MIDI with sample offsets (poly aftertouch forwarding: unverified on hardware)
//   file system -> the plugin folder's Content/ (mpc::contentPath); never absolute device paths
//   UI surface  -> declarative JSON skins: the plugin can only change parameter values and their text
//   encoders    -> Q-Links, banked by the skin (Q-Links.json), 4 knobs per block, 16 per page
#pragma once
#include <string>
#include <vector>

namespace helix {

class IPlatform
{
public:
    virtual ~IPlatform() = default;

    // ---- identity / capabilities
    virtual const char* platformName() const = 0;
    // Unknown on the MPC until tested: does pad pressure arrive as poly aftertouch (0xA0)?
    virtual bool polyAftertouchKnown() const { return false; }

    // ---- file system (non-real-time)
    // Root of the plugin's own data: <plugin folder>/Content on the MPC.
    virtual std::string contentDir() const = 0;

    // ---- UI surface (non-real-time; never call from the audio thread)
    // A parameter changed inside the plugin (preset load, CPU readout): tell the host so the screen follows.
    virtual void notifyParameter(int index, float plainValue) = 0;
    // Names or value lists changed: ask the host to re-read them (expensive on the MPC: rebuilds the page).
    virtual void requestDisplayRefresh() = 0;

    // The host's current program (built-in preset), or -1 if the host has none. Polled by the app's worker.
    virtual int currentProgram() const { return -1; }

    // ---- encoders
    // Q-Link knobs per page (16 on MPCs). Banks of four are described by the skin, not by code.
    virtual int encodersPerPage() const { return 16; }
};

} // namespace helix
