#include "livedbg.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace livedbg {
namespace {

constexpr uint32_t kSnapMagic = 0x42445854;  // "TXDB" little-endian
constexpr uint32_t kSnapVersion = 1;
constexpr int kSnapHeader = 64;
constexpr uint32_t kCmdMagic = 0x43445854;  // "TXDC"
constexpr uint32_t kCmdVersion = 1;
constexpr int kCmdHeader = 32;
constexpr uint32_t kFooterXor = 0x5A5A5A5AU;

template <typename T>
T rd(const unsigned char* p) {
    T v{};
    std::memcpy(&v, p, sizeof(T));
    return v;
}

void put32(std::vector<unsigned char>& v, uint32_t x) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(&x);
    v.insert(v.end(), b, b + 4);
}
void put16(std::vector<unsigned char>& v, uint16_t x) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(&x);
    v.insert(v.end(), b, b + 2);
}

}  // namespace

// ---------------------------------------------------------------- symbols ---

const NodeSym* Symbols::find(int key) const {
    for (const NodeSym& n : nodes)
        if (n.key == key) return &n;
    return nullptr;
}

bool loadSymbols(const std::string& path, Symbols& out) {
    out = Symbols();
    std::ifstream f(path);
    if (!f) return false;

    Symbols s;
    std::string line;
    bool versionSeen = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (!versionSeen) {
            // The first non-comment line is the format version.
            if (tag != "1") return false;
            versionSeen = true;
            continue;
        }
        if (tag == "hash") {
            std::string hex;
            ls >> hex;
            s.hash = std::strtoull(hex.c_str(), nullptr, 16);
        } else if (tag == "n") {
            NodeSym n;
            ls >> n.key >> n.scene >> n.objectId >> n.nodeId >> n.type;
            if (!ls || n.objectId.empty()) return false;
            s.nodes.push_back(std::move(n));
        } else if (tag == "v") {
            VarSym v;
            std::string kind;
            ls >> v.index >> kind >> v.name;
            if (!ls || kind.empty()) return false;
            v.kind = kind[0];
            s.vars.push_back(std::move(v));
        } else if (tag == "nodes" || tag == "vars") {
            continue;  // counts are informational (the lists are authoritative)
        } else {
            return false;  // unknown record: refuse the whole file
        }
    }
    if (!versionSeen) return false;
    s.loaded = true;
    out = std::move(s);
    return true;
}

// --------------------------------------------------------------- snapshot ---

bool parseSnapshot(const std::vector<unsigned char>& bytes, Snapshot& out) {
    if (bytes.size() < (size_t)kSnapHeader + 4) return false;
    const unsigned char* b = bytes.data();
    if (rd<uint32_t>(b + 0) != kSnapMagic) return false;
    if (rd<uint32_t>(b + 4) != kSnapVersion) return false;

    Snapshot s;
    s.seq = rd<uint32_t>(b + 8);
    s.frame = rd<uint32_t>(b + 12);
    s.scene = rd<int32_t>(b + 16);
    const uint32_t flags = rd<uint32_t>(b + 20);
    s.halted = (flags & 1u) != 0;
    s.breakKey = rd<int32_t>(b + 24);
    const int nodeCount = rd<int32_t>(b + 28);
    const int varCount = rd<int32_t>(b + 32);
    const int eventCount = rd<int32_t>(b + 36);
    s.hash = (uint64_t)rd<uint32_t>(b + 40) |
             ((uint64_t)rd<uint32_t>(b + 44) << 32);
    if (nodeCount < 0 || nodeCount > kMaxNodes) return false;
    if (varCount < 0 || varCount > 4096) return false;
    if (eventCount < 0 || eventCount > kMaxEvents) return false;

    // Exact size + footer echo of seq: a torn write fails one of the two.
    const size_t need = (size_t)kSnapHeader + (size_t)nodeCount * 4 +
                        (size_t)eventCount * 4 + (size_t)varCount * 12 + 4;
    if (bytes.size() != need) return false;
    const unsigned char* p = b + kSnapHeader;
    s.hits.resize(nodeCount);
    for (int i = 0; i < nodeCount; ++i, p += 4) s.hits[i] = rd<uint32_t>(p);
    s.events.resize(eventCount);
    for (int i = 0; i < eventCount; ++i, p += 4) {
        Event e;
        e.key = rd<uint16_t>(p);
        // Events carry the age in frames, so the ring stays valid whatever the
        // absolute frame counter is when it is flushed.
        const uint16_t age = rd<uint16_t>(p + 2);
        e.frame = age <= s.frame ? s.frame - age : 0;
        s.events[i] = e;
    }
    s.vars.resize((size_t)varCount * 3);
    for (int i = 0; i < varCount * 3; ++i, p += 4) s.vars[i] = rd<float>(p);
    if (rd<uint32_t>(p) != (s.seq ^ kFooterXor)) return false;

    out = std::move(s);
    return true;
}

