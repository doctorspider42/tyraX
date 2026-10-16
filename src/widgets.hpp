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
 * the three fields), so it is a drop-in replacement. Inside InspectorRows
 * the label is the row's caption instead. */
bool AxisDrag3(const char* label, float v[3], float speed = 1.0f, float vmin = 0.0f,
               float vmax = 0.0f, const char* fmt = "%.3f");

// --- inspector -----------------------------------------------------------

/** Checkbox's drop-in replacement drawn as a toggle switch: same label/ID,
 * same return value ("toggled this frame"), same ImGuiItemFlags_MixedValue
 * support (knob parked in the middle), and it reports itself to --ui-script
 * as checkable with its checked state, so `expect-checked` keeps working.
 * A label "Name (description)" draws Name, then the description dim and
 * smaller on a second line (same item, same ID). Inside InspectorRows it is a
 * full-width settings row: text left, switch at the right end. */
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
    List, Layers, Music, Code, Sliders,
    // workspaces
    Nodes, Film, Screen,
    // panel headers and the outliner
    Plus, Eye, EyeOff, Folder
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

// --- inspector rows (props) --------------------------------------------------
// The Properties panel lays every labelled field out as a ROW: the caption in
// a dim left column, the widget filling the rest. Stock ImGui puts the label on
// the right, and a long label then decides how wide every field may be.
//
// The wrappers below keep each widget's exact label as its ID (and so its
// --ui-script name and ImGui's saved state): the caption is drawn by the row,
// the widget still receives the full label and its own right-hand caption is
// clipped away - the same trick as the Properties header's "Name" field.
// Outside an InspectorRows scope - or in a different window (popup, child), on
// a SameLine, or with a "##"-only label - each wrapper is exactly the stock
// widget, so they are safe to call anywhere.

/** RAII: rows are on for the CURRENT window until the scope ends. The label
 * column is ~a third of the row, clamped to [minLabelW, maxLabelW] px. */
struct InspectorRows {
    InspectorRows(float minLabelW, float maxLabelW);
    ~InspectorRows();
    InspectorRows(const InspectorRows&) = delete;
    InspectorRows& operator=(const InspectorRows&) = delete;
};

/** Help for the row just submitted: the caption gets a dotted underline and
 * shows `tip` on hover. Returns false (nothing done) when the last item was not
 * a row - prefHelp then draws its usual "(?)". */
bool RowHelp(const char* tip);

bool InputText(const char* label, char* buf, size_t size, ImGuiInputTextFlags flags = 0,
               ImGuiInputTextCallback cb = nullptr, void* user = nullptr);
bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags flags = 0);
bool Combo(const char* label, int* cur, const char* const items[], int count, int maxHeight = -1);
bool Combo(const char* label, int* cur, const char* itemsZeroSeparated, int maxHeight = -1);
bool DragFloat(const char* label, float* v, float speed = 1.0f, float vmin = 0.0f, float vmax = 0.0f,
               const char* fmt = "%.3f", ImGuiSliderFlags flags = 0);
bool DragFloat2(const char* label, float v[2], float speed = 1.0f, float vmin = 0.0f, float vmax = 0.0f,
                const char* fmt = "%.3f", ImGuiSliderFlags flags = 0);
bool DragInt(const char* label, int* v, float speed = 1.0f, int vmin = 0, int vmax = 0,
             const char* fmt = "%d", ImGuiSliderFlags flags = 0);
bool SliderFloat(const char* label, float* v, float vmin, float vmax, const char* fmt = "%.3f",
                 ImGuiSliderFlags flags = 0);
bool SliderInt(const char* label, int* v, int vmin, int vmax, const char* fmt = "%d",
               ImGuiSliderFlags flags = 0);

/** ColorEdit3's replacement: a swatch (ID = `label`; click = picker popup), the
 * value as #RRGGBB text, and a disclosure arrow for the 0-255 fields. One group,
 * so IsItemDeactivatedAfterEdit() after it reports the end of an edit made in
 * any of the three; ImGuiItemFlags_MixedValue shows "mixed". */
