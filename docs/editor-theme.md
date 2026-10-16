# The editor's look: themes and the interface font

The editor is built on [Dear ImGui](https://github.com/ocornut/imgui), and for
a long time it looked exactly like it: the stock `StyleColorsDark()` palette and
ImGui's built-in bitmap font. Both are now replaced — the editor picks a real
system UI font and draws itself in one of four themes, three of which are
deliberate nods to the console it builds games for.

Nothing here reaches the PS2. This is the editor's own chrome; a game's look is
decided by its assets, its materials and its [ambience](ambient-occlusion.md).

![The same editor panel in the four themes: Face buttons (graphite with a blue
accent), Boot screen (navy with the PS2 logo blue), Memory card (violet) and
ImGui dark (the stock palette). All four share the same rounding, hairline
borders, spacing and interface font - only the palette
changes.](img/editor-themes.png)

## Picking a theme

**View > Theme**, or **Edit > Preferences > Appearance**. The change applies the
moment you pick it — there is no OK to press, because a theme you cannot see
until you confirm it is a theme you cannot choose.

| Theme | What it is |
|---|---|
| **Face buttons** (default) | Deep graphite panels; the PlayStation's four face-button colours do the semantic work — cross blue as the accent, triangle green for "running", circle red for "stopped", square pink for "live". The reference is in the colours you recognize rather than in a tint over every surface, which is what keeps a full-screen editor calm. |
| **Boot screen** | The deep navy the console fades up from, with the logo's electric blue. Every surface carries the tint, so it reads as one piece of hardware — at the cost of being less neutral than the graphite. |
| **Memory card** | The violet of the console's own browser. The most retro of the three, and the only one whose accent is not a blue. |
| **ImGui dark** | Dear ImGui's stock colours, kept as an escape hatch and as a before/after reference. It still gets the shared style metrics and the interface font — only the palette is stock. |

The choice is **machine-global**, not project data: it lives in `editor.ini`
next to the UI scale, the navigation scheme and the emulator path, because what
an editor looks like is a property of the installation and of the person using
it, not of the game. Opening someone else's project — or joining their
[session](collaboration.md) — never repaints your editor.

The stored value is the theme's *key* (`faceButtons`, `bootScreen`,
`memoryCard`, `imguiDark`), so a config written by a newer build that names a
theme you don't have falls back to the default instead of failing.

## The interface font

The editor loads the first UI face it finds on this machine:

- **Windows** — Segoe UI, then Tahoma, Verdana, Arial.
- **macOS** — Helvetica Neue, then Verdana, Arial, Geneva.
- **Linux** — Inter, then Noto Sans, Ubuntu, Cantarell, DejaVu Sans, Liberation
  Sans.

Alongside it come two more faces:

- **The same family's heavier cut** (Segoe UI Semibold, Helvetica Neue Medium,
  Inter SemiBold...) for section titles. It is only used when it comes from the
  family that loaded — a heading in a different typeface reads as a bug, one in
  the regular weight merely as plain.
- **A fixed-width face** for numbers and logs: Cascadia Mono or Consolas on
  Windows, Menlo on macOS, JetBrains Mono, Noto Sans Mono, Ubuntu Mono, DejaVu
  Sans Mono or Liberation Mono on Linux. Digits that change width while a value
  is dragged make a field jitter; a log is read in columns.

Variable-weight fonts (Segoe UI Variable, SF Pro, SF Mono) are never picked:
the rasterizer cannot select a weight axis and would draw them at the wrong one.

Preferences shows which faces were picked. A machine with none of them keeps
ImGui's built-in bitmap font and everything still works — the font is the one
part of the look that degrades rather than being configured.

Two deliberate omissions. There is **no font picker**: the editor's job is to
look like the desktop it runs on, and a UI font is not a per-user decision worth
a setting. And **no font is bundled**, so the repository grows no dependency and
no font licence — the trade is that the same editor is a couple of pixels
different on two machines.

Text size follows **View > Interface scale** exactly as it did before (fonts are
rasterized dynamically, so a 250% editor re-bakes the glyphs instead of scaling
a bitmap).

## What a theme actually changes

A theme is a **palette of eleven colours**; every one of ImGui's ~60 colour
slots is derived from them (`src/theme.cpp`), and so are the two tones every
theme needs but none picks — the *ground* (the page pushed towards black) and
the *field* (between the two). The style *metrics* — rounding, padding,
border widths, scrollbar size, tree lines — are **shared by all four themes**,
including the stock one: they are what makes the editor read as one product, and
a theme is a palette, not a second layout.

A few things worth knowing because they are decisions rather than defaults:

