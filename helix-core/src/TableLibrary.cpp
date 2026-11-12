#include "helix/TableLibrary.h"
#include "helix/WavetableGen.h"
#include "helix/WavetableImport.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace helix {

namespace fs = std::filesystem;

static std::vector<TableEntry> builtinEntries()
{
    std::vector<TableEntry> v;
    const auto& names = builtinTableNames();
    for (int i = 0; i < (int)names.size(); ++i)
        v.push_back({"builtin:" + names[(size_t)i], names[(size_t)i], "", i, false});
    return v;
}

TableLibrary::TableLibrary(Engine& engine) : engine_(engine)
{
    entries_ = builtinEntries();
    slotId_[0] = slotId_[1] = entries_[0].id;
    thread_ = std::thread([this] { worker(); });
}

TableLibrary::~TableLibrary()
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void TableLibrary::setRoots(std::vector<std::string> roots) { std::lock_guard<std::mutex> lk(mutex_); roots_ = std::move(roots); }
void TableLibrary::rescan() { push({Job::Scan}); }

std::vector<TableEntry> TableLibrary::entries() const { std::lock_guard<std::mutex> lk(mutex_); return entries_; }
int TableLibrary::count() const { std::lock_guard<std::mutex> lk(mutex_); return (int)entries_.size(); }
std::string TableLibrary::nameAt(int i) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (entries_.empty() || i < 0) return std::string();
    return entries_[(size_t)i % entries_.size()].name;
}
int TableLibrary::wrap(int i) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return entries_.empty() || i < 0 ? 0 : i % (int)entries_.size();
}
std::string TableLibrary::idAt(int i) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (entries_.empty() || i < 0) return std::string();
    return entries_[(size_t)i % entries_.size()].id;
}
bool TableLibrary::isLibraryId(const std::string& id)
{
    return id.rfind("builtin:", 0) == 0 || id.rfind("file:", 0) == 0 || id.rfind("folder:", 0) == 0;
}
int TableLibrary::indexOfId(const std::string& id) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    for (size_t i = 0; i < entries_.size(); ++i) if (entries_[i].id == id) return (int)i;
    return -1;
}
std::string TableLibrary::slotId(int slot) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return slot >= 0 && slot < kNumTableSlots ? slotId_[slot] : std::string();
}
std::string TableLibrary::lastError() const { std::lock_guard<std::mutex> lk(mutex_); return lastError_; }
void TableLibrary::setOnLoaded(std::function<void(int, const std::string&)> cb) { std::lock_guard<std::mutex> lk(mutex_); onLoaded_ = std::move(cb); }

void TableLibrary::requestLoad(int slot, int index) { push({Job::LoadIndex, slot, index}); }
void TableLibrary::requestLoadId(int slot, const std::string& id) { push({Job::LoadId, slot, 0, id}); }
void TableLibrary::requestLoadFile(int slot, const std::string& path, int frames) { push({Job::LoadFile, slot, 0, path, frames}); }

void TableLibrary::push(Job j)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        // A newer load for the same slot replaces an older queued one (fast browsing does not pile up work).
        if (j.kind != Job::Scan)
            jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& o) { return o.kind != Job::Scan && o.slot == j.slot; }), jobs_.end());
        jobs_.push_back(std::move(j));
    }
    cv_.notify_all();
}

void TableLibrary::waitIdle()
{
    std::unique_lock<std::mutex> lk(mutex_);
    idleCv_.wait(lk, [this] { return jobs_.empty() && !busy_; });
}

