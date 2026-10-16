// The tool-window kit: the recurring pieces of the redesigned tool windows
// (docs/editor-theme.md, "Tool windows"). widgets.hpp holds the main
// window's controls; this holds what a TOOL is built from - a split view of
// list | detail | preview, thumbnail tiles, list rows, empty states, the one
// primary action, inline progress, "used by" lines, badges, segmented
// controls and cards.
//
// The rules are widgets.hpp's: the label is the ID and the --ui-script name,
// the return value is "clicked/changed this frame", sizes are in UI-scaled
// pixels (pass scaled values, or 0 for "fit"), and colours come from
// theme::semantics() - never a literal.
#pragma once

#include <imgui.h>

#include <string>
#include <vector>

#include "widgets.hpp"

namespace ui {

// --- buttons -----------------------------------------------------------------

/** THE action of a tool (Bake, Import, Render, Create): a filled accent button.
 * One per view. `width` 0 = fit the label; -1 = fill the row. */
bool PrimaryButton(const char* label, float width = 0.0f, bool enabled = true);
/** A destructive action: quiet until hovered, then the danger colour. */
bool DangerButton(const char* label, float width = 0.0f, bool enabled = true);
/** A text-only secondary action (no fill until hovered). */
bool GhostButton(const char* label, float width = 0.0f, bool enabled = true);

// --- layout ------------------------------------------------------------------

/** A vertical splitter between two panes laid out with SameLine: call between
 * the left pane's EndChild and SameLine; drags `*leftW` within [minL, total -
 * minR]. Returns true while dragged. `id` must be unique in the window. */
bool VSplitter(const char* id, float* leftW, float minL, float minR, float height);
/** The same between a top and a bottom pane. */
bool HSplitter(const char* id, float* topH, float minT, float minB, float width);

/** A card: a raised, rounded child region with padding (BeginChild under the
 * hood - always call EndCard). `size` as BeginChild; y 0 = auto-fit height. */
bool BeginCard(const char* id, ImVec2 size = ImVec2(0, 0));
void EndCard();

/** A small caps section caption ("MATERIALS") with an optional dim count and a
 * hairline to the right edge. Not collapsible (use ui::Section for that). */
void Caption(const char* text, const char* count = nullptr);

// --- collections ---------------------------------------------------------------

/** A list row: glyph (or none), label, dim sublabel on the right, selection
 * fill. The label is the ID. Returns clicked. `height` 0 = default. */
bool ListRow(const char* label, const char* sublabel, bool selected, Glyph glyph = Glyph::Box,
             ImU32 glyphCol = 0, bool showGlyph = true, float height = 0.0f);

/** A thumbnail tile for grids: a square `size` preview (texture when `tex` is
 * non-null, else the glyph on a raised tile), the caption below (ellipsized),
 * accent ring when selected. Returns clicked. Lay out with TileFlow. */
bool ThumbTile(const char* label, ImTextureID tex, Glyph fallback, bool selected, float size,
               const char* caption = nullptr);
/** Wraps the next tile to a new line when it would not fit (call before each
 * tile; `first` for the first one). */
void TileFlow(float tileW, bool first);

// --- states and feedback -----------------------------------------------------

/** An empty state: a large dim glyph, a title, one line of why, and (when
 * `action` is non-null) a primary button. Centred in the remaining space of
 * the current window/child. Returns true when the action was clicked. */
bool EmptyState(Glyph g, const char* title, const char* why, const char* action = nullptr);

/** Inline progress where the work happens: a thin bar (`fraction` < 0 =
 * indeterminate, animated) with a label on the left and an optional detail
 * (e.g. "12 / 40 probes · 0:42") on the right. */
void ProgressInline(float fraction, const char* label, const char* detail = nullptr);

/** A status line with a coloured dot: kind 0 ok, 1 warn, 2 danger, 3 info. */
void StatusLine(int kind, const char* text);

/** A small rounded badge ("stale", "3", "in use"). kind as StatusLine, 4 = dim. */
void Badge(const char* text, int kind = 4);

/** "Used by: a, b, c" - dim caption, each item a link-like button. Returns the
 * index of the clicked item, or -1. An empty list draws "Not used yet". */
int UsedBy(const char* caption, const std::vector<std::string>& items);

// --- choices -------------------------------------------------------------------

/** A segmented control (2-5 options) on a field-coloured track; the selected
 * option is a surface pill. Each option's label is its ID ("<label>##<id>"
 * when two controls share labels). Returns true when *selected changed. */
bool Segmented(const char* id, const char* const* labels, int count, int* selected,
               float width = 0.0f);

// --- runtime-b ---------------------------------------------------------------
// Start page, New Project, Preferences, loading screen, update and session
// pieces (docs/editor-theme.md, "Tool windows").

/** A status chip: a pill with a coloured dot (kind as StatusLine; 4 = dim)
 * and `text`. An InvisibleButton named `label` - returns clicked. */
bool StatusChip(const char* label, const char* text, int kind);
/** Width StatusChip(label, text, kind) takes. */
float StatusChipWidth(const char* text);

/** A busy spinner (a rotating arc in the accent), `radius` px, centred on `c`. */
void Spinner(ImDrawList* dl, ImVec2 c, float radius);

/** A check mark in `col` inside a `size` px box centred on `c`. */
void CheckMark(ImDrawList* dl, ImVec2 c, float size, ImU32 col);

/** A round monogram (initials) in `col`: tinted fill, ring and letters. */
void Monogram(ImDrawList* dl, ImVec2 c, float radius, const char* initials, ImU32 col);

/** A choice card (New Project's "Start as"): a raised rounded box `height`
 * high filling the row, accent ring when selected. The caller draws the
 * content into the rect it reports (GetItemRectMin/Max). Returns clicked. */
bool ChoiceCard(const char* label, bool selected, float height);
// --- screens-c ---
// Pieces the screen editors (Menu Editor, Menu Preview, Credits, Save Editor)
// share: pill tabs with a detail, quiet chip buttons, small icon buttons,
// meters and role pills.

/** A pill tab: `label` (the ID and --ui-script name) with `detail` dim in the
 * mono face after it ("end-credits 0:33"). Accent-tinted when `selected`;
 * reported as checkable with `selected`. Returns clicked. */
bool PillTab(const char* label, const char* detail, bool selected);
/** Width a PillTab with this label/detail takes. */
float PillTabWidth(const char* label, const char* detail);

/** A quiet rounded chip button ("+ Row", "Replay open"): a raised fill, the
 * accent tint when `active`. `width` 0 = fit. Returns clicked. */
bool ChipButton(const char* label, bool active = false, float width = 0.0f, bool enabled = true);
float ChipButtonWidth(const char* label);

/** The icons ToolIconButton draws. */
enum class ToolIcon { Play, Pause, Restart, PopOut, Up, Down, Plus };
/** A square icon-only button named `label` (the ID, --ui-script name and
 * tooltip). `size` 0 = one frame high. Returns clicked. */
bool ToolIconButton(const char* label, ToolIcon icon, float size = 0.0f, bool enabled = true);

/** A thin horizontal meter `width` px wide (0 = fill): `fraction` of it in the
 * accent, or the warn/danger colour past `warnAt`/`dangerAt`. */
void Meter(float fraction, float width = 0.0f, float warnAt = 2.0f, float dangerAt = 2.0f);

/** A small rounded tag in a colour (role pills: "Title screen", "Save menu").
 * kind as Badge (0 ok, 1 warn, 2 danger, 3 accent, 4 dim, 5 special). Draws at
 * the cursor and advances like Badge. */
void Pill(const char* text, int kind);
float PillWidth(const char* text);
// --- end screens-c ---
// --- assets-a ---
/** PrimaryButton whose visible text differs from its ID: `id` is the label
 * (the ID and the --ui-script name, kept stable), `text` what it shows -
 * "Import cat" on a button scripts still click as "Import". */
bool PrimaryButtonText(const char* id, const char* text, float width = 0.0f, bool enabled = true);

/** A full-width warning/info card: a tinted rounded block (kind as
 * StatusLine: 0 ok, 1 warn, 2 danger, 3 info) with a dot and wrapped text.
 * Returns true when it is hovered (for a tooltip with the long story). */
bool NoteCard(int kind, const char* text);
// --- end assets-a ---
// --- assets-c ---
// Pieces the Vehicle / Prefabs / Particle / Drone redesigns share.

/** A secondary action next to a PrimaryButton (Replace, Duplicate, Save
 * patch): surface fill and a hairline frame, so it reads as a button and not
 * as text. Same contract as PrimaryButton. */
bool SecondaryButton(const char* label, float width = 0.0f, bool enabled = true);

/** A square "?" button (InvisibleButton named `label`) for a tool's docs link
 * in its header row. `tooltip` may be null. Returns clicked. */
bool HelpButton(const char* label, float size, const char* tooltip = nullptr);

/** ThumbTile for textures that come out of a framebuffer (bottom-up: `flipY`),
 * with an optional dim `note` drawn right-aligned on the caption line (a
 * member count). Same ID/return contract as ThumbTile. */
bool ThumbTileEx(const char* label, ImTextureID tex, bool flipY, Glyph fallback, bool selected,
                 float size, const char* caption, const char* note);

/** A Badge that flows: wraps to a new line when it would not fit after the
 * previous item (`first` for the first of a run). `tooltip` may be null. */
void BadgeFlow(const char* text, int kind, bool first, const char* tooltip = nullptr);

/** An info card: a tinted rounded box (kind as StatusLine) with a ring and
 * wrapped text; `width` 0 = the rest of the row. */
void Callout(int kind, const char* text, float width = 0.0f);

/** A colour dot `d` px across at the cursor, as an item (list rows: effect
 * glows, layer colours, chord colours). `glow` adds a soft halo. */
void SwatchDot(ImU32 col, float d, bool glow = false);
// --- end assets-c ---

// --- runtime-a ---
// Pieces of the running-game tools (Debugger, Remote Pad, AI Assistant):
// stroke icons the interface font has no glyph for (media transport, eye,
// timer...), quiet icon buttons, state pills, a transport bar with key hints,
// a navigation rail row, KPI tiles and a thin meter. Same contract as above.

/** Icons drawn as strokes, so they never depend on the font (the transport
 * buttons once rendered as "? Pause" because U+23F8 was missing). */
enum class RtIcon {
    Play, Pause, StepFrame, StepNode, Camera, More, Eye, Nodes, Breakpoint, Bolt,
    Cube, Monitor, Bars, Lines, Timeline, Chip, Rewind, Timer, Copy, History,
    Plus, Sparkle, Send, Stop, Keyboard, Undo, ChevronUp, ChevronDown, ChevronRight,
    Check, Close, Question, Pencil, Record
};
/** Draws `icon` centred on `c` in a `size` px box. */
void DrawRtIcon(ImDrawList* dl, RtIcon icon, ImVec2 c, float size, ImU32 col);

/** A square quiet icon button (`size` px; 0 = frame height): no fill until
 * hovered, accent tint when `active`. `label` is the ID, the --ui-script name
 * and (when `tooltip` is null) the hover text. Returns clicked. */
bool IconButton(const char* label, RtIcon icon, float size = 0.0f, const char* tooltip = nullptr,
                bool enabled = true, bool active = false);

/** A rounded state pill at the cursor: tinted fill + border in the kind's
 * colour (kind as StatusLine; 4 = dim), a dot, or `icon` when given. */
void StatePill(const char* text, int kind, bool withIcon = false, RtIcon icon = RtIcon::Pause);
float StatePillWidth(const char* text);

/** One button of a TransportBar. `key` (may be null) is drawn as a keycap;
 * `primary` fills it with the accent (one per bar: what to do next). */
struct TransportItem {
    const char* label;  // ID / --ui-script name
    RtIcon icon;
    const char* text;   // what it shows (the label's visible part when null)
    const char* key;
    bool primary;
    bool enabled;
    const char* tooltip;
};
/** A segmented row of icon + text buttons on one track. Returns the index of
 * the clicked item or -1. */
int TransportBar(const char* id, const TransportItem* items, int count);
float TransportBarWidth(const TransportItem* items, int count);

/** A navigation rail row: icon, label, a right-aligned dim count (kind as
 * Badge colours its text; 4 = dim), the selected row filled with an accent
 * edge. `compact` draws the icon alone (the label becomes the tooltip).
 * Reported to --ui-script as checkable with `selected`. Returns clicked. */
bool RailRow(const char* label, RtIcon icon, const char* count, int countKind, bool selected,
             bool compact);

/** A KPI tile `w` px wide: a small label, a big mono value in `valueCol` (0 =
 * text colour) and a dim sub line. Not interactive. */
void KpiTile(const char* label, const char* value, const char* sub, ImU32 valueCol, float w);

/** A thin full-width meter: up to three segments (fractions of the whole, in
 * order) on a field-coloured track. */
void Meter(const float* fracs, const ImU32* cols, int n, float height = 0.0f);

/** A dim caption with a swatch, for legends ("frame buffers 1.64"). */
void LegendSwatch(ImU32 col, const char* text);
// --- end runtime-a ---

// --- screens-a ---------------------------------------------------------------

/** A status pill for a header ("Edits need a build · 3 nodes", "Live · 30 Hz",
 * "0 problems"): a dot and the text on a pill tinted by `kind` (as StatusLine;
 * 4 = dim). An InvisibleButton named `label`; returns clicked. */
bool StatusPill(const char* label, int kind);
/** Width StatusPill(label) takes. */
float StatusPillWidth(const char* label);

/** A square ghost button showing glyph `g` (an InvisibleButton named `label`),
 * accent-tinted while `active`; `tooltip` shows on hover. `size` 0 = one frame
 * height. Returns clicked. */
bool GlyphButton(const char* label, Glyph g, float size = 0.0f, const char* tooltip = nullptr,
                 bool active = false);

/** A value chip: `text` in the mono face on a small field-coloured pill at the
 * cursor (a Dummy - not interactive). The compact way a node or a row shows
 * the value it holds ("lenses", "3", "self"). `dim` greys the text. */
void ValueChip(const char* text, bool dim = false);

/** The one big action of a recorder: a danger-red bar with a dot and `label`,
 * or - while `recording` - a square on a pulsing fill. `width` <= 0 fills the
 * row. Returns clicked (never while disabled). */
bool RecordButton(const char* label, bool recording, float width = -1.0f, bool enabled = true);

// --- end screens-a -----------------------------------------------------------

// --- world-a ---
// Pieces of the world tools (Terrain, Ambience/GI, Color Grading, Disc Layout,
// Project Preferences) that are generic enough to share.

/** A big tool tile (icon over label, shortcut keycap in the corner) for a row
 * of mutually exclusive tools. `draw` (may be null) draws a custom icon
 * instead of `g`. Reported to --ui-script as checkable with `active`.
 * Returns clicked. */
using IconDrawFn = void (*)(ImDrawList* dl, ImVec2 centre, float size, ImU32 col);
bool ToolTile(const char* label, Glyph g, bool active, const char* key, float width, float height,
              IconDrawFn draw = nullptr);

/** A small stat tile: dim caption over a mono value. kind as StatusLine for
 * the value's colour (4 = plain text). `width` 0 = fill. Not interactive. */
void StatTile(const char* caption, const char* value, int kind = 4, float width = 0.0f);

/** A tinted note box (kind as StatusLine: 1 warn, 2 danger, 3 info) with a
 * wrapped text and, when `action` is non-null, a link after it. Returns true
 * when the link was clicked. Full width of the content region. */
bool Note(int kind, const char* text, const char* action = nullptr);

/** An accent-coloured text link (label = ID). Returns clicked. */
bool Link(const char* label);

/** A heading for a settings page: `title` in the heading cut, `sub` dim after
 * it on the same line. Not an item; draws and advances. */
void Heading(const char* title, const char* sub = nullptr);
// --- end world-a ---

// --- content ---
/** A two-handle range slider (a remap window, a band): a field-coloured track,
 * the range between the handles in the accent, the values in the mono face on
 * the right ("0.25 - 0.65", `fmt` per value). `label` is the ID and the
 * --ui-script name (not drawn). Pressing picks the nearer handle; the pair
 * stays ordered. `width` 0 = CalcItemWidth(). Returns true while a value
 * changes; IsItemDeactivatedAfterEdit() reports the end of a drag. */
bool RangeSlider(const char* label, float* lo, float* hi, float vmin, float vmax,
                 const char* fmt = "%.2f", float width = 0.0f);
// --- end content ---

// --- drone ---
// The Simple views' rows (Ambience Editor > Sky & time, Drone Generator):
// a caption in the label column, a thin track with one knob, the value as a
// word at the right end, and a segmented control that can say "close to".

/** A dim row caption (dotted underline + `tip` tooltip when given), then
 * SameLine(labelW) so the control starts in the label column. */
void RowLabel(const char* text, const char* tip, float labelW);

/** A thin track with one knob and the value as text at its right end. The
 * InvisibleButton is named `id` (the --ui-script name; scripts drag it).
 * `fromCol`/`toCol` non-zero paint the track as a gradient instead of filling
 * up to the knob. `detail` null draws `shown` in the mono face; non-null draws
 * `shown` as a word and `detail` dim and mono after it ("deep E1"). `valueW`
 * reserves at least that much for the text so the track does not jitter as
 * the word changes. Returns true while the value changes; the usual
 * IsItemActivated / IsItemDeactivated report the drag. */
bool TrackSlider(const char* id, float* v, float lo, float hi, const std::string& shown,
                 ImU32 fromCol = 0, ImU32 toCol = 0, const char* detail = nullptr,
                 float valueW = 0.0f, bool enabled = true);

/** Segmented with an "approx" state: `selected` -1 and `approx` >= 0 draw
 * that option outlined in the accent (the value is only CLOSE to it). Each
 * option reports checked to --ui-script. `enabled` false greys every option
 * (hover still reaches the caller's tooltip via IsItemHovered on the last
 * one is NOT guaranteed - use the returned `hovered`). Returns true when an
 * option was clicked (even the selected one: a click re-applies it). */
bool SegmentedApprox(const char* id, const char* const* labels, int count, int* selected, int approx,
                     float width = 0.0f, bool enabled = true, bool* hovered = nullptr);
// --- end drone ---

}  // namespace ui