bool ColorHex(const char* label, float col[3]);

/** Quiet tabs: no tab fill, the selected one underlined in the accent over a
 * hairline. A plain ImGui tab bar underneath (scripts click tabs by label). */
bool BeginQuietTabBar(const char* id);
/** One tab. `selected` = what the caller believes is selected (drawn bright).
 * Returns BeginTabItem's result; the item is already ended (content is drawn by
 * the caller after the bar, so it keeps its own ID scope). */
bool QuietTab(const char* label, bool selected, ImGuiTabItemFlags flags = 0);
void EndQuietTabBar();

/** A square "..." button (InvisibleButton named `label`). Returns clicked. */
bool MoreButton(const char* label, float size);
// --- panel headers / outliner ---------------------------------------------
// A docked panel that is ALONE in its dock node shows no ImGui tab strip (and
// no window-menu triangle): it names itself with a header row instead. Nodes
// holding several windows keep their tabs - drawn as quiet pills by the theme
// (ImGuiStyle::TabPillInset, vendor/imgui-tyrax/TYRAX-PATCHES.md).

/** Once per frame, BEFORE DockSpaceOverViewport: every window named in
 * `names` that was alone in its dock node last frame asks that node for no tab
 * bar (ImGuiDockNodeFlags_NoTabBar through its window class, so nothing is
 * written into a saved layout); the others get their tabs back. Opt-in by
 * name because a window that hides its tab must also draw a PanelHeader
 * whenever nothing else on it names it (the Viewport draws one over its image,
 * Properties when no single object heads it). Window names never change. */
void PanelChromeNewFrame(const char* const* names, int count);

/** True when the current window is docked with its tab strip hidden - the
 * window then has no name on screen unless it draws one itself. */
bool PanelIsUntabbed();

/** The header row of a panel: `title` in the heading cut, `count` (may be
 * null) dim after it, and room for `buttons` PanelHeaderButton icons at the
 * right end (call them right after, left to right). While the panel is
 * untabbed, the empty part of the row is its grab handle: dragging it undocks
 * the panel exactly like dragging its tab would, so it can be re-docked. */
void PanelHeader(const char* title, const char* count = nullptr, int buttons = 0);

/** One square icon button in the slots PanelHeader reserved, `label` is the
 * ID and --ui-script name. Returns clicked. */
bool PanelHeaderButton(const char* label, Glyph g, const char* tooltip = nullptr);

/** Inline variant for panels whose first row is a toolbar (Output, Debug,
 * Flow Graph): only while untabbed, `title` in the heading cut followed by
 * SameLine - the same grab handle as PanelHeader's. */
void PanelInlineTitle(const char* title);

/** A single-select filter chip: `label` (the ID, the --ui-script name and the
 * text) with `count` dim after it when >= 0. Reported to --ui-script as
 * checkable with `selected` as its state. Returns clicked. Chips flow:
 * SameLine is done by the caller via ChipFlow. */
bool FilterChip(const char* label, bool selected, int count = -1);

/** Width a FilterChip with this label/count takes. */
float FilterChipWidth(const char* label, int count = -1);

/** A layer chip: a colour square, the name, dimmed (and the square hollow)
 * while `visible` is false. Reported as checkable with `visible`. Returns
 * clicked. */
bool LayerChip(const char* label, ImU32 colour, bool visible);
float LayerChipWidth(const char* label);

/** Before a chip of width `w`: SameLine when it still fits on this row (not
 * for the first chip of a run, `first`). */
void ChipFlow(float w, bool first);

/** An eye toggle `size` px square at the cursor (an InvisibleButton named
 * `label`); `visible` picks the open or struck-through eye. Reported as
 * checkable with `visible`. Drawn only when `show` (the hit box is always
 * there). Returns clicked. */
bool EyeToggle(const char* label, bool visible, float size, bool show);

}  // namespace ui