- **Panels are cards on a ground.** The window behind the docked panels, and
  the 6 px gaps between them, are the ground colour; each panel is the page
  colour. ImGui draws the splitters between docked panels with the dockspace
  host's background, so the gaps come from pushing the ground colour around
  `DockSpaceOverViewport` alone (`theme::pushDockspaceColors`) — the panels
  submitted after it keep the page colour.
- **Value fields are inset**: darker than the panel, with the hairline frame
  border as their edge. Hovering lifts the fill instead of tinting it, and
  buttons hover lighter rather than bluer — sweeping the cursor over a row of
  controls should not flash the accent at each one. The accent is for a press,
  a selection and an active edit.
- **X, Y and Z are named inside the field** (`ui::AxisDrag3`) in the move
  gizmo's colours, which are the same in every theme: a red X is a
  convention, not a palette choice.
- **The selected dock tab is the page colour** and is marked by an accent
  overline. A tab the same colour as the panel below it reads as the panel's
  front edge; the line says which one is active.
- **Checkboxes fill with the accent and knock the tick out of it** in the page
  colour, instead of drawing an accent tick on a dark square.
- **Scrollbars have no track** and a grab that only asserts itself under the
  cursor: a scrollbar is chrome, not content.
- **Trees draw hierarchy lines** (ImGui's `DrawLinesToNodes`), which is most of
  what makes a deep object list scannable.
- **The menu bar carries the wordmark and a hairline in the accent colour.** The
  bar and the dockspace under it are both dark surfaces with no border between
  them; without that line the whole top of the window reads as one block.
- **Hover highlights fade in** (~80 ms) on the hand-drawn toolbar icons and
  status chips. Sweeping the cursor across a row of icons is the most common
  thing anyone does with that bar, and a hard rectangle appearing under each one
  in turn is the difference between a toolbar and a debug overlay. A *held*
  button jumps straight to its pressed fill — a press must feel immediate.
- **The node canvases** (Flow Graph, Procedural) are tinted to match, and are
  deliberately *darker* than a window: a graph is a surface you look into.
  Per-node and per-pin colours are left alone — those encode node category and
  pin type, which is data, not decoration.

## For developers: adding a colour, adding a theme

`src/theme.hpp` is the whole interface. The rule that keeps the themes honest:

> A widget asks for **meaning**, never for a colour.

Anything that cannot go through an `ImGuiCol_` — the toolbar's vector icons, the
BUILDING / LIVE / DBG / LOGIC / SESSION chips, viewport overlays — reads
`theme::semantics()`, which offers `accent`, `accentMuted`, `ok`, `warn`,
`danger`, `special`, `text`, `textDim`, `surface`, `border`, `ground`, `field`
and the gizmo's `axisX`/`axisY`/`axisZ`. A chip that wants "the
running-game green" asks for `ok` and moves with the theme; a chip that writes
`IM_COL32(95, 200, 115, 255)` stays green in a violet editor.

A new theme is one `Palette` literal plus one row in `theme::info()`'s table and
one `case` in `paletteOf()` — nothing else, because every colour slot is
derived. A new *semantic* colour is a field on `theme::Semantics` filled by
`apply()` for every theme, which is what stops a call site from inventing one.

Two integration points to respect if you touch this:

- **`App::applyTheme()` is colours + metrics + scale, in that order**, because
  `baseStyle_` *is* the themed style: the UI-scale path resets to it on every
  zoom step, so a theme that only wrote `ImGui::GetStyle()` would be undone by
  the next `Ctrl+=`.
- **`ImGuiStyle`'s constructor leaves `FontSizeBase` at zero** ("ask the font
  atlas on the first frame"), and the reference style carries that zero — so
  `applyUiScale()` restores the size the font was loaded at. Leave it zero when
  no system face resolved, which is exactly what makes ImGui keep the built-in
  font's own size.

Recurring controls live next door in `src/widgets.hpp` (`ui::`): the heading
and fixed-width faces (`ui::boldFont()`, `ui::monoFont()`, `ui::FontScope`) and
widgets such as `ui::AxisDrag3`. Each keeps ImGui's contract — the label is the
ID and the `--ui-script` name, the return value is "changed this frame", and
`IsItemDeactivatedAfterEdit()` right after it works — so it is a drop-in
replacement for the stock widget it restyles.

The theme module depends on nothing but ImGui — no `Project`, no GL, no `App` —
so the palette is a pure function of a theme id and can be read in one place
instead of being spread across the call sites that used to hardcode colours.
`platform::uiFontFiles()` and `platform::monoFontFiles()` are where the per-OS
font chains live, next to the
other two font lists ([the bake fonts](../src/platform.hpp)), because that file
is the one place OS differences are allowed to exist.
