// Live Debugger - the host side of the flow-graph debugging channel.
//
// The generated game streams what its flow graphs are doing into
// bin/livedbg.bin (over the same host: filesystem Live Link rides on) and
// reads commands - breakpoints, pause/step, "fire this node now" - back from
// bin/livedbg.cmd. This module owns both formats plus the as-built symbol
// table (src/gen/livedbg.sym, written by codegen) that maps the game's opaque
// node keys onto editor objects and nodes.
//
// No GL, no ImGui, no project.hpp: the same harness-testable shape as
// aobake/placement. See docs/live-debugger.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace livedbg {

// ---------------------------------------------------------------- symbols ---

/** One instrumented flow-graph node, in codegen's key order. */
struct NodeSym {
    int key = 0;              // what the game reports (index into the hit table)
    int scene = 0;            // scene index
    std::string objectId;     // owner object's stable editor id (16 hex)
    int nodeId = 0;           // FlowNode::id inside that object's graph
    std::string type;         // FlowNode::type, for display without the project
};

/** One flow variable, in the game's watch-table order. */
struct VarSym {
    int index = 0;
    char kind = 'i';  // 'i' int, 'b' bool, 'p' position
    std::string name;
};

/** src/gen/livedbg.sym - the as-built map from node keys to editor ids. */
struct Symbols {
    bool loaded = false;
    uint64_t hash = 0;  // identity of this table; the game reports its own copy
    std::vector<NodeSym> nodes;
    std::vector<VarSym> vars;

    const NodeSym* find(int key) const;
};

/** Parses a symbol file. Returns false (and leaves `out` unloaded) on any
 * malformed line - a half-written file must never half-load. */
bool loadSymbols(const std::string& path, Symbols& out);

// --------------------------------------------------------------- snapshot ---

/** One node fire, with the frame it happened on. */
struct Event {
    int key = 0;
    uint32_t frame = 0;
};

/** One frame of a watched object's runtime state. */
struct ObjSample {
    uint32_t frame = 0;
    float pos[3] = {0, 0, 0};
    float rot[3] = {0, 0, 0};
    float scale[3] = {1, 1, 1};
    float color[3] = {1, 1, 1};
    bool visible = true;
    bool active = true;
    bool dirty = false;
};

/** A watched object: its runtime index plus the samples of this flush. */
struct ObjWatch {
    int index = -1;
    std::vector<ObjSample> samples;
};

/** One bag flush of the last complete frame, as the VU1 tap saw it go by. A
 * frame sends dozens; a capture holds one - so this is the index. */
struct FlushInfo {
    int qw = 0;        // chain length in quadwords
    int unpacks = 0;
    int verts = 0;     // items UNPACKed to VU1 address 2, i.e. positions
    int program = 0;   // MSCAL entry (0 = the chain only said MSCNT)
};

/** The frame's vital signs (v4). Every number here is one somebody already
 * computed - the engine, the tap, the scene - and nobody carried across. */
struct Stats {
    bool valid = false;
    int fps = 0;
    int flushes = 0;      // bag flushes in the last complete frame
    uint32_t qw = 0;      // quadwords sent to VU1 in that frame
    uint32_t verts = 0;   // position items sent in that frame
    uint32_t vramFreeKB = 0, vramMinFreeKB = 0, vramLargestKB = 0;
    int vramResident = 0, vramPeak = 0;
    uint32_t vramBinds = 0, vramHits = 0, vramUploads = 0, vramEvictions = 0;
    int objects = 0, objActive = 0, objVisible = 0;
    // The largest position stream of the frame: the pipeline cuts a mesh at
    // exactly the VU1 buffer's capacity, so this IS that capacity.
    int maxChunkVerts = 0;
    // Free EE RAM, in KiB, and the frame it was measured on. Measured only when
    // asked for: the engine finds it by allocating every free block until
    // malloc fails, which is not something to do once per frame.
    uint32_t ramFreeKB = 0, ramFrame = 0;
};

/** bin/livedbg.bin - what the running game reports. */
struct Snapshot {
    uint32_t seq = 0;    // flush counter; unchanged = nothing new to read
    uint32_t frame = 0;  // the game's frame counter at flush time
    int scene = 0;
    bool halted = false;
    int breakKey = -1;  // the node whose breakpoint stopped the game (-1 none)
    uint64_t hash = 0;  // the symbol-table hash baked into the ELF
    std::vector<uint32_t> hits;  // cumulative fire count per node key
    std::vector<Event> events;   // recent fires, oldest first
    std::vector<float> vars;     // 3 floats per watch variable
    // Armed timers: (node key, frames left) for every Delay counting down right
    // now - the answer to "I fired the trigger and nothing happened", which is
    // usually a Delay that never got frames to count.
    std::vector<std::pair<int, int>> timers;
    // Watched objects: what the game's own RuntimeObject actually holds,
    // sampled EVERY FRAME into a ring and flushed in one go - so the editor can
    // draw a real 50 Hz curve (and a trail in the viewport), not the 8 Hz the
    // flush cadence would give. Oldest sample first.
    std::vector<ObjWatch> objects;
    Stats stats;                    // v4
    std::vector<FlushInfo> flushes;  // v4: the last complete frame's draws
};

