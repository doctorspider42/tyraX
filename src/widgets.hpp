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
    Move, Rotate, Scale, Sculpt, Paint, Measure, Road, Snap, Gear, Box,
    // object types (the outliner) and the left rail's pages
    Person, Bulb, Camera, Speaker, Area, Comment, Sparkle, Flag,
    List, Layers, Music, Code, Sliders,
    // workspaces
    Nodes, Film, Screen,
    // panel headers and the outliner
    Plus, Eye, EyeOff, Folder,
    // the status bar's side-panel switches: a window with its left/right
    // column filled (shown) or outlined (collapsed)
    PanelLeft, PanelLeftOff, PanelRight, PanelRightOff,
    // the node inspector's header: bypass, help, and the category icons the
    // existing set has no fit for
    Power, Help, Funnel, Copies, Mask
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
/** A Segmented (toolkit.hpp) as a caption-left row: every option visible at
 * once, for a short choice that a combo would hide behind a click. */
bool SegmentedRow(const char* label, const char* const items[], int count, int* cur);
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

// --- verify-world ---
/** The next ui::SliderFloat/SliderInt drawn in an inspector row paints its
 * track as a `from` -> `to` gradient (e.g. Temperature: blue -> orange)
 * instead of the accent fill. Consumed by that one slider. */
void SetNextSliderGradient(ImU32 from, ImU32 to);
/** The next inspector-row widget's field stops `px` short of the row's right
 * edge, so a small button can follow it on the same line (SameLine). Consumed
 * by that one row; outside InspectorRows it does nothing. */
void SetNextRowFieldTrim(float px);
// --- end verify-world ---

// --- verify-world (polish-3) ---
/** RAII, inside an InspectorRows scope: RowHelp also writes the first sentence
 * of its tip as a dim one-line meaning under the row's caption (the whole tip
 * stays the hover). A Switch whose label already has "(what it does)" keeps
 * that line instead. Project Preferences uses it. */
struct RowMeanings {
    RowMeanings();
    ~RowMeanings();
    RowMeanings(const RowMeanings&) = delete;
    RowMeanings& operator=(const RowMeanings&) = delete;

private:
    bool prev_ = false;
};
/** A hand-built label-left row (caption drawn by the caller in `caption`, the
 * widget just submitted) registers as an inspector row, so RowHelp/prefHelp
 * treat it like one: dotted caption, hover tip, meaning line. */
void NoteRow(ImVec2 captionMin, ImVec2 captionMax, float captionTextW);
// --- end verify-world (polish-3) ---

// --- preview navigation ---
// The one mouse standard for every orbit preview (docs/editor-theme.md,
// "Preview navigation"): RIGHT drag orbits, MIDDLE drag pans, the wheel
// zooms, a middle double-click re-centres. The left button stays free for
// picking, painting and handles. Make the preview's InvisibleButton with
// kPreviewButtons and read PreviewNavigation() right after it.
inline constexpr int kPreviewButtons = ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                       ImGuiButtonFlags_MouseButtonMiddle;
inline constexpr const char* kPreviewNavHint = "Right-drag to orbit, middle-drag to pan, wheel to zoom.";
struct PreviewNav {
    ImVec2 orbit{0.0f, 0.0f};  // pixels dragged with the right button
    ImVec2 pan{0.0f, 0.0f};    // pixels dragged with the middle button
    float wheel = 0.0f;        // wheel notches over the preview
    bool recentre = false;     // middle double-click: pan back to zero
    bool orbiting() const { return orbit.x != 0.0f || orbit.y != 0.0f; }
};
PreviewNav PreviewNavigation();

// --- node inspector kit ---
// The building blocks of a node's parameters in the inspector
// (docs/editor-theme.md, "Node inspector"). The composite rows below are
// caption-left inspector rows like the wrappers above: `label` is the caption,
// the ID and the --ui-script name of the row's first control.

