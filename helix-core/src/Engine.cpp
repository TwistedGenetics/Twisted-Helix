#include "helix/Engine.h"
#include "helix/Denormals.h"
#include "helix/Simd.h"
#include "helix/WavetableGen.h"
#include <cstring>

namespace helix {

Engine::Engine()
{
    ctx_.params = &params_;
    for (int s = 0; s < kNumTableSlots; ++s)
    {
        // A default table so a fresh engine makes sound. Built here, on the constructing (non-audio) thread.
        active_[s] = makeBuiltinTable(BuiltinTable::BasicShapes);
        uiTables_[s] = active_[s];
    }
    prepare(44100.0, 512);
}

Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlockSize;
    ctx_.sampleRate = (float)sampleRate;
    for (Voice& v : voices_) v.prepare((float)sampleRate);
    reset();
}

void Engine::reset()
{
    for (Voice& v : voices_) v.kill();
    pitchBend_ = 0.0f;
}

bool Engine::setTable(int slot, WavetablePtr table)
{
    if (slot < 0 || slot >= kNumTableSlots || !table) return false;
    collectGarbage();
    TableHandle h{table, slot};
    if (!pending_.push(std::move(h))) return false;
    uiTables_[slot] = std::move(table);
    return true;
}

WavetablePtr Engine::uiTable(int slot) const { return (slot >= 0 && slot < kNumTableSlots) ? uiTables_[slot] : nullptr; }

void Engine::collectGarbage()
{
    TableHandle h;
    while (retired_.pop(h)) h.ptr.reset();
}

void Engine::swapTables()
{
    // Only swap when the old table can be handed back without freeing it here.
    while (!retired_.full())
    {
        TableHandle h;
        if (!pending_.pop(h)) break;
        TableHandle old{std::move(active_[h.slot]), h.slot};
        active_[h.slot] = std::move(h.ptr);
        retired_.push(std::move(old));
    }
}

void Engine::snapshotParams()
{
    for (int i = 0; i < kNumParams; ++i) ctx_.p[i] = params_.get(i);
    for (int s = 0; s < kNumTableSlots; ++s) ctx_.table[s] = active_[s].get();
    ctx_.mipCrossfade = true;
    ctx_.pitchBendSemis = pitchBend_ * ctx_.p[P_BEND_RANGE];
}

void Engine::handleMidi(const MidiMsg& m)
{
    switch (m.status())
    {
        case 0x90: if (m.data[2] > 0) noteOn(m.data[1], m.data[2]); else noteOff(m.data[1]); break;
        case 0x80: noteOff(m.data[1]); break;
        case 0xE0: pitchBend_ = (float)(((int)m.data[1] | ((int)m.data[2] << 7)) - 8192) / 8192.0f; break;
        case 0xB0:
            if (m.data[1] == 120) allNotesOff(true);
            else if (m.data[1] == 123) allNotesOff(false);
            break;
        default: break;
    }
}

void Engine::noteOn(int note, int vel)
{
    const int maxV = std::max(1, std::min(kMaxVoices, (int)ctx_.p[P_VOICES]));
    Voice* target = nullptr;
    for (int i = 0; i < maxV; ++i) if (!voices_[i].active()) { target = &voices_[i]; break; }
    if (!target)
    {
        target = &voices_[0];
        for (int i = 1; i < maxV; ++i) if (voices_[i].age() < target->age()) target = &voices_[i];
    }
    target->start(note, (float)vel / 127.0f, ++ageCounter_, false);
}

void Engine::noteOff(int note)
{
    for (Voice& v : voices_) if (v.active() && v.held() && v.note() == note) v.release();
}

void Engine::allNotesOff(bool hard)
{
    for (Voice& v : voices_) { if (hard) v.kill(); else v.release(); }
}

void Engine::renderBlock(float* l, float* r, int n)
{
    int count = 0;
    for (Voice& v : voices_)
    {
        if (!v.active()) continue;
        v.render(ctx_, l, r, n);
        ++count;
    }
    activeVoices_.store(count, std::memory_order_relaxed);
}

void Engine::process(float* l, float* r, int n, const MidiMsg* events, int numEvents, const Transport& transport)
{
    (void)transport;
    ScopedFlushDenormals noDenormals;
    swapTables();
    bufClear(l, n);
    bufClear(r, n);

    int ev = 0, pos = 0;
    while (pos < n)
    {
        while (ev < numEvents && events[ev].offset <= pos) handleMidi(events[ev++]);
        int end = std::min(n, pos + kCtrlBlock);
        if (ev < numEvents && events[ev].offset < end) end = std::max(pos + 1, events[ev].offset);
        snapshotParams();
        renderBlock(l + pos, r + pos, end - pos);
        pos = end;
    }
    while (ev < numEvents) handleMidi(events[ev++]);

    const float master = dbToGain(params_.get(P_MASTER));
    bufScale(l, master, n);
    bufScale(r, master, n);
}

} // namespace helix
