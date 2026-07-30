// The editor's look: colour themes, style metrics and the semantic colours
// status chips read (docs/editor-theme.md).
//
// Why this is its own translation unit rather than a block in app.cpp: it
// depends on nothing but ImGui - no Project, no GL, no App - so the whole
// palette is a pure function of a theme id, and a theme can be checked by
// eye in one place instead of being spread over the call sites that used to
// hardcode IM_COL32 literals.
//
// The rule that keeps it that way: a widget asks for MEANING (accent, ok,
// warn, danger), never for a colour. A chip that wants "the running-game
// green" reads semantics().ok, so switching theme moves it; a chip that
// writes IM_COL32(95, 200, 115, 255) stays green in a violet editor.
#pragma once

#include <imgui.h>

#include <string>

namespace theme {

// The built-in themes. Values are NOT serialized (the .ini stores `key`), so
// the order here is free - but `Count` bounds every "for each theme" loop.
enum class Id {
    FaceButtons = 0,  // graphite + the four DualShock face-button colours
    BootScreen,       // navy-black + the PS2 logo blue
    MemoryCard,       // the browser/OSD violet
    ImGuiDark,        // stock ImGui, kept as an escape hatch
    Count
};

struct Info {
    const char* key;    // what editor.ini stores - stable, never translated
    const char* label;  // what the menu shows
    const char* desc;   // one line of help under the picker
};

/** Metadata of one theme. Out-of-range ids resolve to the default. */
const Info& info(Id id);

/** The theme an editor.ini key names; an unknown or empty key is the default
 * (so a config written by a newer editor degrades instead of failing). */
Id fromKey(const std::string& key);

// The theme a fresh installation starts in.
inline constexpr Id kDefault = Id::FaceButtons;

// What a colour MEANS, for the hand-drawn chrome that cannot go through
// ImGuiCol_ (the toolbar's vector icons, the LIVE/DBG/session chips, the
// viewport overlays). Filled by apply(); reading it before the first apply()
// gives the default theme's values.
struct Semantics {
    ImVec4 accent;       // the theme's one bright colour: selection, focus
    ImVec4 accentMuted;  // the same hue, quiet enough to sit under text
    ImVec4 ok;           // running, live, verified
    ImVec4 warn;         // stale, needs a rebuild, clipped
    ImVec4 danger;       // stopped, failed, destructive
    ImVec4 text;         // the theme's foreground, for icons that are just text
    ImVec4 textDim;      // disabled / secondary
    ImVec4 surface;      // panel fill, for chrome that draws its own background
    ImVec4 border;
};
const Semantics& semantics();

/** Writes the theme's colours AND the shared style metrics (rounding,
 * padding, borders) into `style`. Everything else in the style is left at
 * ImGui's default, so callers that keep an unscaled reference copy can take
 * it straight after this call. */
void apply(Id id, ImGuiStyle& style);

/** Tints the node canvases (Flow Graph, Procedural) to match the theme last
 * applied. Separate from apply() because ImNodes has its own style struct and
 * its own context, which app.cpp creates after the ImGui one. Per-node and
 * per-pin colours are NOT touched - those encode node category and pin type,
 * which is data, not decoration. */
void applyImNodes();

/** A 0..1 hover ramp for hand-drawn widgets, so a highlight fades in instead
 * of snapping on. `id` must be stable per widget (ImGui::GetItemID() right
 * after submitting it); `rate` is in units per second, 1/rate = fade time.
 * State lives in the current window's storage, so a widget that stops being
 * submitted simply stops being animated. */
float hoverAnim(ImGuiID id, bool hovered, float rate = 8.0f);

}  // namespace theme