/** A small-caps section caption over a hairline, `right` dim at its far end. */
void KitSection(const char* text, const char* right = nullptr);
/** The same caption with a switch at its far end that turns the whole section
 * on or off (the caller skips the section's rows while it is off). The switch
 * is named `text`. Returns toggled. */
bool KitSectionSwitch(const char* text, bool* on);

/** One drag of a DragRow. `fmt` carries the field's own caption and unit
 * ("Spacing %.1f u"); `isInt` rounds the value. */
struct KitDrag {
    float* v = nullptr;
    float speed = 0.1f;
    float vmin = 0.0f, vmax = 0.0f;
    const char* fmt = "%.2f";
    bool isInt = false;
    const char* name = nullptr;  // --ui-script name (default: the row label, label#2...)
};
/** A caption-left row of 2 or 3 drags side by side: pairs that are always
 * read together. The first field is named `label`, the others `label#2`...
 * A "##" label drops the caption: the fields span the whole width and are
 * named by their KitDrag::name. */
bool DragRow(const char* label, const KitDrag* fields, int n);

/** A min..max pair as ONE control: a track over [viewMin, viewMax] with the
 * kept band and two knobs (drag either), then the two values (and a falloff
 * when `falloff` is set, drawn as ramps on the band). `gradient` paints the
 * track black to white (mask values). The track is named `label`. */
struct RangeSpec {
    float vmin = 0.0f, vmax = 1.0f;        // clamp bounds of the values
    float viewMin = 0.0f, viewMax = 1.0f;  // what the track spans
    float speed = 0.01f;
    const char* fmt = "%.2f";
    float* falloff = nullptr;
    float falloffMax = 0.0f;
    const char* falloffFmt = "\xc2\xb1%.1f";
    bool gradient = false;
    const char* name = nullptr;  // --ui-script name when `label` is "##" (no caption)
};
bool RangeDrag(const char* label, float* lo, float* hi, const RangeSpec& s);

/** A 0..1 value as a 0-100 slider (`fmt` formats the percentage). */
bool PercentSlider(const char* label, float* v01, const char* fmt = "%.0f %%");

/** A FilterChip that flips `*v`. Chips flow with ChipFlow(ToggleChipWidth()). */
bool ToggleChip(const char* label, bool* v);
float ToggleChipWidth(const char* label);

/** An optional setting: a checkbox in front of the caption (named `label`).
 * Ticked = `field()` draws the value in the field column (its widget fills
 * the width set for it); unticked = `unsetText` dim in that column. Returns
 * true when the checkbox flipped or the field reported a change. */
template <class F>
bool OverrideRow(const char* label, bool* on, const char* unsetText, F&& field);
bool BeginOverrideRow(const char* label, bool* on, const char* unsetText, bool* flipped);
void EndOverrideRow();

/** A collapsed-by-default disclosure row: chevron, `label`, and `summary` dim
 * at the far end so the hidden values stay readable. Returns open. */
bool Disclosure(const char* label, const char* summary);

/** A node inspector header: `icon` on a `tint` square, `title` bold over a dim
 * `subtitle`, and `n` square glyph buttons at the far right. Returns the index
 * of the button clicked (-1 none); `hovered` gets the hovered one's index. */
struct HeaderButton {
    const char* label;  // ID, --ui-script name and tooltip
    Glyph glyph;
    bool active = false;
};
int NodeHeader(const char* title, const char* subtitle, Glyph icon, ImVec4 tint, const HeaderButton* buttons,
               int n, int* hovered = nullptr);

template <class F>
bool OverrideRow(const char* label, bool* on, const char* unsetText, F&& field) {
    bool flipped = false;
    bool changed = false;
    if (BeginOverrideRow(label, on, unsetText, &flipped)) {
        changed = field();
        EndOverrideRow();
    }
    return changed || flipped;
}
// --- end node inspector kit ---

// --- picture inspector kit ---------------------------------------------------
// Pieces of an inspector that shows the thing instead of listing its numbers
// (the Road's Properties, docs/roads.md "Properties"): option tiles with a
// drawing, a segmented choice that --ui-script can assert, thumbnail tiles for
// assets, inset cards, switchable card headers, chips that carry a value and
// a dashed "add" row. Same label=ID contract as above.

