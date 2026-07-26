#include "vucap.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace vucap {
namespace {

uint32_t rd32(const unsigned char* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
float rdF(const unsigned char* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

// VIF1 command names we care about. The full set is large; anything unknown is
// reported by number rather than guessed at.
std::string vifName(uint32_t cmd) {
    switch (cmd) {
        case 0x00: return "NOP";
        case 0x01: return "STCYCL";
        case 0x02: return "OFFSET";
        case 0x03: return "BASE";
        case 0x04: return "ITOP";
        case 0x05: return "STMOD";
        case 0x06: return "MSKPATH3";
        case 0x07: return "MARK";
        case 0x10: return "FLUSHE";
        case 0x11: return "FLUSH";
        case 0x13: return "FLUSHA";
        case 0x14: return "MSCAL";
        case 0x15: return "MSCALF";
        case 0x17: return "MSCNT";
        case 0x20: return "STMASK";
        case 0x30: return "STROW";
        case 0x31: return "STCOL";
        case 0x4A: return "MPG";
        case 0x50: return "DIRECT";
        case 0x51: return "DIRECTHL";
        default: return "";
    }
}

// UNPACK formats: cmd 0x60 | (m << 4) | (vn << 2) | vl.
std::string unpackFormat(uint32_t cmd, int* wordsPerItem) {
    const int vn = (int)((cmd >> 2) & 3);   // 0 = S, 1 = V2, 2 = V3, 3 = V4
    const int vl = (int)(cmd & 3);          // 0 = 32, 1 = 16, 2 = 8, 3 = V4_5
    static const char* kVn[4] = {"S", "V2", "V3", "V4"};
    static const char* kVl[4] = {"32", "16", "8", "5"};
    const int comps = vn + 1;
    const int bits = vl == 0 ? 32 : vl == 1 ? 16 : vl == 2 ? 8 : 5;
    // Quadword-aligned: every item is padded to a whole number of words for the
    // formats this engine uses (V4_32 = 4 words, V4_8 = 1 word).
    *wordsPerItem = vl == 0 ? comps : (comps * bits + 31) / 32;
    std::string f = std::string(kVn[vn]) + "_" + kVl[vl];
    if (cmd & 0x10) f += " (masked)";
    return f;
}

}  // namespace

const std::vector<float>* Capture::vertices() const {
    if (vertexUnpack < 0 || vertexUnpack >= (int)unpacks.size()) return nullptr;
    return &unpacks[vertexUnpack].floats;
}

int Capture::triangleCount() const {
    const std::vector<float>* v = vertices();
    return v ? (int)(v->size() / 4) / 3 : 0;
}

bool load(const std::string& path, Capture& out) {
    out = Capture();
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        out.error = "no capture yet";
        return false;
    }
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
    if (b.size() < 16 || std::memcmp(b.data(), "TXVU", 4) != 0) {
        out.error = "not a VU capture";
        return false;
    }
    const uint32_t ver = rd32(&b[4]);
    if (ver != 1u && ver != 2u) {
        out.error = "unknown capture version";
        return false;
    }
    out.frame = rd32(&b[8]);
    const uint32_t packed = rd32(&b[12]);
    out.qw = (int)(ver == 1 ? packed : (packed & 0xFFFF));
    const int blockCount = ver == 1 ? 0 : (int)(packed >> 16);
    const unsigned char* p = b.data() + 16;
    size_t bytes = b.size() - 16;
    if ((size_t)out.qw * 16 > bytes) out.qw = (int)(bytes / 16);

    // v2: after the chain come `blockCount` index entries (chain quadword,
    // quadword count) and then the referenced data itself - the vertex arrays
    // the pipeline sends by reference rather than inline.
    struct Block {
        int at, qw;
        const unsigned char* data;
    };
    std::vector<Block> blocks;
    {
        const unsigned char* idx = p + (size_t)out.qw * 16;
        const unsigned char* data = idx + (size_t)blockCount * 4;
        size_t used = 0;
        for (int i = 0; i < blockCount; ++i) {
            uint16_t at, qwc;
            std::memcpy(&at, idx + i * 4, 2);
            std::memcpy(&qwc, idx + i * 4 + 2, 2);
            if (data + used + (size_t)qwc * 16 > b.data() + b.size()) break;
            blocks.push_back({(int)at, (int)qwc, data + used});
            used += (size_t)qwc * 16;
        }
    }

    // Walk the chain. The advance rule is the thing to get right: `cnt`/`next`
    // carry their data INLINE (skip 1 + qwc), while `ref`/`refs`/`refe` point at
    // it (the tag is one quadword, and the data arrived as a block above).
    int at = 0;
    char line[192];
    while (at < out.qw) {
        const unsigned char* q = p + (size_t)at * 16;
        const uint32_t w0 = rd32(q), w1 = rd32(q + 4);
        const uint32_t vif0 = rd32(q + 8), vif1 = rd32(q + 12);
        const int qwc = (int)(w0 & 0xFFFF);
        const int id = (int)((w0 >> 28) & 0x7);
        const bool byRef = (id == 0 || id == 3 || id == 4);
        static const char* kId[8] = {"refe", "cnt",  "next", "ref",
                                     "refs", "call", "ret",  "end"};
        std::snprintf(line, sizeof(line), "DMAtag %-4s qwc=%d%s", kId[id], qwc,
                      byRef ? " (data by reference)" : " (inline)");
        out.steps.push_back({(uint32_t)at, line});

        const unsigned char* payload = byRef ? nullptr : q + 16;
        int payloadQw = byRef ? 0 : qwc;
        if (byRef)
            for (const Block& bl : blocks)
                if (bl.at == at) {
                    payload = bl.data;
                    payloadQw = bl.qw;
                    break;
                }

        for (int k = 0; k < 2; ++k) {
            const uint32_t code = k ? vif1 : vif0;
            const uint32_t cmd = (code >> 24) & 0xFF;
            const uint32_t num = (code >> 16) & 0xFF;
            const uint32_t imm = code & 0xFFFF;
            if (!code) continue;
            if ((cmd & 0x60) == 0x60) {
                int wordsPerItem = 4;
                Unpack u;
                u.format = unpackFormat(cmd, &wordsPerItem);
                u.count = (int)(num ? num : 256);
                u.vuAddr = imm & 0x3FF;
                u.useTops = (imm & 0x8000) != 0;
                const size_t need = (size_t)u.count * wordsPerItem * 4;
                const size_t avail = (size_t)payloadQw * 16;
                const size_t take = payload ? (need < avail ? need : avail) : 0;
                for (size_t o = 0; o + 4 <= take; o += 4) {
                    u.words.push_back(rd32(payload + o));
                    u.floats.push_back(rdF(payload + o));
                }
                std::snprintf(line, sizeof(line),
                              "  VIF UNPACK %-12s num=%d -> VU1 addr %u%s%s",
                              u.format.c_str(), u.count, u.vuAddr,
                              u.useTops ? " (+TOPS)" : "",
                              payload ? "" : " [payload not captured]");
                out.steps.push_back({(uint32_t)at, line});
                out.unpacks.push_back(std::move(u));
            } else {
                const std::string name = vifName(cmd);
                if (cmd == 0x14 || cmd == 0x15) out.mscal.push_back((int)imm);
                std::snprintf(line, sizeof(line), "  VIF %-10s num=%d imm=%u",
                              name.empty() ? "?" : name.c_str(), (int)num,
                              (unsigned)imm);
                out.steps.push_back({(uint32_t)at, line});
            }
        }
        at += byRef ? 1 : 1 + qwc;
        if (id == 0 || id == 7) break;  // refe / end terminate the chain
    }

    // The vertex stream is the largest V4_32 unpack: the small blocks are the
    // scales, the GIF tag and the matrices.
    size_t best = 0;
    for (size_t i = 0; i < out.unpacks.size(); ++i) {
        if (out.unpacks[i].format.rfind("V4_32", 0) != 0) continue;
        if (out.unpacks[i].floats.size() > best) {
            best = out.unpacks[i].floats.size();
            out.vertexUnpack = (int)i;
        }
    }
    out.loaded = true;
    return true;
}

}  // namespace vucap
