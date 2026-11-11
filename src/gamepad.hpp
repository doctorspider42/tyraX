// The host's game controller as a Remote Pad source (docs/remote-pad.md,
// "A real controller").
//
// The Remote Pad already lets the editor hold the game's controller - by
// clicking a drawn pad, by the keyboard, by a script. This adds the obvious
// fourth way: a controller plugged into the PC. Its state is read through
// GLFW's gamepad mapping (so an Xbox pad reads the same on Windows, Linux and
// macOS), turned into the PS2 pad's buttons and sticks, and merged into the
// state the editor sends - to PCSX2 through bin/livepad.bin and to a console
// through devlink, exactly like every other source.
//
// Split in two on purpose: poll() talks to GLFW, toPad() is pure arithmetic a
// harness can feed. No ImGui, no project.hpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gamepad {

/** When the controller's input is sent to the game. */
enum SendMode {
    kWhilePreviewHasPad = 0,  // the live preview took the pad (click / Esc)
    kWhileEditorFocused = 1,  // any editor window is in front
    kAlways = 2,              // also while another application is in front
};

/** The controller's own controls, in the order the mapping stores them. */
enum Source {
    kSrcA, kSrcB, kSrcX, kSrcY,
    kSrcLB, kSrcRB, kSrcLT, kSrcRT,
    kSrcView, kSrcMenu, kSrcLClick, kSrcRClick,
    kSrcUp, kSrcDown, kSrcLeft, kSrcRight,
    kSourceCount
};

/** Preferences > Running games > Controller. Per machine (editor.ini), never
 * per project: which pad is plugged into this PC is not a property of a game. */
struct Prefs {
    bool enabled = true;
    std::string device;   // GLFW gamepad name; "" = the first one connected
    int pad = 0;          // which PS2 connector it drives (0 / 1)
    int sendMode = kWhilePreviewHasPad;
    int deadZone = 12;    // stick dead zone, % of full travel (0..40)
    int trigger = 30;     // analog trigger -> held threshold, % (5..95)
    bool invertRY = false;
    // What each control sends: a PS2 button (livepad's kPadButtonNames index,
    // 0..15) or -1 for nothing. Defaults to the layout by position.
    int map[kSourceCount] = {0, 3, 1, 2, 8, 11, 9, 12, 15, 14, 10, 13, 4, 5, 6, 7};

    bool operator==(const Prefs& o) const;
};

/** Sets A and B the way the two presets mean them: by position (A = Cross,
 * B = Circle) or by label (A = Circle, B = Cross). */
void setFacePreset(Prefs& prefs, bool byLabel);
/** 0 = by position, 1 = by label, -1 = neither (a custom A / B). */
int facePreset(const Prefs& prefs);
/** The mapping back to its defaults. */
void resetMap(Prefs& prefs);

/** What the controller reports, before any mapping. Sticks are -1..1 with up
 * and left negative (GLFW's convention, which is also the pad's); triggers
 * are 0..1. `buttons` is indexed by GLFW_GAMEPAD_BUTTON_*. */
struct Sample {
    bool connected = false;
    std::string name;
    float lx = 0, ly = 0, rx = 0, ry = 0;
    float lt = 0, rt = 0;
    bool buttons[15] = {};
    bool guide = false;
};

/** Names of every mapped gamepad connected right now. */
std::vector<std::string> devices();

/** Reads the preferred controller (or the first connected one). */
Sample poll(const Prefs& prefs);

/** Whether control `source` is held in this sample (a trigger: past the
 * threshold). Pure. */
bool held(const Sample& s, int source, const Prefs& prefs);

/** The sample as PS2 pad state: a button mask in livepad's kPadButtonNames
 * order and the four stick axes (lh, lv, rh, rv) as -127..127. Pure. */
void toPad(const Sample& s, const Prefs& prefs, uint32_t& buttons, int8_t axes[4]);

/** The controller's own name for a control: "A", "RT", "Menu", "L click"... */
const char* sourceName(int source);
/** A PS2 button as the UI names it: "Cross", "Up", "L2"...; "Nothing" for -1. */
const char* targetName(int padButton);

}  // namespace gamepad
