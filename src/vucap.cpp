#include "vucap.hpp"

#include <cstdio>
#include <cmath>
#include <cstdlib>
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

float Mesh::extent() const {
    float e = 0.0f;
    for (int a = 0; a < 3; ++a) {
        const float d = hi[a] - lo[a];
        if (d > e) e = d;
    }
    return e;
}

const std::vector<float>* Capture::verticesOf(int i) const {
    if (i < 0 || i >= (int)meshes.size()) return nullptr;
    const int u = meshes[i].unpack;
    if (u < 0 || u >= (int)unpacks.size()) return nullptr;
    return &unpacks[u].floats;
}

int Capture::triangleCount() const {
    const std::vector<float>* v = vertices();
    return v ? (int)(v->size() / 4) / 3 : 0;
}


// --------------------------------------------------------------- v3 decode ---

std::string GifPacket::primName() const {
    static const char* kPrim[8] = {"POINT",    "LINE",      "LINE_STRIP",
                                   "TRIANGLE", "TRI_STRIP", "TRI_FAN",
                                   "SPRITE",   "?"};
    std::string s = kPrim[prim & 7];
    if (prim & (1u << 4)) s += " +TEX";
    if (prim & (1u << 5)) s += " +FOG";
    if (prim & (1u << 6)) s += " +ABE";
    if (prim & (1u << 7)) s += " +AA1";
    return s;
}

int Capture::outputVerts() const {
    int n = 0;
    for (const GifPacket& g : gifs)
        if (g.hasGeometry) n += (int)g.verts.size();
    return n;
}

int Capture::inputTris() const {
    if (meshes.empty()) return triangleCount();
    int n = 0;
    for (const Mesh& m : meshes) n += m.tris;
    return n;
}

int Capture::outputTris() const {
    int out = 0;
    for (const GifPacket& g : gifs)
        if (g.hasGeometry) out += (int)g.verts.size() / 3;
    return out;
}

int Capture::clipDelta() const {
    const int last = meshes.empty() ? triangleCount() : meshes.back().tris;
    return outputTris() - last;
}

namespace {

// GS register names, for the REGS list of a GIFtag.
const char* gsRegName(uint32_t r) {
    switch (r) {
        case 0x0: return "PRIM";
        case 0x1: return "RGBAQ";
        case 0x2: return "ST";
        case 0x3: return "UV";
        case 0x4: return "XYZF2";
        case 0x5: return "XYZ2";
        case 0xA: return "FOG";
        case 0xD: return "XYZ3";
        case 0xE: return "A+D";
        case 0xF: return "NOP";
        default: return "?";
    }
}

// Does this quadword look like the GIFtag this pipeline emits? PACKED mode,
// 1..4 registers, a sane NLOOP and a register list of known values. Strict on
// purpose: a false positive would decode noise as geometry.
bool looksLikeGifTag(const uint32_t* q, int* nloop, int* nreg, uint32_t* prim,
                     bool* eop, bool* pre) {
    const uint32_t w0 = q[0], w1 = q[1];
    const int nl = (int)(w0 & 0x7FFF);
    const int nr = (int)((w1 >> 28) & 0xF);
    const int flg = (int)((w1 >> 26) & 0x3);
    if (nl <= 0 || nl > 512) return false;
    if (nr < 1 || nr > 4) return false;
    if (flg != 0) return false;  // PACKED only
    for (int i = 0; i < nr; ++i) {
        const uint32_t r = (q[2] >> (i * 4)) & 0xF;
        if (r > 0x5 && r != 0xA && r != 0xD && r != 0xE && r != 0xF) return false;
    }
    *nloop = nl;
    *nreg = nr;
    *eop = ((w0 >> 15) & 1) != 0;
    *pre = ((w1 >> 14) & 1) != 0;
    *prim = (w1 >> 15) & 0x7FF;
    return true;
}

}  // namespace

