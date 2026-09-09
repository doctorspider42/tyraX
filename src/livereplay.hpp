// Input recorder / replay - the host side of the fifth devkit channel
// (docs/input-replay.md).
//
// The other four live channels answer "what does the world look like" (Live
// Link), "what do the graphs do" (Live Logic), "what just ran" (Live Debugger)
// and "put the world back" (the time machine). This one answers the question
// none of them can: WHAT DID THE PLAYER DO. The game records every frame's
// input into a file next to the ELF; feeding that file back makes the game
// perform the same run again, so a bug somebody hit once can be reproduced on
// demand with the Live Debugger and the time machine open beside it.
//
// Only the INPUT is recorded, plus a light state fingerprint to notice when a
// replay stops matching. That is what keeps a ten-minute session about a
// megabyte instead of a gigabyte of world state - and what makes a recording
// worth committing next to the bug it reproduces.
//
// This module owns the file format and the parser. Like livetime it is
// deliberately incurious about what a frame MEANS: the pad bits and the key
// codes are the game's business, the editor stores and validates them.
//
// No GL, no ImGui, no project.hpp: the same harness-testable shape as
// livedbg/livetime/livepad. The layout has a twin in the generated runtime
// (templates::inputReplaySource) - change them together.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace livereplay {

// ------------------------------------------------------------------ format --

constexpr uint32_t kMagic = 0x50525854u;  // "TXRP" little-endian
constexpr uint32_t kVersion = 1u;
constexpr int kHeaderBytes = 64;
// Status file the game writes for the editor's Replay tab, fixed size.
constexpr int kStatusBytes = 32;

// Frames buffered in RAM before a chunk is appended to the file. Under PCSX2
// host: is a local syscall; over ps2link it is a network round-trip, so the
// cadence drops by 4x there for the same reason every other channel's does.
constexpr int kChunkFramesPcsx2 = 64;
constexpr int kChunkFramesPs2Link = 256;

// Keys reported per frame, each direction. A person cannot hold seventeen
// keys, and the cap is what bounds a frame record - overflow is truncated and
// warned about once rather than growing the format.
constexpr int kMaxKeys = 16;

// Payload bytes one chunk may carry. 256 frames of the widest possible record
// is ~21 KB; the cap is the refusal point for a corrupt length field, and the
// game sizes its one static buffer from it.
constexpr int kMaxChunkPayload = 32768;

// Header flags.
constexpr uint32_t kFlagKeyboard = 1u << 0;   // the build had USB kbd/mouse
constexpr uint32_t kFlagMultiplayer = 1u << 1;  // pad 2 records are present
constexpr uint32_t kFlagFinalized = 1u << 2;    // frameCount is trustworthy

// Per-frame record flags.
constexpr uint8_t kFrameFingerprint = 1u << 0;
constexpr uint8_t kFramePad2 = 1u << 1;
constexpr uint8_t kFrameKbd = 1u << 2;

/** What the recording says about the game it came out of. A replay refuses a
 * frame-rate mismatch outright (the whole point is that dt is reproduced) and
 * only WARNS about a layout mismatch - a scene edited since the recording is
 * a divergence to report, not a file to reject. */
struct Header {
    uint32_t version = kVersion;
    uint32_t frameCount = 0;   // 0 while recording; filled in by finalize
    uint32_t frameRate = 50;   // 50 (PAL) or 60 (NTSC)
    uint32_t flags = 0;
    uint32_t chunkFrames = kChunkFramesPcsx2;
    uint64_t layout = 0;       // project::inputLayoutHash at record time
    uint32_t editorFormatVersion = 0;
    int32_t startScene = 0;
    std::string projectName;   // up to 23 chars, informational

    bool finalized() const { return (flags & kFlagFinalized) != 0; }
    bool hasKeyboard() const { return (flags & kFlagKeyboard) != 0; }
    bool hasPad2() const { return (flags & kFlagMultiplayer) != 0; }
};

/** One connector's state for one frame. `pressed`/`clicked` are masks over
 * kPadButtonNames (input.hpp) - the same bit order livepad uses - and the
 * axes are the RAW PadJoy bytes (127 = centred), not offsets. */
struct PadFrame {
    uint16_t pressed = 0;
    uint16_t clicked = 0;
    uint8_t lh = 127, lv = 127, rh = 127, rv = 127;
};

/** USB keyboard + mouse for one frame. Key codes are USB HID usages, the same
 * numbers KbdMouse reports and input.hpp offers. */
struct KbdFrame {
    int16_t dx = 0, dy = 0;
    int8_t wheel = 0;
    uint8_t buttons = 0;
    uint8_t clicked = 0;
    std::vector<uint8_t> held;
    std::vector<uint8_t> clickedKeys;
};

struct Frame {
    float dt = 0.02f;
    PadFrame pad[2];
    bool hasPad2 = false;
    bool hasKbd = false;
    KbdFrame kbd;
    // Where the player was when this frame's input was read. Present only on
    // frames where the fingerprint script ran (it does not while a menu owns
    // the frame), which is what the flag says.
    bool hasFingerprint = false;
    float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f, pitch = 0.0f;
};

/** A procedural volume asking for a seed. The one non-deterministic decision
 * the game makes, so it is recorded rather than reproduced. */
struct SeedEvent {
    uint32_t frame = 0;  // frame index it was asked on (parser-derived)
    uint16_t volume = 0;
    uint32_t seed = 0;
};

struct Recording {
    Header header;
    std::vector<Frame> frames;
    std::vector<SeedEvent> seeds;
    // The file ended mid-chunk (a killed emulator, a pulled plug). Everything
    // before the break is good and is returned; the tail is dropped. This is
    // the normal state of a raw replay.out - "Save" is what canonicalizes it.
    bool truncated = false;
};

/** Decodes a whole recording. Tolerates a truncated tail (see above) and
 * reports it through Recording::truncated; returns false only when the file is
 * not a recording at all. */
bool parse(const std::vector<unsigned char>& bytes, Recording& out,
           std::string& err);
bool read(const std::string& path, Recording& out, std::string& err);

/** Re-encodes a recording canonically: one header with frameCount filled in
 * and kFlagFinalized set, whole chunks, a terminal chunk at the end. This is
 * what a saved .tyrarep in <project>/recordings/ contains. */
std::vector<unsigned char> encode(const Recording& rec);

/** Reads a raw recording (typically bin/replay.out), validates it, and writes
 * the canonical form to `dest` atomically (sibling tmp + rename, like
 * livetime::writeRestore). Returns "" or an error. */
std::string finalize(const std::string& rawOut, const std::string& dest);

// ------------------------------------------------------------------ status --

/** What the game is doing right now, read by the Debugger's Replay tab out of
 * bin/replay.st. A fixed 32 bytes so a torn read is impossible in practice. */
struct Status {
    enum Mode { Off = 0, Record = 1, Replay = 2 };
    int mode = Off;
    uint32_t frame = 0;           // frames recorded / replayed so far
    uint32_t divergences = 0;     // replay only
    uint32_t firstDivergent = 0;  // replay only, 0 = none
    bool done = false;            // the run ended (stopped / stream exhausted)
    uint32_t total = 0;           // replay only: frames in the recording
};
bool parseStatus(const std::vector<unsigned char>& bytes, Status& out);
bool readStatus(const std::string& path, Status& out);

/** Bytes of the widest possible frame record - what the game's chunk buffer is
 * sized against, and what an editor-side size estimate multiplies. */
int maxFrameRecordBytes();

}  // namespace livereplay
