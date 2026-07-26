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
};

/** Reads and decodes bin/vucap.bin. */
bool load(const std::string& path, Capture& out);

}  // namespace vucap
