#include "MidiFile.h"
#include <algorithm>
#include <fstream>
#include <map>

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

bool readMidiFile(const std::string& path, std::vector<TimedMidi>& out, std::string* err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot open"; return false; }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 14 || std::string((char*)d.data(), 4) != "MThd") { if (err) *err = "not a MIDI file"; return false; }
    const int ntracks = be16(&d[10]);
    const int division = be16(&d[12]);
    if (division & 0x8000) { if (err) *err = "SMPTE time division not supported"; return false; }
    struct Raw { uint64_t tick; int order; uint8_t b[3]; bool tempo; uint32_t usPerQ; };
    std::vector<Raw> raw;
    size_t pos = 8 + be32(&d[4]);
    int order = 0;
    for (int t = 0; t < ntracks && pos + 8 <= d.size(); ++t)
    {
        if (std::string((char*)&d[pos], 4) != "MTrk") break;
        const size_t len = be32(&d[pos + 4]);
        size_t p = pos + 8;
        const size_t end = std::min(d.size(), p + len);
        uint64_t tick = 0;
        uint8_t running = 0;
        while (p < end)
        {
            uint32_t delta = 0;
            for (int k = 0; k < 4 && p < end; ++k) { uint8_t c = d[p++]; delta = (delta << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
            tick += delta;
            if (p >= end) break;
            uint8_t st = d[p];
            if (st == 0xFF)
            {
                if (p + 2 >= end) break;
                const uint8_t type = d[p + 1];
                p += 2;
                uint32_t l = 0;
                for (int k = 0; k < 4 && p < end; ++k) { uint8_t c = d[p++]; l = (l << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
                if (type == 0x51 && l == 3 && p + 3 <= end)
                    raw.push_back({tick, order++, {0, 0, 0}, true, (uint32_t)d[p] << 16 | (uint32_t)d[p + 1] << 8 | d[p + 2]});
                p += l;
                continue;
            }
            if (st == 0xF0 || st == 0xF7)
            {
                ++p;
                uint32_t l = 0;
                for (int k = 0; k < 4 && p < end; ++k) { uint8_t c = d[p++]; l = (l << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
                p += l;
                continue;
            }
            if (st & 0x80) { running = st; ++p; }
            if (!running) break;
            const uint8_t hi = running & 0xF0;
            const int n = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            if (p + (size_t)n > end) break;
            Raw r{tick, order++, {running, d[p], n == 2 ? d[p + 1] : (uint8_t)0}, false, 0};
            raw.push_back(r);
            p += (size_t)n;
        }
        pos = pos + 8 + len;
    }
    std::stable_sort(raw.begin(), raw.end(), [](const Raw& a, const Raw& b) { return a.tick < b.tick; });
    double usPerQ = 500000.0, secs = 0.0;
    uint64_t lastTick = 0;
    for (const Raw& r : raw)
    {
        secs += (double)(r.tick - lastTick) * usPerQ / 1e6 / division;
        lastTick = r.tick;
        if (r.tempo) { usPerQ = r.usPerQ; continue; }
        out.push_back({secs, {r.b[0], r.b[1], r.b[2]}});
    }
    return true;
}