/** Decodes a snapshot. Torn/partial writes fail the size + footer checks and
 * return false, which the caller simply retries on its next tick. */
bool parseSnapshot(const std::vector<unsigned char>& bytes, Snapshot& out);
bool readSnapshot(const std::string& path, Snapshot& out);

// ---------------------------------------------------------------- command ---

/** bin/livedbg.cmd - what the editor asks the game to do. Applied by the game
 * whenever `seq` changes, so every command must carry the FULL desired state
 * (the breakpoint list included). */
struct Command {
    uint32_t seq = 0;
    bool halt = false;         // freeze the game now
    bool stepUntilFire = false;  // run until any instrumented node fires
    // Force-fire while the game is stopped: false = run exactly the one frame
    // the fire needs (the default), true = resume the game afterwards, so
    // anything the branch ARMS (a Delay, a glide) actually gets frames to run.
    bool fireAndRun = false;
    // Ask the game to hand over the next VU1 DMA chain (docs/devkit.md). The
    // game grabs one packet and writes bin/vucap.bin; one-shot, like `fire`.
    bool captureVu = false;
    // WHICH flush of the frame to grab. A frame sends one chain per bag flush,
    // always in the same order, so without this every capture inspects the same
    // draw. -1 = let the game walk them, one per capture (the default); >= 0 =
    // that index every time. Rides in the flags word, so a game built before
    // this existed simply keeps grabbing the first flush.
    int vuFlush = -1;
    // Ask the game to measure free EE RAM once. One-shot, like `fire`: the
    // engine's measurement allocates every free block and frees the chain, so
    // it happens when asked and never on a timer.
    bool measureRam = false;
    int stepFrames = 0;        // run exactly this many frames, then freeze
    std::vector<uint16_t> breakpoints;  // node keys that halt the game
    std::vector<uint16_t> fire;         // node keys to force-fire once
    // Runtime object indices to sample every frame (see Snapshot::objects).
    std::vector<uint16_t> watchObjects;

    /** Everything except `seq` - the editor rewrites the file only when this
     * changes (a resend of the same state would re-run a step). */
    bool sameStateAs(const Command& o) const;
};

std::vector<unsigned char> encodeCommand(const Command& c);
/** Writes atomically (sibling tmp + rename). Returns "" or an error string. */
std::string writeCommand(const std::string& path, const Command& c);

// --------------------------------------------------------------- timeline ---

/** Editor-side history of what the game did: one entry per frame that had at
 * least one node fire, plus the variable values sampled at that flush. Feeds
 * the Debugger's timeline and its scrub-back ("what fired on frame N?").
 */
class Timeline {
 public:
    struct Frame {
        uint32_t frame = 0;
        std::vector<int> keys;      // node keys that fired, in order
        std::vector<float> vars;    // watch values at the flush that carried it
    };

    /** Folds a fresh snapshot in. Ignores repeats (same seq) and snapshots
     * from a restarted game (frame went backwards -> the history is cleared,
     * a new run is not a continuation of the old one). */
    void ingest(const Snapshot& s);
    void clear();

    const std::vector<Frame>& frames() const { return frames_; }
    /** Index of the newest entry at or before `frame`, or -1. */
    int indexAtOrBefore(uint32_t frame) const;
    uint32_t lastSeq() const { return lastSeq_; }

 private:
    std::vector<Frame> frames_;  // oldest first, capped at kMaxFrames
    uint32_t lastSeq_ = 0;
    uint32_t lastFrame_ = 0;
    static constexpr size_t kMaxFrames = 900;
};

// Shared limits - the game side (templates.cpp) mirrors them; keep in sync.
constexpr int kMaxNodes = 1024;       // instrumented nodes (hit-table size)
constexpr int kMaxBreakpoints = 64;   // breakpoints the game tracks at once
constexpr int kMaxForced = 8;         // force-fire keys per command
constexpr int kMaxEvents = 192;       // event ring the game flushes
constexpr int kMaxWatchObjects = 8;   // objects sampled per frame
constexpr int kObjRing = 32;          // per-object sample ring in the game

}  // namespace livedbg
