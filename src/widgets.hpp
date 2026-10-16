// Shared widgets and faces for the editor's redesigned chrome
// (docs/editor-theme.md).
//
// theme.hpp says what a colour MEANS; this says what a recurring control LOOKS
// like, so the same inspector field is the same field in every panel. Pure
// ImGui like theme.cpp - no App, no Project - so a widget can be read and
// changed in one place.
//
// Every widget keeps ImGui's own contract: the label is the ID (and the name
// --ui-script finds it by), the return value is "changed this frame", and
// IsItemDeactivatedAfterEdit() right after it reports the end of an edit.
#pragma once

#include <string>

#include <imgui.h>

namespace ui {

// --- faces -------------------------------------------------------------------
// Set once by App::loadUiFont. Either may stay null (no matching system face):
// the getters then return null, and PushFont(nullptr, 0) keeps the current
// face - so callers never branch on whether a face was found.
void setBoldFont(ImFont* f);
void setMonoFont(ImFont* f);
ImFont* boldFont();  // the interface family's heavier cut: titles, headings
ImFont* monoFont();  // fixed-width: numbers that change, logs, code

/** RAII PushFont at the current size; a null face keeps the current one. */
struct FontScope {
    explicit FontScope(ImFont* f) { ImGui::PushFont(f, 0.0f); }
    ~FontScope() { ImGui::PopFont(); }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;
};

// --- marks -------------------------------------------------------------------

/** The editor's mark: the four face-button glyphs (triangle, circle, cross,
 * square) in a rounded tile `size` px square at `p`, in the theme's ok /
 * danger / accent / special colours. Draw-only, no item. */
void LogoMark(ImDrawList* dl, ImVec2 p, float size);

// --- shortcuts ---------------------------------------------------------------

/** A Ctrl chord as this platform shows it: "Ctrl" + sep + key, or on macOS
 * the Command sign + `macKey` (key when null) with no separator - "Cmd" when
 * the font has no U+2318. `ShortcutChord("K", nullptr, " ")` = "Ctrl K" /
 * "<cmd>K". `keycap`: the text goes on a Keycap, which may draw it in the
 * mono face, so that face having the sign is enough. */
std::string ShortcutChord(const char* key, const char* macKey, const char* sep, bool keycap = false);

/** A keycap: `text` in the mono face on a bordered tile `h` tall at `p`.
 * Returns its width; draws nothing when dl is null (measure first). */
float Keycap(ImDrawList* dl, ImVec2 p, float h, const char* text);

// --- fields ------------------------------------------------------------------

/** DragFloat3 with the axis named INSIDE each field - a small X/Y/Z in the
 * gizmo's colour at the field's left edge - and the value in the fixed-width
 * face, so digits do not jitter while dragging. Same signature, ID, return
 * value and group semantics as ImGui::DragFloat3 (the label is drawn after
 * the three fields), so it is a drop-in replacement. */
bool AxisDrag3(const char* label, float v[3], float speed = 1.0f, float vmin = 0.0f,
               float vmax = 0.0f, const char* fmt = "%.3f");

// --- inspector -----------------------------------------------------------

/** Checkbox's drop-in replacement drawn as a toggle switch: same label/ID,
 * same return value ("toggled this frame"), same ImGuiItemFlags_MixedValue
 * support (knob parked in the middle), and it reports itself to --ui-script
 * as checkable with its checked state, so `expect-checked` keeps working. */
bool Switch(const char* label, bool* v);

/** CollapsingHeader's drop-in replacement for inspector sections: no tinted
 * band, the title in the heading cut over a hairline, the caret quiet. Same
 * label/ID and open state (stored by ImGui per label) as CollapsingHeader. */
bool Section(const char* label, ImGuiTreeNodeFlags flags = 0);

// --- viewport overlays ----------------------------------------------------
// Controls that float over the 3D view: grouped on a translucent card
// (OverlayPanel) so they read as one tool palette, not loose buttons on top of
// the scene. Each button is an InvisibleButton named by `label` - the label
// is the ID and the --ui-script name, exactly as the SmallButton it replaces.

enum class Glyph {
    Move, Rotate, Scale, Sculpt, Paint, Measure, Snap, Gear, Box,
    // object types (the outliner) and the left rail's pages
    Person, Bulb, Camera, Speaker, Area, Comment, Sparkle, Flag,
    List, Layers, Music, Code, Sliders
};

/** Draws `g` centred on `c` in a box `size` px square, one stroke weight. */
void DrawGlyph(ImDrawList* dl, Glyph g, ImVec2 c, float size, ImU32 col);

/** Height of an overlay text button / segmented row (one frame). */
float OverlayRowHeight();
/** Inner padding of an OverlayPanel around its buttons. */
float OverlayPad();
/** Width an OverlayTextButton showing `text` takes. */
float OverlayTextWidth(const char* text);

/** The translucent card behind a group of overlay controls. */
void OverlayPanel(ImDrawList* dl, ImVec2 min, ImVec2 max);

/** A square icon button at the cursor: accent fill when `active`, a faded
 * hover otherwise, dimmed when disabled; `key` (may be null) is drawn small in
 * the corner. Returns clicked (never while disabled). */
bool OverlayIconButton(const char* label, Glyph g, float size, bool active, bool enabled,
                       const char* key);

/** A text button at the cursor showing `text` (ID = `label`), accent-tinted
 * when `active`. Returns clicked. */
bool OverlayTextButton(const char* label, const char* text, bool active, bool enabled = true);

// --- viewport overlays (blur) ---
// Frosted cards: the Viewport hands over a blurred copy of its own image
// (Viewport::blurBackdrop) and where that image sits on screen; cards drawn
// with OverlayGlassPanel then show the blurred scene behind them under a dark
// tint. Without a backdrop they are the plain OverlayPanel.

/** Sets the blurred backdrop for this frame's glass cards. `tex` is a GL
 * texture in GL's bottom-up orientation covering [imgMin, imgMax]; 0 clears. */
void SetOverlayBackdrop(ImTextureID tex, ImVec2 imgMin, ImVec2 imgMax);
/** OverlayPanel over the blurred backdrop (falls back to OverlayPanel). */
void OverlayGlassPanel(ImDrawList* dl, ImVec2 min, ImVec2 max);
/** Width an OverlayMenuButton showing `text` takes (text + caret). */
float OverlayMenuButtonWidth(const char* text);
/** An OverlayTextButton with a small down caret after `text`: it opens a
 * popup. Same ID/return contract as OverlayTextButton. */
bool OverlayMenuButton(const char* label, const char* text, bool active, bool enabled = true);
/** A thin vertical divider between groups inside an overlay card. */
void OverlayDivider(ImDrawList* dl, float x, float y0, float y1);
// --- end viewport overlays (blur) ---

}  // namespace ui
