#include "livedbg.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace livedbg {
namespace {

constexpr uint32_t kSnapMagic = 0x42445854;  // "TXDB" little-endian
constexpr uint32_t kSnapVersion = 5;  // v5 appends the World Facts ring
constexpr int kMaxFlushMap = 64;
constexpr int kSnapHeader = 64;
constexpr uint32_t kCmdMagic = 0x43445854;  // "TXDC"
constexpr uint32_t kCmdVersion = 2;  // v2 appends fact overrides
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
    // v3 is still accepted: a game built before the stats block exists on
    // people's consoles right now, and refusing it would blank the whole
    // Debugger over a tail it simply does not have.
    const uint32_t ver = rd<uint32_t>(b + 4);
    // v3 and v4 are still accepted for the same reason v3 was: a game built
    // before the block exists is running on somebody's console right now, and
    // blanking the whole Debugger over a tail it does not have helps no one.
    if (ver != 3 && ver != 4 && ver != kSnapVersion) return false;

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
    const int timerCount = rd<int32_t>(b + 52);
    const int objCount = rd<int32_t>(b + 60);
    s.hash = (uint64_t)rd<uint32_t>(b + 40) |
             ((uint64_t)rd<uint32_t>(b + 44) << 32);
    if (nodeCount < 0 || nodeCount > kMaxNodes) return false;
    if (varCount < 0 || varCount > 4096) return false;
    if (eventCount < 0 || eventCount > kMaxEvents) return false;
    if (timerCount < 0 || timerCount > kMaxNodes) return false;
    if (objCount < 0 || objCount > kMaxWatchObjects) return false;

    // Minimum size (the watched-object block is variable and is bounds-checked
    // as it is walked; the footer echo below is what catches a torn write).
    const size_t need = (size_t)kSnapHeader + (size_t)nodeCount * 4 +
                        (size_t)eventCount * 4 + (size_t)varCount * 12 +
                        (size_t)timerCount * 4 + 4;
    if (bytes.size() < need) return false;
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
    s.timers.reserve(timerCount);
    for (int i = 0; i < timerCount; ++i, p += 4)
        s.timers.emplace_back((int)rd<uint16_t>(p), (int)rd<uint16_t>(p + 2));
    // Watched objects: (index, sampleCount) then that many 56-byte samples.
    for (int i = 0; i < objCount; ++i) {
        if (p + 4 > bytes.data() + bytes.size()) return false;
        ObjWatch w;
        w.index = (int)(int16_t)rd<uint16_t>(p);
        const int n = (int)rd<uint16_t>(p + 2);
        p += 4;
        if (n < 0 || n > kObjRing) return false;
        if (p + (size_t)n * 56 > bytes.data() + bytes.size()) return false;
        w.samples.reserve(n);
        for (int k = 0; k < n; ++k, p += 56) {
            ObjSample sm;
            sm.frame = rd<uint32_t>(p);
            for (int a = 0; a < 3; ++a) sm.pos[a] = rd<float>(p + 4 + a * 4);
            for (int a = 0; a < 3; ++a) sm.rot[a] = rd<float>(p + 16 + a * 4);
            for (int a = 0; a < 3; ++a) sm.scale[a] = rd<float>(p + 28 + a * 4);
            for (int a = 0; a < 3; ++a) sm.color[a] = rd<float>(p + 40 + a * 4);
            const uint32_t f = rd<uint32_t>(p + 52);
            sm.visible = (f & 1u) != 0;
            sm.active = (f & 2u) != 0;
            sm.dirty = (f & 4u) != 0;
            w.samples.push_back(sm);
        }
        s.objects.push_back(std::move(w));
    }
    // v4 tail: 64 bytes of stats, then the flush map. A v3 snapshot ends at the
    // footer and simply reports no stats.
    if (ver < 4) {
        if (rd<uint32_t>(p) != (s.seq ^ kFooterXor)) return false;
        out = std::move(s);
        return true;
    }
    if (p + 64 + 2 > bytes.data() + bytes.size()) return false;
    Stats st;
    st.valid = true;
    st.fps = rd<uint16_t>(p + 0);
    st.flushes = rd<uint16_t>(p + 2);
    st.qw = rd<uint32_t>(p + 4);
    st.verts = rd<uint32_t>(p + 8);
    st.vramFreeKB = rd<uint32_t>(p + 12);
    st.vramMinFreeKB = rd<uint32_t>(p + 16);
    st.vramLargestKB = rd<uint32_t>(p + 20);
    st.vramResident = rd<uint16_t>(p + 24);
    st.vramPeak = rd<uint16_t>(p + 26);
    st.vramUploads = rd<uint32_t>(p + 28);
    st.vramEvictions = rd<uint32_t>(p + 32);
    st.objects = rd<uint16_t>(p + 36);
    st.objActive = rd<uint16_t>(p + 38);
    st.objVisible = rd<uint16_t>(p + 40);
    st.maxChunkVerts = rd<uint16_t>(p + 42);
    st.ramFreeKB = rd<uint32_t>(p + 44);
    st.ramFrame = rd<uint32_t>(p + 48);
    st.vramBinds = rd<uint32_t>(p + 52);
    st.vramHits = rd<uint32_t>(p + 56);
    // The four spare bytes of the block: tenths of a frame per second,
    // rendered and presented. 0 from a game built before they existed, which
    // is why the panel falls back to the whole-number field rather than
    // showing 0.0.
    st.fpsX10 = rd<uint16_t>(p + 60);
    st.presentedX10 = rd<uint16_t>(p + 62);
    s.stats = st;
    p += 64;
    const int flushCount = (int)rd<uint16_t>(p);
    p += 2;
    if (flushCount < 0 || flushCount > kMaxFlushMap) return false;
    if (p + (size_t)flushCount * 8 + 4 > bytes.data() + bytes.size()) return false;
    s.flushes.reserve(flushCount);
    for (int i = 0; i < flushCount; ++i, p += 8) {
        FlushInfo fi;
        fi.qw = rd<uint16_t>(p + 0);
        fi.unpacks = rd<uint16_t>(p + 2);
        fi.verts = rd<uint16_t>(p + 4);
        fi.program = rd<uint16_t>(p + 6);
        s.flushes.push_back(fi);
    }
    if (ver >= 5) {
        if (p + 2 > bytes.data() + bytes.size()) return false;
        const int factCount = (int)rd<uint16_t>(p);
        p += 2;
        if (factCount < 0 || factCount > kMaxFactEvents) return false;
        if (p + (size_t)factCount * 12 + 4 > bytes.data() + bytes.size())
            return false;
        s.factEvents.reserve((size_t)factCount);
        for (int i = 0; i < factCount; ++i, p += 12) {
            FactEvent fe;
            fe.slot = (int)rd<uint16_t>(p + 0);
            fe.src = (int)rd<int16_t>(p + 2);
            // The game sends an AGE in frames; absolute numbers are rebuilt
            // from the header's counter, exactly like the node event ring.
            const uint32_t age = rd<uint16_t>(p + 4);
            fe.frame = s.frame >= age ? s.frame - age : 0;
            fe.value = rd<float>(p + 8);
            s.factEvents.push_back(fe);
        }
    }

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
           fire == o.fire && fireAndRun == o.fireAndRun &&
           captureVu == o.captureVu && vuFlush == o.vuFlush &&
           measureRam == o.measureRam && watchObjects == o.watchObjects &&
           sameFactSets(o);
}