void TableLibrary::worker()
{
    for (;;)
    {
        Job j;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] { return quit_ || !jobs_.empty(); });
            if (quit_) return;
            j = std::move(jobs_.front());
            jobs_.pop_front();
            busy_ = true;
        }
        switch (j.kind)
        {
            case Job::Scan: doScan(); break;
            case Job::LoadIndex:
            {
                TableEntry e; bool ok = false;
                { std::lock_guard<std::mutex> lk(mutex_); if (j.index >= 0 && !entries_.empty()) { e = entries_[(size_t)j.index % entries_.size()]; ok = true; } }
                if (ok) doLoad(j.slot, e);
                break;
            }
            case Job::LoadId:
            {
                TableEntry e; bool ok = false;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    for (auto& x : entries_) if (x.id == j.text) { e = x; ok = true; break; }
                }
                // Ids that name a file directly still load when the file is not in the library.
                if (!ok && j.text.rfind("path:", 0) == 0) { e = {j.text, fs::path(j.text.substr(5)).stem().string(), j.text.substr(5), -1, false}; ok = true; }
                if (!ok && j.text.rfind("sample:", 0) == 0)
                {
                    const size_t hash = j.text.rfind('#');
                    const std::string path = j.text.substr(7, hash == std::string::npos ? std::string::npos : hash - 7);
                    const int frames = hash == std::string::npos ? 64 : std::atoi(j.text.c_str() + hash + 1);
                    { std::lock_guard<std::mutex> lk(mutex_); jobs_.push_front(Job(Job::LoadFile, j.slot, 0, path, std::max(2, frames))); }
                    break;
                }
                if (ok) doLoad(j.slot, e);
                else { std::lock_guard<std::mutex> lk(mutex_); lastError_ = "table not found: " + j.text; }
                break;
            }
            case Job::LoadFile:
            {
                TableEntry e{"path:" + j.text, fs::path(j.text).stem().string(), j.text, -1, false};
                if (j.frames > 0)
                {
                    WavData w; std::string err;
                    WavetablePtr t;
                    if (readWavFile(j.text, w, &err))
                    {
                        SampleToTableOptions opt; opt.frames = j.frames;
                        const auto m = w.mono();
                        t = sampleToTable(m.data(), m.size(), w.sampleRate, e.name + " (S>T)", opt);
                    }
                    if (t)
                    {
                        while (!engine_.setTable(j.slot, t)) std::this_thread::yield();
                        std::function<void(int, const std::string&)> cb;
                        const std::string id = "sample:" + j.text + "#" + std::to_string(j.frames);
                        { std::lock_guard<std::mutex> lk(mutex_); slotId_[j.slot] = id; cb = onLoaded_; }
                        if (cb) cb(j.slot, id);
                    }
                    else { std::lock_guard<std::mutex> lk(mutex_); lastError_ = "Sample -> Table failed: " + (err.empty() ? std::string("no usable audio") : err); }
                }
                else doLoad(j.slot, e);
                break;
            }
        }
        {
            std::lock_guard<std::mutex> lk(mutex_);
            busy_ = false;
            if (jobs_.empty()) idleCv_.notify_all();
        }
    }
}

bool TableLibrary::doLoad(int slot, const TableEntry& e)
{
    if (slot < 0 || slot >= kNumTableSlots) return false;
    std::string err;
    WavetablePtr t;
    if (e.builtin >= 0) t = makeBuiltinTable((BuiltinTable)e.builtin);
    else if (e.folder) t = importCycleFolder(e.path, &err);
    else t = importWavetableFile(e.path, &err);
    if (!t) { std::lock_guard<std::mutex> lk(mutex_); lastError_ = e.name + ": " + err; return false; }
    // The hand-over queue is bounded; wait for the audio thread to drain it if necessary.
    for (int tries = 0; !engine_.setTable(slot, t); ++tries)
    {
        if (tries > 2000) { std::lock_guard<std::mutex> lk(mutex_); lastError_ = "audio thread not running"; return false; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::function<void(int, const std::string&)> cb;
    { std::lock_guard<std::mutex> lk(mutex_); slotId_[slot] = e.id; cb = onLoaded_; }
    if (cb) cb(slot, e.id);
    return true;
}

void TableLibrary::doScan()
{
    scanning_ = true;
    std::vector<std::string> roots;
    { std::lock_guard<std::mutex> lk(mutex_); roots = roots_; }
    std::vector<TableEntry> found;
    for (const std::string& root : roots)
    {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) break;
            if (it.depth() > 4) { it.disable_recursion_pending(); continue; }
            const fs::path p = it->path();
            const std::string rel = fs::relative(p, root, ec).generic_string();
            if (it->is_directory(ec))
            {
                // A folder that holds WAVs and no sub-folders is offered as a "folder of single cycles".
                bool hasWav = false, hasDir = false;
                for (const auto& c : fs::directory_iterator(p, ec))
                {
                    if (c.is_directory(ec)) hasDir = true;
                    else { std::string x = c.path().extension().string(); std::transform(x.begin(), x.end(), x.begin(), ::tolower); hasWav |= x == ".wav"; }
                }
                if (hasWav && !hasDir)
                {
                    found.push_back({"folder:" + rel, p.filename().string() + " (cycles)", p.string(), -1, true});
                    it.disable_recursion_pending();   // its files are frames, not tables of their own
                }
                continue;
            }
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wav") found.push_back({"file:" + rel, p.stem().string(), p.string(), -1, false});
        }
    }
    std::sort(found.begin(), found.end(), [](const TableEntry& a, const TableEntry& b) { return a.id < b.id; });
    std::vector<TableEntry> all = builtinEntries();
    all.insert(all.end(), found.begin(), found.end());
    if (all.size() > 512) all.resize(512);   // the table parameter's range
    { std::lock_guard<std::mutex> lk(mutex_); entries_ = std::move(all); }
    scanning_ = false;
}

} // namespace helix
