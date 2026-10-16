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
 * wrapped text, full width. */
void Callout(int kind, const char* text);

/** A colour dot `d` px across at the cursor, as an item (list rows: effect
 * glows, layer colours, chord colours). `glow` adds a soft halo. */
void SwatchDot(ImU32 col, float d, bool glow = false);
// --- end assets-c ---

}  // namespace ui
