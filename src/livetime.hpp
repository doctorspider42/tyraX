// The time machine - the host side of the state-rewind channel.
//
// Live Link streams object state INTO the running game, the Live Debugger
// streams back what ran, Live Logic streams the program itself. This channel
// streams the WORLD: the generated game captures everything it mutates into
// bin/livetime.bin every few frames, the editor keeps a history of those
// captures, and pushing one back through bin/livetime.rst puts the game where
// it was. With Live Logic that closes the loop nobody has on this hardware -
// rewind a few seconds, patch a graph, and watch the fix play out on the
// situation that just broke.
//
// This module owns both file formats and the history. It deliberately does NOT
// understand the payload: what is inside a snapshot is a codegen detail (the
// generated capture walk decides), and the editor's job is to store the bytes
// and hand the right ones back. The `layout` hash is what keeps that safe - a
// snapshot only goes back into a game built from the same state shape.
//
// No GL, no ImGui, no project.hpp: the same harness-testable shape as
// livedbg/aobake. See docs/time-machine.md.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace livetime {

/** One capture of the running game's mutable state. */
struct Snapshot {
    uint32_t seq = 0;    // capture counter; unchanged = nothing new to read
    uint32_t frame = 0;  // the game's frame counter when it was taken
    int scene = 0;       // which scene it belongs to (a restore may not cross)
    uint64_t layout = 0;  // identity of the as-built state shape
    int objectCount = 0;  // runtime objects it covers (display only)
    // The generated walk's bytes, opaque here. Sized by the game's own
    // capture; the editor never looks inside.
    std::vector<unsigned char> state;

    size_t bytes() const { return state.size() + sizeof(Snapshot); }
};

/** Decodes a capture. Torn/partial writes fail the size + footer checks and
 * return false, which the caller retries on its next tick (the game rewrites
 * the same file every few frames, so a miss costs nothing). */
bool parseSnapshot(const std::vector<unsigned char>& bytes, Snapshot& out);
bool readSnapshot(const std::string& path, Snapshot& out);

/** Encodes a snapshot back into the wire format - this is what the editor
 * writes to bin/livetime.rst for the game to apply. Byte-identical to what the
 * game wrote, which is the point: a restore hands back exactly what was
 * captured, never a re-derivation of it. */
std::vector<unsigned char> encodeSnapshot(const Snapshot& s);
/** Writes it atomically (sibling tmp + rename). Returns "" or an error. */
std::string writeRestore(const std::string& path, const Snapshot& s);

/** Editor-side history: the captures, oldest first, bounded by a BYTE budget
 * rather than a count - what a capture costs depends on the scene, so a count
 * would mean a tiny scene wastes the budget and a huge one blows it.
 *
 * It lives in RAM on purpose. Writing the history to disk would be the obvious
 * way to get hours of it, but then a debugging session silently grows a file
 * nobody asked for; here the only things on disk are the two fixed-size
 * channel files, and closing the editor drops the history with the process. */
class History {
 public:
    /** Bytes of captures to keep. Oldest are dropped as new ones arrive. */
    void setBudget(size_t bytes);
    size_t budget() const { return budget_; }

    /** Folds a fresh capture in. Ignores repeats (same seq). A frame that went
     * BACKWARDS means the game restarted, so the history is cleared first - a
     * new run is not a continuation of the old one. Returns true when the
     * snapshot was actually added. */
    bool ingest(const Snapshot& s);
    void clear();

    size_t count() const { return snaps_.size(); }
    size_t bytes() const { return bytes_; }
    bool empty() const { return snaps_.empty(); }
    const Snapshot& at(size_t i) const { return snaps_[i]; }
    const Snapshot& newest() const { return snaps_.back(); }
    const Snapshot& oldest() const { return snaps_.front(); }
    /** Index of the newest capture at or before `frame`, or -1. */
    int indexAtOrBefore(uint32_t frame) const;
    /** Frames spanned by the history (0 when it holds fewer than two). */
    uint32_t frameSpan() const;
    uint32_t lastSeq() const { return lastSeq_; }

 private:
    std::deque<Snapshot> snaps_;  // oldest first
    size_t bytes_ = 0;
    size_t budget_ = kDefaultBudget;
    uint32_t lastSeq_ = 0;
    uint32_t lastFrame_ = 0;

 public:
    // 128 MiB holds roughly seven minutes of a mid-sized scene at the default
    // capture rate. It is a ceiling, not an allocation: an empty history costs
    // nothing and a small scene simply keeps more history inside it.
    static constexpr size_t kDefaultBudget = 128u << 20;
};

// Shared limits - the game side (templates.cpp) mirrors them; keep in sync.
// A capture bigger than this is a corrupt file rather than a big scene: 1000
// objects cost ~128 KB, so a megabyte is a wide margin around anything the
// PS2 can hold.
constexpr uint32_t kMaxStateBytes = 1u << 20;
// Capture cadence in frames. The debugger flushes on the same beat under
// PCSX2, and drops to a slower one under ps2link for the same reason (the
// file server is a network round-trip, not a memcpy).
constexpr int kCaptureFrames = 6;
constexpr int kCaptureFramesPs2Link = 25;

}  // namespace livetime
