// VU1 packet capture - the host side (docs/devkit.md).
//
// Debugging a VU1 microprogram is blind: you cannot print from VU1 and its
// output goes straight to the GS. Its INPUT, though, is a DMA chain the EE built
// - and the engine's tap can hand one to the editor (bin/vucap.bin). This module
// decodes that chain: the DMA tags, the VIF commands, the UNPACKed data blocks
// (matrices, scales, vertex arrays), and which microprogram the packet started.
//
// No GL, no ImGui, no project.hpp - the aobake/livedbg shape.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vucap {

/** One UNPACKed data block: what the EE wrote into VU1 data memory. */
struct Unpack {
    uint32_t vuAddr = 0;      // destination in VU1 data memory (quadwords)
    int count = 0;            // items (VIFcode NUM)
    std::string format;       // "V4_32", "V4_8", ...
    bool useTops = false;     // addressed relative to the double-buffer base
    std::vector<float> floats;      // payload read as floats (V4_32 only)
    std::vector<uint32_t> words;    // payload raw
};

/** One decoded line of the chain, for the inspector list. */
struct Step {
    uint32_t offsetQw = 0;
    std::string text;
};

/** One GS vertex the microprogram STAGED for XGKICK: screen space, as the GS
 * will read it (X/Y are 12.4 fixed point, Z is 24-bit). */
struct GsVertex {
    int x = 0, y = 0;      // raw 12.4
    uint32_t z = 0;        // 24-bit
    uint8_t r = 255, g = 255, b = 255, a = 128;
    float s = 0, t = 0, q = 1;
    float px() const { return x / 16.0f; }
    float py() const { return y / 16.0f; }
};

/** A GIF packet found in VU1 memory: the program's OUTPUT. */
struct GifPacket {
    int vuAddr = 0;        // where in VU1 data memory it sits (quadwords)
    int nloop = 0;         // GS vertices
    int nreg = 0;
    bool eop = false;
    bool pre = false;
    uint32_t prim = 0;     // PRIM bits (type, texture, fog, AA, ...)
    std::string regs;      // decoded REGS list, e.g. "ST, RGBAQ, XYZF2"
    bool hasGeometry = false;  // carries XYZF2/XYZ2, i.e. real vertices
    std::vector<GsVertex> verts;
    std::string primName() const;
};

/** What the host computes for the same input, to compare against. */
struct RefVertex {
    float clip[4] = {0, 0, 0, 0};  // after the MVP
    int x = 0, y = 0;              // 12.4, same scaling the program does
    int yFlipped = 0;              // the same Y with the GS axis flipped
    uint32_t z = 0;
    bool behind = false;           // w <= 0: the program's divide blows up here
};

struct Capture {
    bool loaded = false;
    std::string error;
    uint32_t frame = 0;
    int qw = 0;                    // chain length in quadwords
    std::vector<Step> steps;       // decoded DMA tags + VIF commands
    std::vector<Unpack> unpacks;
    std::vector<int> mscal;        // microprogram start addresses seen
    int vertexUnpack = -1;         // index into `unpacks`: the vertex stream
    /** Vertices of the biggest V4_32 unpack, as triangles (3 per triangle). */
    const std::vector<float>* vertices() const;
    int triangleCount() const;

    // --- v3: what VU1 left behind ------------------------------------------
    bool hasVuMem = false;
    std::vector<uint32_t> vuMem;      // 1024 quadwords, as words
    bool hasMvp = false;
    float mvp[16] = {};               // VU1 quadwords 0..3, column per quadword
    float scale[3] = {2048.0f, 2048.0f, 8388607.5f};  // from the packet
    std::vector<GifPacket> gifs;      // the program's staged output
    std::vector<RefVertex> reference;  // host transform of the input vertices
    // Diff of the largest GIF packet against `reference`, in 12.4 units.
    bool yFlipped = false;  // which screen-Y convention matched the hardware
    int diffCompared = 0;
    float diffMaxX = 0, diffMaxY = 0, diffMeanX = 0, diffMeanY = 0;
    int outputVerts() const;
    int clipDelta() const;  // output triangles - input triangles
};

/** Reads and decodes bin/vucap.bin. */
bool load(const std::string& path, Capture& out);

}  // namespace vucap
