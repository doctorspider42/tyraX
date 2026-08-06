// UI scripting - driving the EDITOR without a human (docs/ui-scripting.md).
//
// The Remote Pad's lesson applied to the editor itself: do not fight the window
// manager, go through something the application already owns. Here that is Dear
// ImGui's own item bookkeeping and its own event queue.
//
// Two halves:
//
//  - The **item registry**. ImGui already announces every widget it submits
//    (id, bounding box, label, status flags) through four `extern` hook
//    functions that exist purely so an external test engine can implement them.
//    We implement them ourselves in uiscript.cpp - so the editor knows what is
//    on screen and where, by NAME, with no imgui_test_engine dependency (and
//    without its commercial licence question). That is what makes a script say
//    `click "Remote Pad/Cross"` instead of `click 837,412`: no DPI arithmetic,
//    no ui-scale arithmetic, no layout guessing, and a script that keeps working
//    when the panel moves.
//  - The **script**. Steps are executed one at a time by `App::uiScriptTick`,
//    which injects mouse/keyboard events straight into `ImGui::GetIO()` before
//    ImGui::NewFrame(). Nothing reaches the OS, so **no window needs the
//    keyboard focus** and the editor can be behind everything else. `click`
//    WAITS for its target to exist rather than sleeping a guessed interval,
//    which is what makes menus (a popup only exists a frame later) scriptable
//    without timing luck.
//
// No GL, no project.hpp; imgui internals live in the .cpp only, so this header
// stays cheap and the parser is testable on its own.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uiscript {

// ---------------------------------------------------------------- registry ---

/** One widget ImGui submitted this frame. Coordinates are ImGui screen space -
 * the same space mouse events are injected in, which is why a script never has
 * to know the window's position or the UI scale. */
struct Item {
    uint32_t id = 0;
    std::string label;   // "Cross", "Live Link", "Pad 1" - as authored
    std::string window;  // the ImGui window it was submitted in
    // Id of that window, which is the SEED ImGui hashed the widget's label with.
    // It is what lets a label be found even when ImGui never reported one - see
    // the id-hash fallback in find(). Combos are the whole reason: BeginCombo
    // does not call the test-engine's ItemInfo hook at all, so every combo in
    // the editor was nameless (and therefore unscriptable) until this.
    uint32_t windowId = 0;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    // The WINDOW itself, which ImGui registers as an item covering the whole
    // window. Findable on purpose (an existence check wants it) but never
    // clickable: its centre is whatever widget happens to sit in the middle.
    bool isWindow = false;
    bool checkable = false, checked = false;
    bool openable = false, opened = false;
    bool inputable = false;
};

/** Item collection costs a branch per widget, so it is off until a script (or
 * the UI inspector) asks for it. Sets ImGui's own `TestEngineHookItems`. */
void setEnabled(bool on);
bool enabled();

/** Drops last frame's items. Call once per frame BEFORE ImGui::NewFrame(), and
 * after whoever needed to read them has read them. */
void beginFrame();

const std::vector<Item>& items();

/** Finds a widget by `"Window/Label"` or by bare `"Label"` (first match, in
 * submission order). Both halves match case-insensitively, the label may be
 * given without a trailing "..." and the window may be a prefix ("Remote"
 * matches "Remote Pad"), so a script does not depend on a menu's punctuation or
 * a docked window's decorations. Returns null when it is not on screen - which
 * is a normal answer, not an error: a script waits for it.
 *
 * `clickable` excludes the whole-window items - pass true for anything that
 * ends in a mouse event. Without it a bare window name resolves to the window's
 * own rect and a "click" lands on whatever widget sits in its middle, which is
 * how the first version of this pressed R3 when asked to open a panel. */
const Item* find(const std::string& target, bool clickable = false);

/** Every item on screen, one per line - the discovery tool. A session that does
 * not know what a panel's buttons are called runs a script of just
 * `dump; quit`. */
std::string dumpText();

// ------------------------------------------------------------------ script ---

struct Step {
    enum Kind {
        Click,        // arg = target
        RightClick,   // arg = target - the context-menu button
        DoubleClick,  // arg = target
        HoldClick,    // arg = target, seconds = how long to keep the button down
        Hover,        // arg = target
        Drag,         // arg = target, dx/dy = pixels to drag by
        Wheel,        // arg = target, dy = wheel notches (the only way to reach
                      // a canvas zoom, which is not a widget)
        Key,          // arg = chord ("ctrl+n", "f9", "escape")
        Text,         // arg = characters to type into the focused item
        Wait,         // seconds
        Frames,       // count in `n`
        Shot,         // arg = png path
        Expect,       // arg = target that must exist
        ExpectNot,    // arg = target that must NOT exist
        ExpectChecked,    // arg = a checkbox/menu item that must be ticked
        ExpectUnchecked,  // ...and the other way round
        Dump,
        Log,   // arg = message
        Quit,  // close the editor
    };
    Kind kind = Click;
    std::string arg;
    float dx = 0, dy = 0;
    double seconds = 0;
    int n = 0;
    std::string source;  // the line it came from, for the log
};

/** Parses a UI script (docs/ui-scripting.md). Commands are separated by
 * newlines or ';', '#' starts a comment, and a target containing spaces may be
 * quoted. Returns false + `err` on the first unusable line. */
bool parseScript(const std::string& text, std::vector<Step>& out,
                 std::string& err);

/** Resolves "ctrl+shift+n" / "f9" / "escape" into an ImGuiKey (returned as an
 * int so this header needs no ImGui) plus its modifiers. False = no such key. */
bool parseChord(const std::string& chord, int& key, bool& ctrl, bool& shift,
                bool& alt);

}  // namespace uiscript
