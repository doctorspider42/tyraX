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
    int program = -1;         // MSCAL entry address this block is run under
    /** Why this block is NOT counted as a mesh's positions, when it is not.
     * Empty on a position stream. The mesh list is a HEURISTIC over the chain
     * (see load()), and a heuristic that silently drops a stream looks exactly
     * like "the pipeline never sent my model" - so it says why instead. */
    std::string posNote;
    std::vector<float> floats;      // payload read as floats (V4_32 only)
    std::vector<uint32_t> words;    // payload raw
};

/** One mesh of the flush: its position stream, plus what can be said about it
 * without leaving the capture. A flush is a whole bag, so there are as many of
 * these as the bag held - which is why the wireframe needs a selector. */
struct Mesh {
    int unpack = 0;        // index into `unpacks`: the positions
    int verts = 0;
    int tris = 0;
    int program = -1;      // microprogram entry address it runs under
    int degenerate = 0;    // triangles with (near) zero area in model space
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};  // model-space bounds
    float extent() const;  // longest bbox side
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
    /** Every microprogram RUN of the chain (MSCAL / MSCALF / MSCNT), in order:
     * how many unpacks had been seen when it ran, and which program it was.
     * The unpacks between two runs are that run's input. */
    struct Run {
        int unpackEnd = 0;
        int program = -1;
    };
    std::vector<Run> runs;
    int vertexUnpack = -1;         // index into `unpacks`: the vertex stream
    /** Every mesh of the flush, in send order. A flush carries a whole bag, so
     * a busy frame puts a dozen in here; showing only the biggest one made
     * every capture look identical. */
    std::vector<Mesh> meshes;
    /** Vertices of the biggest V4_32 unpack, as triangles (3 per triangle). */
    const std::vector<float>* vertices() const;
    /** Positions of mesh `i`, same layout. */
    const std::vector<float>* verticesOf(int i) const;
    int triangleCount() const;

    // --- v4: which draw this is, and what the picture measures ---------------
    uint32_t version = 0;
    int flushIndex = -1;   // which bag flush of the frame (-1 = pre-v4 capture)
    int flushCount = 0;    // flushes the last complete frame sent (0 = unknown)
    int renderWidth = 0;   // the game's live render resolution (0 = unknown)
    int renderHeight = 0;

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
    int inputTris() const;   // triangles the EE sent, over every mesh
    int outputTris() const;  // triangles staged in VU1 memory
    /** Staged triangles minus the LAST mesh's - VU1 memory holds only what the
     * final run(s) left, so this is that mesh's clip fan-out, not the flush's. */
    int clipDelta() const;

    // --- what looks wrong ---------------------------------------------------
    /** The drawing window in GS plane units (the same units GsVertex::px()
     * reports): the engine centres a `renderWidth x renderHeight` window on
     * 2048, so anything outside these bounds is not on screen. Only known when
     * the capture carries the resolution (v4) - and only for the MAIN pass:
     * portal/env/split passes move XYOFFSET while they run. */
    bool hasWindow = false;
    float winX0 = 0, winX1 = 0, winY0 = 0, winY1 = 0;
    int gsVerts = 0;        // vertices of the biggest staged geometry packet
    int gsOffWindow = 0;    // ...of which outside the drawing window
    int gsZeroAlpha = 0;    // ...with alpha 0 in a packet that does not blend
    // That packet's screen bounds, same units as the window. A vertex outside
    // the window is ORDINARY (a triangle crossing the screen edge has two of
    // them, and the GS scissors it) - what is not ordinary is the whole packet
    // missing the window, which is why the finding tests the box, not vertices.
    float gsX0 = 0, gsX1 = 0, gsY0 = 0, gsY1 = 0;
    bool packetOffscreen = false;
    int hugeTris = 0;  // triangles spanning nearly the whole 4096 plane
    int behindVerts = 0;    // input vertices with clip w <= 0 (host reference)
    int degenerateTris = 0; // input triangles with (near) zero area
};

/** Reads and decodes bin/vucap.bin. */
bool load(const std::string& path, Capture& out);

// --- a whole frame (bin/vuframe.bin) -----------------------------------------
// Every VU1 chain of ONE frame, decoded and run through the same transform the
// microprograms perform - on the host, so it costs the game nothing but the
// copy. What comes out is what the EE SENT: every triangle before VU1 clips or
// culls it, which is exactly the view that answers "why is this frame heavy".

/** One microprogram run of the frame: one mesh package, ready to draw. */
struct FrameDraw {
    int flush = 0;       // which bag flush of the frame it belongs to
    int program = -1;    // microprogram entry address
    int tris = 0;        // triangles sent
    int behind = 0;      // ...with a vertex behind the eye (not drawable here)
    int offscreen = 0;   // ...whose box misses the drawing window entirely
    // Screen points, GS plane units (the window is centred on 2048): six floats
    // a triangle (x0 y0 x1 y1 x2 y2). Triangles behind the eye are left out.
    std::vector<float> xy;
};

/** One bag flush of the frame, summed over its runs. */
struct FrameFlush {
    int qw = 0;
    int runs = 0;
    int tris = 0, behind = 0, offscreen = 0;
    int firstDraw = 0, drawCount = 0;  // its slice of Frame::draws
};

struct Frame {
    bool loaded = false;
    std::string error;
    uint32_t frame = 0;
    int renderWidth = 0, renderHeight = 0;
    bool truncated = false;        // the game ran out of room: the tail is missing
    size_t bytes = 0;              // file size
    size_t poolBytes = 0;          // ...of which referenced blocks, stored once
    std::vector<FrameFlush> flushes;
    std::vector<FrameDraw> draws;
    int tris = 0, behind = 0, offscreen = 0;
    // The drawing window in GS plane units.
    float winX0 = 0, winX1 = 0, winY0 = 0, winY1 = 0;
};

/** Reads and decodes bin/vuframe.bin. `flipY` picks the other screen-Y
 * convention (see decodeVuMem's note on the GS axis). */
bool loadFrame(const std::string& path, Frame& out, bool flipY = false);

/** Rasterizes the frame's wireframe into an RGBA image of the render size
 * (one colour per flush). For --dump-vuframe and tests. */
void rasterFrame(const Frame& f, std::vector<unsigned char>& rgba, int& w, int& h);

/** Scans a block of VU1 data memory (1024 quadwords as words) for the GIF
 * packets a microprogram staged, and decodes their vertices.
 *
 * Public because the HOST VU1 SIMULATOR (src/vusim.cpp) ends a run with exactly
 * this memory image, and a simulated run is only comparable to a captured one if
 * both go through the same decoder. Do not reimplement it on the simulator side. */
void scanGifPackets(const std::vector<uint32_t>& vuMem,
                    std::vector<GifPacket>& out);

}  // namespace vucap
