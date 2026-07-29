// Remote Pad - the host side of the input channel (docs/remote-pad.md).
//
// The other three live channels tell the game what the WORLD should look like
// (Live Link), what its graphs should DO (Live Logic) or ask it what it is
// doing (Live Debugger). This one answers the question none of them can: who
// is holding the controller. The editor (or a script, or an unattended test)
// writes a pad state into bin/livepad.bin and the generated game overlays it
// on the physical pad through Pad::injectVirtual - so the game can be driven
// with no window focus anywhere, which on Windows is the difference between
// "testable" and "somebody has to sit in front of PCSX2".
//
// The file is absolute STATE, not events: it says which buttons are down and
// where the sticks are, right now. A driver therefore has to keep rewriting it
// while it holds anything (the `seq` is what proves it is still alive - see
// kStaleFrames), and the game drops the overlay when the writer goes away
// rather than walking into a wall forever.
//
// No GL, no ImGui, no project.hpp: the same harness-testable shape as
// livedbg/livetime. The layout has a twin in the generated runtime
// (templates::livePadSource) - change them together.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace livepad {

// Two connectors, and that is the whole story: the PS2 has two without a
// multitap and the generated game reads pad 2 only in a multiplayer project.
inline constexpr int kPads = 2;

// bin/livepad.bin, fixed size - the game rejects anything else.
inline constexpr int kFileSize = 36;

// How many frames the game keeps applying an overlay whose `seq` never moves.
// A driver that crashed (or was Ctrl+C'd) mid-hold must not leave the player
// walking; a driver that is genuinely idle writes a neutral state, so letting
// it expire costs nothing. The game's twin of this constant is what enforces
// it - this copy exists so the editor can say so in the UI.
inline constexpr int kStaleFrames = 120;

/** Who is holding the pad, right now. Buttons are a mask over
 * kPadButtonNames (bit i = that button); axes are -127..127 offsets
 * applied to the sticks, 0 = leave centred, in the order lh, lv, rh, rv. */
struct State {
    uint32_t buttons[kPads] = {0, 0};
    int8_t axes[kPads][4] = {};

    bool neutral() const;
    void clear();
    bool operator==(const State& o) const;
    bool operator!=(const State& o) const { return !(*this == o); }
};

/** Bit for a button index (kPadButtonNames order); 0 if out of range. */
uint32_t buttonBit(int index);

/** Resolves a button name to a kPadButtonNames index, accepting the
 * spellings a person types: case-insensitive, "x"/"o" for Cross/Circle and
 * bare "up"/"down"/"left"/"right" for the D-pad. -1 = not a button. */
int buttonByName(const std::string& name);

// ------------------------------------------------------------------- file ---

std::vector<unsigned char> encode(const State& s, uint32_t seq, bool attached);

/** Reads a state back. False on a short/torn/foreign file - the same
 * exact-size + footer-echo check the game does. */
bool decode(const std::vector<unsigned char>& bytes, State& out, uint32_t& seq,
            bool& attached);

/** Writes atomically (sibling tmp + rename), like every other channel: the
 * game may fopen this file between our two writes. Returns "" or an error. */
std::string write(const std::string& path, const State& s, uint32_t seq,
                  bool attached);

// ----------------------------------------------------------------- script ---

/** One resolved moment of a pad script: the full state to hold, and for how
 * long. Parsing produces a flat timeline of these, so a script is entirely
 * checkable without touching a file system or a running game. */
struct Step {
    State state;
    double seconds = 0.0;  // 0 = apply and move on
    std::string source;    // the command that produced it, for --pad's log
};

/** Parses a pad script into a timeline (docs/remote-pad.md):
 *
 *   pad 2              subsequent commands target connector 2
 *   press cross 0.2    tap (default 0.1 s), then release it again
 *   hold up            hold until released; non-blocking
 *   release up | all
 *   stick l 0 -127     left stick to (x, y), -127..127
 *   wait 1.5
 *   neutral            release everything on both pads, centre the sticks
 *
 * Commands are separated by newlines or ';', '#' starts a comment. Returns
 * false and fills `err` on the first line it cannot make sense of - a script
 * that is half-understood would drive the game somewhere nobody asked for. */
bool parseScript(const std::string& text, std::vector<Step>& out,
                 std::string& err);

}  // namespace livepad
