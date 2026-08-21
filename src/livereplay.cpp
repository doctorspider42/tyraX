#include "livereplay.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace livereplay {
namespace {

// Chunk framing: u32 firstFrame, u16 records, u16 payloadBytes, payload,
// u32 crc32(payload) ^ firstFrame. records == 0 && payloadBytes == 0 is the
// TERMINAL chunk - the writer's way of saying "this file ends here on
// purpose", which is what tells a reader that a short file is finished rather
// than killed.
constexpr int kChunkHeaderBytes = 8;
constexpr int kChunkFooterBytes = 4;

constexpr uint8_t kRecFrame = 0x01;
constexpr uint8_t kRecSeed = 0x02;

void put8(std::vector<unsigned char>& v, uint8_t x) { v.push_back(x); }
void put16(std::vector<unsigned char>& v, uint16_t x) {
    v.push_back((unsigned char)(x & 0xFF));
    v.push_back((unsigned char)(x >> 8));
}
void put32(std::vector<unsigned char>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back((unsigned char)((x >> (i * 8)) & 0xFF));
}
void put64(std::vector<unsigned char>& v, uint64_t x) {
    put32(v, (uint32_t)(x & 0xFFFFFFFFu));
    put32(v, (uint32_t)(x >> 32));
}
void putF32(std::vector<unsigned char>& v, float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    put32(v, bits);
}

uint16_t get16(const unsigned char* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
uint32_t get32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
uint64_t get64(const unsigned char* p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}
float getF32(const unsigned char* p) {
    const uint32_t bits = get32(p);
    float f = 0.0f;
    std::memcpy(&f, &bits, 4);
    return f;
}

/** CRC-32 (the zlib/PNG polynomial), computed the table-free way. This is the
 * twin of the game's own crc32 in the generated runtime; both are small enough
 * that a shared table would cost more than it saves. */
uint32_t crc32(const unsigned char* data, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}

void encodeHeader(std::vector<unsigned char>& v, const Header& h) {
    const size_t start = v.size();
    put32(v, kMagic);
    put32(v, h.version);
    put32(v, h.frameCount);
    put32(v, h.frameRate);
    put32(v, h.flags);
    put32(v, h.chunkFrames);
    put64(v, h.layout);
    put32(v, h.editorFormatVersion);
    put32(v, (uint32_t)h.startScene);
    for (int i = 0; i < 24; ++i)
        v.push_back(i < (int)h.projectName.size()
                        ? (unsigned char)h.projectName[i]
                        : (unsigned char)0);
    (void)start;
}

void encodeFrame(std::vector<unsigned char>& v, const Frame& f,
                 bool wantPad2, bool wantKbd) {
    uint8_t flags = 0;
    if (f.hasFingerprint) flags |= kFrameFingerprint;
    if (wantPad2 && f.hasPad2) flags |= kFramePad2;
    if (wantKbd && f.hasKbd) flags |= kFrameKbd;
    put8(v, kRecFrame);
    put8(v, flags);
    putF32(v, f.dt);
    put16(v, f.pad[0].pressed);
    put16(v, f.pad[0].clicked);
    put8(v, f.pad[0].lh);
    put8(v, f.pad[0].lv);
    put8(v, f.pad[0].rh);
    put8(v, f.pad[0].rv);
    if (flags & kFramePad2) {
        put16(v, f.pad[1].pressed);
        put16(v, f.pad[1].clicked);
        put8(v, f.pad[1].lh);
        put8(v, f.pad[1].lv);
        put8(v, f.pad[1].rh);
        put8(v, f.pad[1].rv);
    }
    if (flags & kFrameKbd) {
        put16(v, (uint16_t)f.kbd.dx);
        put16(v, (uint16_t)f.kbd.dy);
        put8(v, (uint8_t)f.kbd.wheel);
        put8(v, f.kbd.buttons);
        put8(v, f.kbd.clicked);
        const size_t nh = f.kbd.held.size() < (size_t)kMaxKeys
                              ? f.kbd.held.size() : (size_t)kMaxKeys;
        const size_t nc = f.kbd.clickedKeys.size() < (size_t)kMaxKeys
                              ? f.kbd.clickedKeys.size() : (size_t)kMaxKeys;
        put8(v, (uint8_t)nh);
        put8(v, (uint8_t)nc);
        for (size_t i = 0; i < nh; ++i) put8(v, f.kbd.held[i]);
        for (size_t i = 0; i < nc; ++i) put8(v, f.kbd.clickedKeys[i]);
    }
    if (flags & kFrameFingerprint) {
        putF32(v, f.x);
        putF32(v, f.y);
        putF32(v, f.z);
        putF32(v, f.yaw);
        putF32(v, f.pitch);
    }
}

void encodeSeed(std::vector<unsigned char>& v, const SeedEvent& s) {
    put8(v, kRecSeed);
    put16(v, s.volume);
    put32(v, s.seed);
}

void appendChunk(std::vector<unsigned char>& out, uint32_t firstFrame,
                 uint16_t records, const std::vector<unsigned char>& payload) {
    put32(out, firstFrame);
    put16(out, records);
    put16(out, (uint16_t)payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
    put32(out, crc32(payload.data(), payload.size()) ^ firstFrame);
}

}  // namespace

int maxFrameRecordBytes() {
    // kind + flags + dt + pad1 + pad2 + kbd fixed + 2 * kMaxKeys + fingerprint
    return 1 + 1 + 4 + 8 + 8 + 9 + 2 * kMaxKeys + 20;
}

// ------------------------------------------------------------------- parse --

bool parse(const std::vector<unsigned char>& bytes, Recording& out,
           std::string& err) {
    out = Recording();
    if (bytes.size() < (size_t)kHeaderBytes) {
        err = "too short to be a recording";
        return false;
    }
    const unsigned char* b = bytes.data();
    if (get32(b) != kMagic) {
        err = "not a TyraX recording (bad magic)";
        return false;
    }
    Header h;
    h.version = get32(b + 4);
    if (h.version != kVersion) {
        err = "recording format v" + std::to_string(h.version) +
              ", this editor reads v" + std::to_string(kVersion);
        return false;
    }
    h.frameCount = get32(b + 8);
    h.frameRate = get32(b + 12);
    h.flags = get32(b + 16);
    h.chunkFrames = get32(b + 20);
    h.layout = get64(b + 24);
    h.editorFormatVersion = get32(b + 32);
    h.startScene = (int32_t)get32(b + 36);
    {
        const char* nm = reinterpret_cast<const char*>(b + 40);
        size_t n = 0;
        while (n < 24 && nm[n]) ++n;
        h.projectName.assign(nm, nm + n);
    }
    out.header = h;

    size_t p = (size_t)kHeaderBytes;
    uint32_t frameIndex = 0;
    while (p < bytes.size()) {
        if (bytes.size() - p < (size_t)kChunkHeaderBytes) {
            out.truncated = true;
            break;
        }
        const uint32_t firstFrame = get32(b + p);
        const uint16_t records = get16(b + p + 4);
        const uint16_t payloadBytes = get16(b + p + 6);
        if (records == 0 && payloadBytes == 0) {
            // The terminal chunk: a clean end. Anything after it is somebody
            // appending to a finished file, which is not a state to guess at.
            break;
        }
        if (payloadBytes > kMaxChunkPayload) {
            out.truncated = true;
            break;
        }
        const size_t need =
            (size_t)kChunkHeaderBytes + payloadBytes + kChunkFooterBytes;
        if (bytes.size() - p < need) {
            out.truncated = true;
            break;
        }
        const unsigned char* pay = b + p + kChunkHeaderBytes;
        if (get32(pay + payloadBytes) != (crc32(pay, payloadBytes) ^ firstFrame)) {
            // A chunk that does not check out is where the file stopped being
            // trustworthy - keep everything before it and stop.
            out.truncated = true;
            break;
        }
        // The chunk states which frame it starts at, so a reader can notice a
        // gap rather than silently renumber the run around it.
        if (firstFrame != frameIndex) frameIndex = firstFrame;

        size_t q = 0;
        int seen = 0;
        bool bad = false;
        while (q < payloadBytes && seen < records) {
            const uint8_t kind = pay[q++];
            if (kind == kRecSeed) {
                if (payloadBytes - q < 6) { bad = true; break; }
                SeedEvent s;
                s.frame = frameIndex;
                s.volume = get16(pay + q);
                s.seed = get32(pay + q + 2);
                q += 6;
                out.seeds.push_back(s);
                ++seen;
                continue;
            }
            if (kind != kRecFrame) { bad = true; break; }
            if (payloadBytes - q < 13) { bad = true; break; }
            Frame f;
            const uint8_t flags = pay[q];
            f.dt = getF32(pay + q + 1);
            f.pad[0].pressed = get16(pay + q + 5);
            f.pad[0].clicked = get16(pay + q + 7);
            f.pad[0].lh = pay[q + 9];
            f.pad[0].lv = pay[q + 10];
            f.pad[0].rh = pay[q + 11];
            f.pad[0].rv = pay[q + 12];
            q += 13;
            if (flags & kFramePad2) {
                if (payloadBytes - q < 8) { bad = true; break; }
                f.hasPad2 = true;
                f.pad[1].pressed = get16(pay + q);
                f.pad[1].clicked = get16(pay + q + 2);
                f.pad[1].lh = pay[q + 4];
                f.pad[1].lv = pay[q + 5];
                f.pad[1].rh = pay[q + 6];
                f.pad[1].rv = pay[q + 7];
                q += 8;
            }
            if (flags & kFrameKbd) {
                if (payloadBytes - q < 9) { bad = true; break; }
                f.hasKbd = true;
                f.kbd.dx = (int16_t)get16(pay + q);
                f.kbd.dy = (int16_t)get16(pay + q + 2);
                f.kbd.wheel = (int8_t)pay[q + 4];
                f.kbd.buttons = pay[q + 5];
                f.kbd.clicked = pay[q + 6];
                const uint8_t nh = pay[q + 7];
                const uint8_t nc = pay[q + 8];
                q += 9;
                if (nh > kMaxKeys || nc > kMaxKeys ||
                    payloadBytes - q < (size_t)nh + nc) { bad = true; break; }
                f.kbd.held.assign(pay + q, pay + q + nh);
                q += nh;
                f.kbd.clickedKeys.assign(pay + q, pay + q + nc);
                q += nc;
            }
            if (flags & kFrameFingerprint) {
                if (payloadBytes - q < 20) { bad = true; break; }
                f.hasFingerprint = true;
                f.x = getF32(pay + q);
                f.y = getF32(pay + q + 4);
                f.z = getF32(pay + q + 8);
                f.yaw = getF32(pay + q + 12);
                f.pitch = getF32(pay + q + 16);
                q += 20;
            }
            out.frames.push_back(f);
            ++frameIndex;
            ++seen;
        }
        if (bad) {
            // The CRC passed, so this is a format disagreement rather than a
            // damaged file - stop here and keep what parsed.
            out.truncated = true;
            break;
        }
        p += need;
    }

    if (!out.header.finalized() || out.header.frameCount == 0)
        out.header.frameCount = (uint32_t)out.frames.size();
    err.clear();
    return true;
}

bool read(const std::string& path, Recording& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot read " + path;
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    return parse(bytes, out, err);
}

// ------------------------------------------------------------------ encode --

std::vector<unsigned char> encode(const Recording& rec) {
    Header h = rec.header;
    h.version = kVersion;
    h.frameCount = (uint32_t)rec.frames.size();
    h.flags |= kFlagFinalized;
    if (h.chunkFrames == 0) h.chunkFrames = kChunkFramesPcsx2;

    const bool wantPad2 = h.hasPad2();
    const bool wantKbd = h.hasKeyboard();

    std::vector<unsigned char> out;
    out.reserve((size_t)kHeaderBytes + rec.frames.size() * 40);
    encodeHeader(out, h);

    std::vector<unsigned char> payload;
    uint32_t chunkFirst = 0;
    uint16_t records = 0;
    int framesInChunk = 0;
    size_t seedAt = 0;

    auto flush = [&] {
        if (records == 0) return;
        appendChunk(out, chunkFirst, records, payload);
        payload.clear();
        records = 0;
        framesInChunk = 0;
    };

    for (size_t i = 0; i < rec.frames.size(); ++i) {
        // Seeds asked on this frame go in FRONT of it: the game asks for a
        // seed from a scene load, i.e. before that frame's input was applied,
        // and the replay pops them in the same order.
        while (seedAt < rec.seeds.size() && rec.seeds[seedAt].frame <= (uint32_t)i) {
            if (records == 0) chunkFirst = (uint32_t)i;
            encodeSeed(payload, rec.seeds[seedAt++]);
            ++records;
        }
        if (records == 0) chunkFirst = (uint32_t)i;
        encodeFrame(payload, rec.frames[i], wantPad2, wantKbd);
        ++records;
        if (++framesInChunk >= (int)h.chunkFrames) flush();
    }
    while (seedAt < rec.seeds.size()) {
        if (records == 0) chunkFirst = (uint32_t)rec.frames.size();
        encodeSeed(payload, rec.seeds[seedAt++]);
        ++records;
    }
    flush();

    // Terminal chunk - "this file ends here on purpose".
    put32(out, (uint32_t)rec.frames.size());
    put16(out, 0);
    put16(out, 0);
    put32(out, crc32(nullptr, 0) ^ (uint32_t)rec.frames.size());
    return out;
}

std::string finalize(const std::string& rawOut, const std::string& dest) {
    namespace fs = std::filesystem;
    Recording rec;
    std::string err;
    if (!read(rawOut, rec, err)) return err;
    if (rec.frames.empty())
        return "the recording is empty - the game wrote no frames";

    const std::vector<unsigned char> bytes = encode(rec);
    const fs::path target(dest);
    const fs::path tmp(dest + ".tmp");
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return "cannot write " + tmp.string();
        f.write(reinterpret_cast<const char*>(bytes.data()),
                (std::streamsize)bytes.size());
        if (!f) return "write failed: " + tmp.string();
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec)
            return "cannot replace " + target.string() + ": " + ec.message();
    }
    return "";
}

// ------------------------------------------------------------------ status --

bool parseStatus(const std::vector<unsigned char>& bytes, Status& out) {
    if (bytes.size() != (size_t)kStatusBytes) return false;
    const unsigned char* b = bytes.data();
    if (get32(b) != kMagic) return false;
    const uint32_t mode = get32(b + 4);
    if (mode > (uint32_t)Status::Replay) return false;
    Status s;
    s.mode = (int)mode;
    s.frame = get32(b + 8);
    s.divergences = get32(b + 12);
    s.firstDivergent = get32(b + 16);
    s.done = get32(b + 20) != 0;
    s.total = get32(b + 24);
    // The footer echoes the frame counter, the livedbg/livetime torn-write
    // guard: a 32-byte file caught half-rewritten fails here instead of
    // reporting a mixture of two ticks.
    if (get32(b + 28) != (s.frame ^ 0x5A5A5A5Au)) return false;
    out = s;
    return true;
}

bool readStatus(const std::string& path, Status& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    return parseStatus(bytes, out);
}

}  // namespace livereplay