bool Command::sameFactSets(const Command& o) const {
    if (factSets.size() != o.factSets.size()) return false;
    for (size_t i = 0; i < factSets.size(); ++i) {
        const FactSet& a = factSets[i];
        const FactSet& b = o.factSets[i];
        if (a.slot != b.slot || a.isPosition != b.isPosition) return false;
        for (int k = 0; k < 3; ++k)
            if (a.v[k] != b.v[k]) return false;
    }
    return true;
}

std::vector<unsigned char> encodeCommand(const Command& c) {
    std::vector<unsigned char> v;
    v.reserve(kCmdHeader + (c.breakpoints.size() + c.fire.size()) * 2 + 4);
    put32(v, kCmdMagic);
    put32(v, kCmdVersion);
    put32(v, c.seq);
    // Bits 0-3 are the switches; a capture with a named flush index sets bit 4
    // and carries the index in bits 8-23 (see Command::vuFlush). Spare bits
    // rather than a longer header: a game built before this reads the switches
    // it knows and ignores the rest, so no version bump is needed on either
    // side.
    uint32_t flags = (c.halt ? 1u : 0u) | (c.stepUntilFire ? 2u : 0u) |
                     (c.fireAndRun ? 4u : 0u) | (c.captureVu ? 8u : 0u);
    if (c.captureVu && c.vuFlush >= 0)
        flags |= 16u | ((uint32_t)(c.vuFlush & 0xFFFF) << 8);
    if (c.measureRam) flags |= 32u;
    // Fact overrides ride the top byte: the header is full at 32 bytes and a
    // count capped at kMaxFactSets has nowhere better to live. A game built
    // before they existed reads those bits as 0, i.e. "no overrides".
    const size_t factCount = std::min(c.factSets.size(), (size_t)kMaxFactSets);
    flags |= (uint32_t)(factCount & 0xFFu) << 24;
    put32(v, flags);
    put32(v, (uint32_t)(int32_t)c.stepFrames);
    put32(v, (uint32_t)(int32_t)c.breakpoints.size());
    put32(v, (uint32_t)(int32_t)c.fire.size());
    put32(v, (uint32_t)(int32_t)c.watchObjects.size());
    for (uint16_t k : c.breakpoints) put16(v, k);
    for (uint16_t k : c.fire) put16(v, k);
    for (uint16_t k : c.watchObjects) put16(v, k);
    for (size_t i = 0; i < factCount; ++i) {
        const FactSet& f = c.factSets[i];
        put16(v, (uint16_t)f.slot);
        v.push_back(f.isPosition ? 1 : 0);
        v.push_back(0);  // pad to a 4-byte boundary for the floats
        for (int k = 0; k < 3; ++k) {
            uint32_t bits;
            std::memcpy(&bits, &f.v[k], 4);
            put32(v, bits);
        }
    }
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