// Scans VU1 data memory for the GIF packets the microprogram staged, decodes
// their vertices, and compares them against a host transform of the same input.
static void decodeVuMem(Capture& out) {
    if (out.vuMem.size() < 16) return;

    // The MVP matrix sits at quadword 0 (VU1_MVP_MATRIX_ADDR): the pipeline
    // uploads it per mesh and nothing in the run overwrites it.
    out.hasMvp = true;
    for (int i = 0; i < 16; ++i) std::memcpy(&out.mvp[i], &out.vuMem[i], 4);

    // The scales the program multiplies by are the first V4_32 unpack of the
    // chain: (2048, 2048, 0xFFFFFF/2, vertexCount).
    for (const Unpack& u : out.unpacks) {
        if (u.vuAddr == 0 && u.floats.size() >= 3 && u.floats[0] > 1.0f) {
            out.scale[0] = u.floats[0];
            out.scale[1] = u.floats[1];
            out.scale[2] = u.floats[2];
            break;
        }
    }

    // Walk memory for GIF tags; each is followed by NLOOP * NREG quadwords.
    const int words = (int)out.vuMem.size();
    for (int qw = 0; (qw + 1) * 4 <= words;) {
        const uint32_t* q = &out.vuMem[(size_t)qw * 4];
        int nloop = 0, nreg = 0;
        uint32_t prim = 0;
        bool eop = false, pre = false;
        if (!looksLikeGifTag(q, &nloop, &nreg, &prim, &eop, &pre)) {
            ++qw;
            continue;
        }
        if ((qw + 1 + nloop * nreg) * 4 > words) {
            ++qw;
            continue;
        }
        GifPacket g;
        g.vuAddr = qw;
        g.nloop = nloop;
        g.nreg = nreg;
        g.eop = eop;
        g.pre = pre;
        g.prim = prim;
        int regIds[4] = {0, 0, 0, 0};
        for (int i = 0; i < nreg; ++i) {
            regIds[i] = (int)((q[2] >> (i * 4)) & 0xF);
            g.regs += (i ? ", " : "");
            g.regs += gsRegName((uint32_t)regIds[i]);
        }
        // PACKED mode: one quadword per register per vertex, in REGS order.
        for (int v = 0; v < nloop; ++v) {
            GsVertex gv;
            for (int r = 0; r < nreg; ++r) {
                const uint32_t* d =
                    &out.vuMem[(size_t)(qw + 1 + v * nreg + r) * 4];
                switch (regIds[r]) {
                    case 0x4:  // XYZF2: X bits 0-15, Y bits 32-47, Z bits 68-91
                        gv.x = (int)(d[0] & 0xFFFF);
                        gv.y = (int)(d[1] & 0xFFFF);
                        gv.z = (d[2] >> 4) & 0xFFFFFF;
                        break;
                    case 0x5:  // XYZ2: Z is the full word
                        gv.x = (int)(d[0] & 0xFFFF);
                        gv.y = (int)(d[1] & 0xFFFF);
                        gv.z = d[2];
                        break;
                    case 0x1:  // RGBAQ
                        gv.r = (uint8_t)(d[0] & 0xFF);
                        gv.g = (uint8_t)(d[1] & 0xFF);
                        gv.b = (uint8_t)(d[2] & 0xFF);
                        gv.a = (uint8_t)(d[3] & 0xFF);
                        break;
                    case 0x2:  // ST + Q
                        std::memcpy(&gv.s, &d[0], 4);
                        std::memcpy(&gv.t, &d[1], 4);
                        std::memcpy(&gv.q, &d[2], 4);
                        break;
                    default: break;
                }
            }
            g.verts.push_back(gv);
        }
        for (int i = 0; i < nreg; ++i)
            if (regIds[i] == 0x4 || regIds[i] == 0x5) g.hasGeometry = true;
        out.gifs.push_back(g);
        qw += 1 + nloop * nreg;
    }

    // The host reference: the same transform the microprogram performs (see
    // ScaleVertexToGSFormat in vcl_sml.i) - clip = MVP * v, ndc = clip / clip.w,
    // screen = scale * (ndc + 1), fixed = ftoi4(screen) = trunc(screen * 16).
    //
    // Screen Y needs one measured fact rather than an assumption: the GS Y axis
    // grows DOWNWARD, so a pipeline may or may not have folded the flip into its
    // projection. Both conventions are computed here and the diff below keeps
    // whichever matches the hardware - and reports which one that was, so a
    // change in the projection shows up as the convention flipping rather than
    // as a silent 400-pixel error.
    const std::vector<float>* in = out.vertices();
    if (in && out.hasMvp) {
        const size_t n = in->size() / 4;
        for (size_t i = 0; i < n; ++i) {
            const float v[4] = {(*in)[i * 4 + 0], (*in)[i * 4 + 1],
                                (*in)[i * 4 + 2], 1.0f};
            RefVertex r;
            for (int row = 0; row < 4; ++row) {
                float acc = 0.0f;
                for (int c = 0; c < 4; ++c) acc += out.mvp[c * 4 + row] * v[c];
                r.clip[row] = acc;
            }
            if (r.clip[3] <= 0.0001f) {
                r.behind = true;
            } else {
                const float inv = 1.0f / r.clip[3];
                const float nx = r.clip[0] * inv;
                const float ny = r.clip[1] * inv;
                const float nz = r.clip[2] * inv;
                r.x = (int)((nx + 1.0f) * out.scale[0] * 16.0f);
                r.y = (int)((ny + 1.0f) * out.scale[1] * 16.0f);
                r.yFlipped = (int)((1.0f - ny) * out.scale[1] * 16.0f);
                r.z = (uint32_t)((nz + 1.0f) * out.scale[2] * 16.0f) >> 4;
            }
            out.reference.push_back(r);
        }
    }
    // Diff the biggest GEOMETRY packet against the reference, vertex by vertex.
    // They line up 1:1 only when nothing was clipped or reordered - which is
    // exactly the case worth checking, and the counts say when it is not.
    const GifPacket* big = nullptr;
    for (const GifPacket& g : out.gifs) {
        if (!g.hasGeometry) continue;
        if (!big || g.verts.size() > big->verts.size()) big = &g;
    }
    if (big && !out.reference.empty()) {
        const size_t n = big->verts.size() < out.reference.size()
                             ? big->verts.size()
                             : out.reference.size();
        double sx = 0, syUp = 0, syDown = 0;
        float maxUp = 0, maxDown = 0, maxX = 0;
        int compared = 0;
        for (size_t i = 0; i < n; ++i) {
            if (out.reference[i].behind) continue;
            const float dx =
                (float)std::abs(big->verts[i].x - out.reference[i].x);
            const float dUp =
                (float)std::abs(big->verts[i].y - out.reference[i].y);
            const float dDown =
                (float)std::abs(big->verts[i].y - out.reference[i].yFlipped);
            if (dx > maxX) maxX = dx;
            if (dUp > maxUp) maxUp = dUp;
            if (dDown > maxDown) maxDown = dDown;
            sx += dx;
            syUp += dUp;
            syDown += dDown;
            ++compared;
        }
        out.diffCompared = compared;
        if (compared) {
            out.yFlipped = syDown < syUp;
            out.diffMaxX = maxX;
            out.diffMaxY = out.yFlipped ? maxDown : maxUp;
            out.diffMeanX = (float)(sx / compared);
            out.diffMeanY =
                (float)((out.yFlipped ? syDown : syUp) / compared);
        }
    }

    // --- what looks wrong ---------------------------------------------------
    // The GS draws a renderWidth x renderHeight window centred on 2048 of the
    // 4096-unit plane (RendererCoreGS sets XYOFFSET = 2048 - size/2), and
    // GsVertex::px() reports that plane. So "is this vertex on screen" is a
    // range check - the single most useful thing to ask of a staged packet.
    if (out.renderWidth > 0 && out.renderHeight > 0) {
        out.hasWindow = true;
        out.winX0 = 2048.0f - out.renderWidth * 0.5f;
        out.winX1 = 2048.0f + out.renderWidth * 0.5f;
        out.winY0 = 2048.0f - out.renderHeight * 0.5f;
        out.winY1 = 2048.0f + out.renderHeight * 0.5f;
    }
    // Scoped to ONE packet on purpose. VU1 data memory is not cleared between
    // runs, so a scan of all 1024 quadwords also turns up whatever earlier
    // meshes (and earlier frames) left lying there - counting every vertex it
    // finds produces confident nonsense like "60 of 60 vertices are off
    // screen". The biggest geometry packet is the one this run staged, and it
    // is the same packet the host reference is diffed against.
    if (big && !big->verts.empty()) {
        const bool blends = (big->prim & (1u << 6)) != 0;  // +ABE
        out.gsX0 = out.gsX1 = big->verts[0].px();
        out.gsY0 = out.gsY1 = big->verts[0].py();
        for (const GsVertex& v : big->verts) {
            ++out.gsVerts;
            if (out.hasWindow &&
                (v.px() < out.winX0 || v.px() > out.winX1 ||
                 v.py() < out.winY0 || v.py() > out.winY1))
                ++out.gsOffWindow;
            if (v.a == 0 && !blends) ++out.gsZeroAlpha;
            if (v.px() < out.gsX0) out.gsX0 = v.px();
            if (v.px() > out.gsX1) out.gsX1 = v.px();
            if (v.py() < out.gsY0) out.gsY0 = v.py();
            if (v.py() > out.gsY1) out.gsY1 = v.py();
        }
        out.packetOffscreen =
            out.hasWindow &&
            (out.gsX1 < out.winX0 || out.gsX0 > out.winX1 ||
             out.gsY1 < out.winY0 || out.gsY0 > out.winY1);
        // The classic PS2 smear: a vertex whose W went to (or past) zero comes
        // out the far side of the plane and drags its triangle across the
        // screen. It cannot be spotted per vertex - the packed field is 16 bits
        // and has already wrapped - so the tell is the triangle's SIZE. The
        // threshold is deliberately near the size of the whole 4096-unit plane:
        // a terrain quad at the camera legitimately spans several screens (the
        // guard band exists for exactly that), and a warning that fires on
        // ordinary geometry is worse than no warning at all.
        if ((big->prim & 7) == 3)  // TRIANGLE list
            for (size_t t = 0; t + 2 < big->verts.size(); t += 3) {
                float x0 = big->verts[t].px(), x1 = x0, y0 = big->verts[t].py(),
                      y1 = y0;
                for (size_t k = 1; k < 3; ++k) {
                    const float x = big->verts[t + k].px(),
                                y = big->verts[t + k].py();
                    x0 = x < x0 ? x : x0;
                    x1 = x > x1 ? x : x1;
                    y0 = y < y0 ? y : y0;
                    y1 = y > y1 ? y : y1;
                }
                if (x1 - x0 > 3500.0f || y1 - y0 > 3500.0f) ++out.hugeTris;
            }
    }
    // Only when the flush carried ONE mesh. The reference transforms the
    // largest mesh's vertices with the MVP left in VU1 memory - the LAST mesh's
    // - so on a multi-mesh flush "behind the camera" would be an artefact of
    // pairing the wrong matrix with the wrong vertices. Silence beats a
    // confident wrong answer.
    if (out.meshes.size() <= 1)
        for (const RefVertex& r : out.reference)
            if (r.behind) ++out.behindVerts;
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
    if (ver < 1u || ver > 4u) {
        out.error = "unknown capture version";
        return false;
    }
    out.version = ver;
    out.frame = rd32(&b[8]);
    const uint32_t packed = rd32(&b[12]);
    out.qw = (int)(ver == 1 ? packed : (packed & 0xFFFF));
    const int blockCount = ver == 1 ? 0 : (int)(packed >> 16);
    // v4 doubled the header: which bag flush of the frame this is, how many the
    // frame sent, and the resolution the game is actually rendering at (decided
    // at runtime, so the editor cannot know it from the project settings).
    size_t headerSize = 16;
    if (ver >= 4u) {
        headerSize = 32;
        if (b.size() < headerSize) {
            out.error = "truncated capture header";
            return false;
        }
        out.flushIndex = (int)rd32(&b[16]);
        out.flushCount = (int)rd32(&b[20]);
        out.renderWidth = (int)rd32(&b[24]);
        out.renderHeight = (int)rd32(&b[28]);
    }
    const unsigned char* p = b.data() + headerSize;
    size_t bytes = b.size() - headerSize;
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
    // Which microprogram each block is run UNDER is not in the UNPACK: it is the
    // MSCAL/MSCNT that follows the mesh's blocks. MSCAL names an entry address,
    // MSCNT re-runs the last one - so the unpacks queue up here and are stamped
    // when the call arrives. A chain with two different MSCAL addresses ran two
    // programs, which is worth seeing.
    int lastProgram = -1;
    std::vector<size_t> pending;
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
                pending.push_back(out.unpacks.size());
                out.unpacks.push_back(std::move(u));
            } else {
                const std::string name = vifName(cmd);
                if (cmd == 0x14 || cmd == 0x15) {  // MSCAL / MSCALF
                    out.mscal.push_back((int)imm);
                    lastProgram = (int)imm;
                }
                if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17) {  // ...or MSCNT
                    for (size_t ui : pending) out.unpacks[ui].program = lastProgram;
                    pending.clear();
                }
                std::snprintf(line, sizeof(line), "  VIF %-10s num=%d imm=%u",
                              name.empty() ? "?" : name.c_str(), (int)num,
                              (unsigned)imm);
                out.steps.push_back({(uint32_t)at, line});
            }
        }
        at += byRef ? 1 : 1 + qwc;
        if (id == 0 || id == 7) break;  // refe / end terminate the chain
    }

    // Which unpacks are POSITIONS? One flush carries a whole bag - a dozen
    // meshes, each sending its positions and then a second same-sized V4_32
    // array (ST/Q). They are told apart by the w component: the pipeline packs
    // positions as (x, y, z, 1.0) and nothing else in the chain has a constant
    // 1.0 there. Everything that passes is a mesh of its own, in send order.
    for (size_t i = 0; i < out.unpacks.size(); ++i) {
        Unpack& u = out.unpacks[i];
        if (u.format.rfind("V4_32", 0) != 0) {
            u.posNote = "not V4_32";
            continue;
        }
        const size_t items = u.floats.size() / 4;
        if (items < 3) {
            u.posNote = "fewer than 3 items";
            continue;
        }
        size_t badW = 0;
        for (size_t k = 0; k < items; ++k)
            if (u.floats[k * 4 + 3] != 1.0f) ++badW;
        if (badW) {
            char b[64];
            std::snprintf(b, sizeof(b), "w != 1.0 on %zu of %zu items", badW,
                          items);
            u.posNote = b;
            continue;
        }
        // A count that is not a multiple of 3 is NOT a rejection: a mesh sent
        // as a strip or a fan is a perfectly real mesh, and dropping it would
        // hide geometry from the list. The triangle figure below is the
        // triangle-LIST reading, which is what this pipeline sends today.
        if (items % 3 != 0) u.posNote = "not a triangle list (strip or fan?)";
        Mesh m;
        m.unpack = (int)i;
        m.verts = (int)items;
        m.tris = (int)items / 3;
        m.program = u.program;
        for (int a = 0; a < 3; ++a) {
            m.lo[a] = u.floats[a];
            m.hi[a] = u.floats[a];
        }
        for (size_t k = 0; k < items; ++k)
            for (int a = 0; a < 3; ++a) {
                const float v = u.floats[k * 4 + a];
                if (v < m.lo[a]) m.lo[a] = v;
                if (v > m.hi[a]) m.hi[a] = v;
            }
        // A triangle with no area draws nothing and costs the same as one that
        // does - a duplicated index or a collapsed strip shows up here.
        for (size_t t = 0; t + 2 < items; t += 3) {
            const float* A = &u.floats[t * 4];
            const float* B = &u.floats[(t + 1) * 4];
            const float* C = &u.floats[(t + 2) * 4];
            const float ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2];
            const float vx = C[0] - A[0], vy = C[1] - A[1], vz = C[2] - A[2];
            const float cx = uy * vz - uz * vy, cy = uz * vx - ux * vz,
                        cz = ux * vy - uy * vx;
            if (cx * cx + cy * cy + cz * cz < 1e-12f) ++m.degenerate;
        }
        out.degenerateTris += m.degenerate;
        out.meshes.push_back(m);
    }
    // The one the header counts and the host reference is diffed against stays
    // the largest stream, so the numbers below mean what they always did.
    size_t best = 0;
    for (const Mesh& m : out.meshes)
        if (out.unpacks[m.unpack].floats.size() > best) {
            best = out.unpacks[m.unpack].floats.size();
            out.vertexUnpack = m.unpack;
        }
    if (out.vertexUnpack < 0)  // nothing looked like positions: old rule
        for (size_t i = 0; i < out.unpacks.size(); ++i) {
            if (out.unpacks[i].format.rfind("V4_32", 0) != 0) continue;
            if (out.unpacks[i].floats.size() > best) {
                best = out.unpacks[i].floats.size();
                out.vertexUnpack = (int)i;
            }
        }
    // v3 tail: the whole of VU1 data memory after the run.
    if (ver >= 3u) {
        const unsigned char* memAt =
            p + (size_t)out.qw * 16 + (size_t)blockCount * 4;
        for (const Block& bl : blocks) memAt += (size_t)bl.qw * 16;
        const size_t have = (size_t)(b.data() + b.size() - memAt);
        const size_t want = 1024 * 16;
        if (have >= want) {
            out.vuMem.resize(want / 4);
            for (size_t k = 0; k < out.vuMem.size(); ++k)
                out.vuMem[k] = rd32(memAt + k * 4);
            out.hasVuMem = true;
            decodeVuMem(out);
        }
    }
    out.loaded = true;
    return true;
}

}  // namespace vucap
