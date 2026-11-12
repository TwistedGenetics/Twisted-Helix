// Minimal Standard MIDI File reader (format 0 and 1, tempo map). Produces events in seconds.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct TimedMidi
{
    double seconds;
    uint8_t data[3];
};

bool readMidiFile(const std::string& path, std::vector<TimedMidi>& out, std::string* error);