/** A segmented choice (2-5 options) like toolkit's Segmented, but every option
 * reports itself to --ui-script as checkable with its state, and the whole
 * control can be disabled (`enabled` false: dimmed, clicks ignored). Option
 * labels are their IDs inside PushID(id). `width` 0 = fit, < 0 = fill. Returns
 * true when *cur changed. */
bool Choice(const char* id, const char* const* labels, int count, int* cur, float width = 0.0f,
            bool enabled = true);
/** Choice as a caption-left inspector row (`label` = caption and ID scope). */
bool ChoiceRow(const char* label, const char* const* labels, int count, int* cur, bool enabled = true);

/** What an option tile draws in its picture area: `mn`..`mx`, `ink` the
 * tile's current text colour (accent when selected). */
using TileDrawFn = void (*)(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 ink, bool selected);
/** A big option tile: a picture (drawn by `draw`) over `text`. `label` is the
 * ID and --ui-script name (reported checkable with `selected`), `text` what
 * it shows. `approx` outlines it in the accent without the fill (the value is
 * only close to this option). Returns clicked. */
bool OptionTile(const char* label, const char* text, TileDrawFn draw, bool selected, float width, float height,
                bool approx = false, bool enabled = true);

/** An asset tile: a square thumbnail on the left (`tex`, or a field-coloured
 * placeholder with `fallback`), `title` over a dim `sub` on the right, the
 * whole tile `width` x `height` (0 = fill / two lines). `flipY` for
 * framebuffer-rendered thumbnails. Returns clicked. */
bool AssetTile(const char* label, ImTextureID tex, Glyph fallback, const char* title, const char* sub,
               float width = 0.0f, float height = 0.0f, bool flipY = false);

/** An inset card: a rounded, raised block behind everything drawn until
 * EndInsetCard, with `pad` px inside it (0 = the default). Not a child window
 * - the contents keep their window's ID scope, inspector rows and
 * --ui-script names. Nesting is not supported. */
void BeginInsetCard(const char* id, float pad = 0.0f);
void EndInsetCard();

/** The header row of a switchable card: a chevron when `open` is non-null
 * (clicking the row toggles it; the hit box is named "Fold <label>"), a
 * coloured dot when `dot` is non-zero, `label` in the heading cut over a dim
 * `sub`, and a switch at the right end named `label` (reported checkable).
 * `onEnabled` false greys the switch (it then ignores clicks). Returns true
 * when the switch toggled *on. */
bool CardHeader(const char* label, const char* sub, bool* on, bool* open = nullptr, ImU32 dot = 0,
                bool onEnabled = true);

/** A dashed full-width row with a "+" and `label` (ID, script name, text), a
 * dim `hint` at its far end. Returns clicked. */
bool AddRow(const char* label, const char* hint = nullptr);

/** A chip that carries a value: `label`, then `detail` in the mono face.
 * `on` tints it in the accent (reported checkable with `on`), `focused`
 * draws a stronger ring (the one being edited). Returns clicked. */
bool DetailChip(const char* label, const char* detail, bool on, bool focused);
float DetailChipWidth(const char* label, const char* detail);

/** A dimension line drawn into a picture: arrowheads at `a` and `b`
 * (horizontal or vertical), and `text` on a small pill at its middle (null =
 * none). */
void DimensionLine(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, ImU32 col);

/** A round drag handle centred on `c` (an InvisibleButton named `label`,
 * `r` px radius): ring in the accent, filled while hot. Returns true while it
 * is being dragged; the drag delta is ImGui's io.MouseDelta. */
bool DragHandle(const char* label, ImVec2 c, float r, ImGuiMouseCursor cursor = ImGuiMouseCursor_ResizeEW);
// --- end picture inspector kit -----------------------------------------------

}  // namespace ui
