#include "livetime.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace livetime {
namespace {

constexpr uint32_t kMagic = 0x4D545854;    // "TXTM" little-endian
constexpr uint32_t kVersion = 1;
constexpr int kHeader = 48;
constexpr uint32_t kFooterXor = 0x5A5A5A5AU;  // livedbg's torn-write guard

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

}  // namespace

// ---------------------------------------------------------------- snapshot ---

bool parseSnapshot(const std::vector<unsigned char>& bytes, Snapshot& out) {
    if (bytes.size() < (size_t)kHeader + 4) return false;
    const unsigned char* b = bytes.data();
    if (rd<uint32_t>(b + 0) != kMagic) return false;
    if (rd<uint32_t>(b + 4) != kVersion) return false;

    Snapshot s;
    s.seq = rd<uint32_t>(b + 8);
    s.frame = rd<uint32_t>(b + 12);
    s.scene = rd<int32_t>(b + 16);
    const uint32_t stateBytes = rd<uint32_t>(b + 20);
    s.layout = (uint64_t)rd<uint32_t>(b + 24) |
               ((uint64_t)rd<uint32_t>(b + 28) << 32);
    s.objectCount = (int)rd<uint32_t>(b + 36);
    if (stateBytes > kMaxStateBytes) return false;
    if (bytes.size() < (size_t)kHeader + stateBytes + 4) return false;
    // The footer echoes the sequence number, so a file caught half-rewritten
    // (new header, old tail) fails here instead of restoring a mixed state -
    // which would be the one failure mode worse than missing a capture.
    if (rd<uint32_t>(b + kHeader + stateBytes) != (s.seq ^ kFooterXor)) return false;

    s.state.assign(b + kHeader, b + kHeader + stateBytes);
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

std::vector<unsigned char> encodeSnapshot(const Snapshot& s) {
    std::vector<unsigned char> v;
    v.reserve((size_t)kHeader + s.state.size() + 4);
    put32(v, kMagic);
    put32(v, kVersion);
    put32(v, s.seq);
    put32(v, s.frame);
    put32(v, (uint32_t)s.scene);
    put32(v, (uint32_t)s.state.size());
    put32(v, (uint32_t)(s.layout & 0xFFFFFFFFu));
    put32(v, (uint32_t)(s.layout >> 32));
    put32(v, 0);  // flags (reserved)
    put32(v, (uint32_t)s.objectCount);
    put32(v, 0);  // reserved
    put32(v, 0);  // reserved
    v.insert(v.end(), s.state.begin(), s.state.end());
    put32(v, s.seq ^ kFooterXor);
    return v;
}

std::string writeRestore(const std::string& path, const Snapshot& s) {
    namespace fs = std::filesystem;
    const std::vector<unsigned char> bytes = encodeSnapshot(s);
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
    // Rename over the old file so the game never reads a half-written restore.
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) return "cannot replace " + target.string() + ": " + ec.message();
    }
    return "";
}

// ---------------------------------------------------------------- history ---

void History::setBudget(size_t bytes) {
    budget_ = bytes;
    while (bytes_ > budget_ && !snaps_.empty()) {
        bytes_ -= snaps_.front().bytes();
        snaps_.pop_front();
    }
}

bool History::ingest(const Snapshot& s) {
    if (!snaps_.empty() && s.seq == lastSeq_) return false;
    // A frame that went backwards is a restarted game, not a rewind: a restore
    // deliberately leaves the frame counter running forward (it is this
    // history's ordering key), so the only way to see one go back is a fresh
    // boot - and that history is not a continuation of this one.
    if (!snaps_.empty() && s.frame < lastFrame_) clear();
    lastSeq_ = s.seq;
    lastFrame_ = s.frame;
    bytes_ += s.bytes();
    snaps_.push_back(s);
    while (bytes_ > budget_ && snaps_.size() > 1) {
        bytes_ -= snaps_.front().bytes();
        snaps_.pop_front();
    }
    return true;
}

void History::clear() {
    snaps_.clear();
    bytes_ = 0;
    lastSeq_ = 0;
    lastFrame_ = 0;
}

int History::indexAtOrBefore(uint32_t frame) const {
    int best = -1;
    for (size_t i = 0; i < snaps_.size(); ++i) {
        if (snaps_[i].frame > frame) break;  // oldest first
        best = (int)i;
    }
    return best;
}

uint32_t History::frameSpan() const {
    if (snaps_.size() < 2) return 0;
    return snaps_.back().frame - snaps_.front().frame;
}

}  // namespace livetime