bool readSnapshot(const std::string& path, Snapshot& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    return parseSnapshot(bytes, out);
}

// ---------------------------------------------------------------- command ---

bool Command::sameStateAs(const Command& o) const {
    return halt == o.halt && stepUntilFire == o.stepUntilFire &&
           stepFrames == o.stepFrames && breakpoints == o.breakpoints &&
           fire == o.fire;
}

std::vector<unsigned char> encodeCommand(const Command& c) {
    std::vector<unsigned char> v;
    v.reserve(kCmdHeader + (c.breakpoints.size() + c.fire.size()) * 2 + 4);
    put32(v, kCmdMagic);
    put32(v, kCmdVersion);
    put32(v, c.seq);
    put32(v, (c.halt ? 1u : 0u) | (c.stepUntilFire ? 2u : 0u));
    put32(v, (uint32_t)(int32_t)c.stepFrames);
    put32(v, (uint32_t)(int32_t)c.breakpoints.size());
    put32(v, (uint32_t)(int32_t)c.fire.size());
    put32(v, 0);  // reserved
    for (uint16_t k : c.breakpoints) put16(v, k);
    for (uint16_t k : c.fire) put16(v, k);
    put32(v, c.seq ^ kFooterXor);
    return v;
}

std::string writeCommand(const std::string& path, const Command& c) {
    namespace fs = std::filesystem;
    const std::vector<unsigned char> bytes = encodeCommand(c);
    const fs::path target(path);
    const fs::path tmp = fs::path(path + ".tmp");
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return "cannot write " + tmp.string();
        f.write(reinterpret_cast<const char*>(bytes.data()),
                (std::streamsize)bytes.size());
        if (!f) return "write failed: " + tmp.string();
    }
    // Rename over the old file so the game never reads a half-written command.
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) return "cannot replace " + target.string() + ": " + ec.message();
    }
    return "";
}

// --------------------------------------------------------------- timeline ---

void Timeline::clear() {
    frames_.clear();
    lastSeq_ = 0;
    lastFrame_ = 0;
}

void Timeline::ingest(const Snapshot& s) {
    if (s.seq == lastSeq_ && !frames_.empty()) return;
    // A restarted game starts counting frames from zero again: the old history
    // belongs to the previous run, not in front of this one.
    if (s.frame + 1 < lastFrame_) clear();
    lastSeq_ = s.seq;
    lastFrame_ = s.frame;

    // The game's event ring overlaps between flushes (it is not drained on
    // read), so everything up to the newest frame already recorded is a
    // repeat. A frame's events always arrive in one flush - the game flushes
    // between frames, never inside one - so this threshold never splits a
    // frame in half.
    const bool haveAny = !frames_.empty();
    const uint32_t threshold = haveAny ? frames_.back().frame : 0;
    for (const Event& e : s.events) {
        if (haveAny && e.frame <= threshold) continue;
        if (frames_.empty() || frames_.back().frame != e.frame) {
            Frame f;
            f.frame = e.frame;
            f.vars = s.vars;
            frames_.push_back(std::move(f));
        }
        frames_.back().keys.push_back(e.key);
    }
    if (frames_.size() > kMaxFrames)
        frames_.erase(frames_.begin(),
                      frames_.begin() + (frames_.size() - kMaxFrames));
}

int Timeline::indexAtOrBefore(uint32_t frame) const {
    int best = -1;
    for (size_t i = 0; i < frames_.size(); ++i) {
        if (frames_[i].frame > frame) break;
        best = (int)i;
    }
    return best;
}

}  // namespace livedbg
