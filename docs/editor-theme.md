# The editor's look: themes and the interface font

The editor is built on [Dear ImGui](https://github.com/ocornut/imgui), and for
a long time it looked exactly like it: the stock `StyleColorsDark()` palette and
ImGui's built-in bitmap font. Both are now replaced — the editor draws itself
in its own typeface, Geist, and in one of four themes.

Nothing here reaches the PS2. This is the editor's own chrome; a game's look is
decided by its assets, its materials and its [ambience](ambient-occlusion.md).

![The same editor panel in the four themes: Graphite (graphite with a blue
accent), Boot screen (navy with an electric blue), Memory card (violet) and
ImGui dark (the stock palette). All four share the same rounding, hairline
borders, spacing and interface font - only the palette
changes.](img/editor-themes.png)

## Picking a theme

**View > Theme**, or **Edit > Preferences > Appearance**. The change applies the
moment you pick it — there is no OK to press, because a theme you cannot see
until you confirm it is a theme you cannot choose.

| Theme | What it is |
|---|---|
| **Graphite** (default) | Deep graphite panels; four colours do the semantic work — blue as the accent, green for "running", red for "stopped", pink for "live" — rather than a tint over every surface, which is what keeps a full-screen editor calm. (Stored as `faceButtons`, its key before 1.173.0.) |
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

The editor draws itself in **Geist**, its own typeface, built into the
executable (`resources/fonts`, SIL Open Font License 1.1 - see
THIRD-PARTY-LICENSES.md):

- **Geist** for the interface,
- **Geist SemiBold** for panel and section titles,
- **Geist Mono** for numbers, fields that are dragged, logs and code - digits
  that change width while a value is dragged make a field jitter, and a log is
  read in columns.

The same letters on Windows, Linux and macOS, whatever the machine has
installed. Behind each Geist face the editor merges a **system face as a
fallback** for the glyphs Geist does not have (the macOS Command sign, some
arrows, the scripts of a project name in another alphabet): the first one this
machine has of

- **Windows** — Segoe UI (Semibold for titles), then Tahoma, Verdana, Arial;
  Cascadia Mono or Consolas behind Geist Mono.
- **macOS** — Helvetica Neue (Medium for titles), then Verdana, Arial, Geneva;
  Menlo behind Geist Mono.
- **Linux** — Inter, then Noto Sans, Ubuntu, Cantarell, DejaVu Sans, Liberation
  Sans; JetBrains Mono, Noto Sans Mono, Ubuntu Mono, DejaVu Sans Mono or
  Liberation Mono behind Geist Mono.

Variable-weight fonts (Segoe UI Variable, SF Pro, SF Mono) are never picked:
the rasterizer cannot select a weight axis and would draw them at the wrong
one. There is **no font picker** - the typeface is part of the design, not a
per-user decision worth a setting. Preferences says which faces are in use.

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
- **Each panel is a rounded card**: its tab strip rounded on top, its body at
  the bottom. Stock ImGui rounds only the corners that touch the window's edge,
  so this is a small patch to Dear ImGui (`ImGuiStyle::DockNodeRounding`),
  which is why the editor carries its own copy of ImGui in
  `vendor/imgui-tyrax` rather than fetching it - every change there is marked
  `// TYRAX:` and listed in its `TYRAX-PATCHES.md`.
- **Value fields are inset**: darker than the panel, with the hairline frame
  border as their edge. Hovering lifts the fill instead of tinting it, and
  buttons hover lighter rather than bluer — sweeping the cursor over a row of
  controls should not flash the accent at each one. The accent is for a press,
  a selection and an active edit.
- **Sliders are a thin track, not a field.** Every slider (a range: volume,
  brightness, a count) is a 4 px track filled in the accent up to a round
  white knob, with the value in the mono face at the right end of the row -
  the look of the tool designs. A click on the value, a double-click anywhere
  or Ctrl+click types a number into a field over the whole row. Numeric drag
  fields (positions, sizes) keep the inset-field look: they are numbers, not
  ranges. This is another small ImGui patch (`ImGuiStyle::SliderThinTrack`),
  so it applies to every `ImGui::SliderFloat` / `SliderInt` in the editor.
- **X, Y and Z are named inside the field** (`ui::AxisDrag3`) in the move
  gizmo's colours, which are the same in every theme: a red X is a
  convention, not a palette choice.
- **Panels name themselves; tabs only where there is a choice.** A panel alone
  in its dock node has no ImGui tab strip and no window-menu triangle. It draws
  a header row instead: the title in the heading cut, a dim count, and icon
  buttons on the right (`ui::PanelHeader` / `ui::PanelHeaderButton`). The
  Project panel's header names its page ("Outliner 81") and carries the **+**
  that adds an object; the Viewport (header strip over the image, which then
  rounds all four corners) and Properties name themselves the same way. The header's empty part is the grab handle: drag it to
  undock the panel and drop it on any node, exactly as you would drag a tab.
  This is opt-in per window (`ui::PanelChromeNewFrame` in `drawUI`) and is never
  written into a saved layout, because it goes through the window class's
  `DockNodeFlagsOverrideSet` rather than the node's own flags. A node holding
  several windows keeps its tabs, drawn as quiet pills
  (`ImGuiStyle::TabPillInset`, an in-tree ImGui patch): the front tab is a
  surface-filled pill, the others dim labels.
- **Checkboxes fill with the accent and knock the tick out of it** in the page
  colour, instead of drawing an accent tick on a dark square.
- **Scrollbars have no track** and a grab that only asserts itself under the
  cursor: a scrollbar is chrome, not content.
- **Trees draw hierarchy lines** (ImGui's `DrawLinesToNodes`), which is most of
  what makes a deep object list scannable.
- **Title bar.** On macOS and Windows the system title bar is merged into the
  top bar: the bar is the window's top edge. On macOS the traffic lights sit
  inside it, centred, and the logo starts after them; on Windows the bar ends
  with its own minimize, maximize/restore and close buttons. Any empty part of
  the bar moves the window when dragged and zooms or maximizes it on
  double-click (macOS follows the system's double-click setting; on Windows a
  right-click opens the system menu). *Edit > Preferences > Appearance > Use the
  native title bar* brings the OS title bar back at the next start (editor.ini
  `nativeTitleBar`). Linux always keeps the desktop's own decorations.
- **The window has a frame: a top bar and a status bar, both on the ground.**
  The top bar starts with the mark (the "Tx" app icon, "TyraX" and a caret): one button that opens the ten menus as submenus of one popup (F10
  and a lone Alt tap open it too). Then a breadcrumb — the project's name (its tooltip is the name, location, terrain
  and target the Project panel used to list; a click opens Project
  Preferences), `/`, and the **scene picker** with a *start* badge on the scene
  the game boots into — and an **N unsaved** marker (undo steps plus
  project-wide edits since the last save) that is also the save button. The
  room the menus used to take goes to the workspace labels first, then to a
  search field ("Search objects, assets, commands..." + its keycap) centred in
  the free space; on a narrow window it shrinks to the magnifier. On the right: the build profile pill, the **PCSX2 | PS2** target
  switch, Build, a filled **Run** in the target's colour (with the run variants
  under its caret) and Stop. The **status bar** at the bottom carries the
  build state and the LIVE / DBG / LOGIC / SESSION chips on the left, the last
  status message after them, and the scene's object and selection counts and
  the interface scale on the right. Both are drawn in `src/shell_ui.cpp`.
- **Workspaces and the command palette.** Next to the breadcrumb, four
  workspaces, each with an icon of what it is for: **Scene** (a cube),
  **Logic** (a node graph), **Cinema** (a film frame) and **UI & HUD** (a
  screen). Each is one of the project's window
  layouts - Default, Debugger, Director and Menu Designer, found by their
  built-in recipe - so it is the Layout menu one click closer, and your own
  arrangement of each is kept. With little room the labels go and the icons
  stay. **Ctrl+K** (Cmd+K on a Mac), or the magnifier before the build
  profile, opens the **command palette**: type to find any menu command (shown with
  its menu path and shortcut - the palette reads the menus themselves, so it
  never misses one), the top bar's Run / Stop / build profile, New scene, an
  object of the active scene (Enter selects it and centres the camera on it),
  a scene or a documentation page (opened on GitHub). Typing **bake** lists every
  bake the editor can start - they are items of the **Build** menu (global
  illumination for this scene / all scenes, model ambient occlusion, pending
  pre-lit models, shadow decals for this scene / all scenes, stale procedural
  volumes), each running the same code as the button in its tab, with a hint
  that says what it bakes; a bake that is off or already running is not listed. Arrows move,
  Enter runs, Esc closes; Ctrl+Enter (Cmd+Return) opens the AI Assistant with
  the query as its draft.
- **The Outliner** filters by single-select chips (All, Models, Primitives,
  Lights, Areas, Audio, Cameras, Other), shows layers, prefabs and groups as
  folders with counts, gives each row an eye (on hover, or always while off)
  that hides that object in the editor (saved with the object as
  `editorHidden` and undoable; the game still draws it), and ends in a strip of layer chips
  that toggle a layer's editor visibility.
- **The left rail picks the Project panel's page.** Outliner, Layers, Assets,
  Audio (music and sounds) and Scripts each get the whole panel instead of
  sharing it as a stack of collapsing headers; the Outliner's list takes the
  full height. At the bottom of the rail: the AI Assistant and Project
  Preferences. Clicking a page also brings the Project panel forward if it is
  docked behind another tab. The Outliner rows draw a type icon, the name and
  the type (dim, right-aligned) themselves; the row's label is still
  `name  (type)` - the ID and what scripts name it by.
- **Assets, Audio and Scripts share one shape** (`src/rail_ui.cpp`): header
  actions, search and filters, a list that takes the height, rows that say
  what uses them, one fix per problem, a one-line hint at the bottom.
  - **Assets** is a pocket Asset Browser: type chips with counts, a folder
    picker, **Unused**, Grid | List, thumbnails with a used-by badge (amber
    0), models first. Drag a model onto the viewport or double-click it to
    place it; the selection drawer has *Add to scene* (*Edit material* for a
    material) and *Open in Asset Browser*. The built-in HUD sprites in
    `res/hud` show only when that folder is picked. Moving, renaming and
    deleting stay in the browser.
  - **Audio** starts with the SPU2 budget bar (a segment per sound; music
    streams and takes none), then Music and Sounds. Every row has a round
    **play** button (the editor plays the file; music shows its progress),
    length, size and its first use. A selected track shows its PS2 build
    settings (Keep / 48k / 32k / 22k, Mono) and its users; the fixes are
    *Convert*, *Edit in Drone Generator* and, for a sound too big for SPU2
    that nothing plays as a sound, *Make music*.
  - **Scripts** filters All | Flow graphs | C++ | VU. Flow graphs are the
    active scene's objects with a graph, their node count and trigger
    nodes, and open in the Flow Graph. C++ files show the classes they
    register (a *global* tag for `TYRA_SCRIPT`) and the objects each class
    is attached to; a class nobody attaches has *Attach...* (to the selected
    object), and a class an object attaches that nothing registers is listed
    as *missing* with *Create*. The last build's first error in a script
    sits on top with *Open file*.
- **The viewport's controls float on glass cards.** After `render()`,
  `Viewport::blurBackdrop` makes a quarter-size gaussian-blurred copy of the
  viewport image (two reused framebuffers, GL 3.3); `ui::SetOverlayBackdrop`
  hands it to `ui::OverlayGlassPanel`, which draws each card as the blurred
  image under a 60% ground tint and a hairline (no blurred texture: the plain
  `OverlayPanel`). Top left, the tool palette (Move/Rotate/Scale |
  Sculpt/Paint | Measure, with shortcut digits). Top centre, a view pill:
  projection (`Proj: <name>`), look-through camera (`View: <name>`), `Render
  mode`, `Surface snap` and `World`/`Camera` - it wraps to two pills or hides
  when there is no room between the palette and the axis gizmo. Bottom left,
  the gear (its menu starts with *Center view*) and the selection readout -
  colour, name, type, triangles and size in metres - with **Frame** (key F; its
  ID is still `Center selection`). Bottom right, **Editor | As PS2**
  (`viewportPs2_`) with the GS framebuffer size. While Sculpt or Paint is on,
  a brush card sits at the bottom centre - Radius, Strength, the Flatten (+
  Level) or Erase switch, and a dim line saying what a drag does - in the
  bottom row's gap when it fits, else one row above it; a press on the card
  never strokes the ground under it (`brushCardMin_`/`brushCardMax_`). Visible text may differ from
  the ID (`ui::OverlayMenuButton(label, text, ...)`); every old label is still
  the ID. The viewport image rounds its bottom corners with the card.
- **The Properties panel is headed by the object** - a tile in its colour, its
  name as a large borderless field (still the *Name* input), its type - and a
  "..." menu with Duplicate and Delete object. **The inspector reads as rows**:
  every field's caption sits in a dim column on the left and the control fills
  the rest (`ui::InspectorRows` and the `ui::` field wrappers - each still
  receives its exact label, so IDs, script names and saved open states are
  unchanged); help that used to be a trailing "(?)" lives on the caption
  (dotted underline, hover). Switches (`ui::Switch`, a drop-in for `Checkbox`
  that reports itself as checkable to `--ui-script`) are settings-list rows
  with the switch on the right; a label "Name (what it does)" shows the
  parenthesis as a smaller dim second line. A solid object's sections sit on
  three quiet tabs - **Object**, **Material**, **Physics** (`Object##ptab`...),
  accent-underlined, no tab fill - and the choice sticks for the session.
  Colours are a swatch with a picker and a #RRGGBB field (`ui::ColorHex`),
  the 0-255 fields one arrow away. Sections are titled in the heading cut over
  a hairline (`ui::Section`), not a tinted band.
- **The console drawer.** In the Default layout Output, Debug and the Debugger
  share the bottom node. Log rows carry a dim mono time column (`textDim` at
  75%), a gutter mark on error and warning lines, and one header row of
  actions, *Select text* and right-aligned level pills. The Output and Debug
  tabs show a count pill - `danger` for errors, `warn` for warnings only, its
  ink chosen by the pill's luminance - drawn over the tab's stock
  unsaved-document slot (`ImGuiWindowFlags_UnsavedDocument`), so a tab never
  changes its name or id.
- **Hover highlights fade in** (~80 ms) on the hand-drawn top-bar controls and
  status chips. Sweeping the cursor across a row of icons is the most common
  thing anyone does with that bar, and a hard rectangle appearing under each one
  in turn is the difference between a toolbar and a debug overlay. A *held*
  button jumps straight to its pressed fill — a press must feel immediate.
- **The node canvases** (Flow Graph, Procedural) are tinted to match, and are
  deliberately *darker* than a window: a graph is a surface you look into.
  Per-node and per-pin colours are left alone — those encode node category and
  pin type, which is data, not decoration.

## Tool windows

Every tool window follows the same few rules, and is built from the same kit
(`src/toolkit.hpp`, on top of `src/widgets.hpp`):

- **The thing being made is the hero.** A visual tool's preview sits in the
  middle and never shrinks (Material, Particle, Tree, Vehicle, Credits, Menu,
  UI Editor, Color Grading's before/after split, the Cutscene Director's own
  shot via `Viewport::renderShot`); its controls float on it as glass or
  overlay pills. A tool that edits a collection is **list | preview |
  inspector**, with thumbnails where a picture says more than a name.
- **The answer before the machinery.** A verdict banner or summary tiles come
  first (Texture Atlas, Static Batches, Neural Upscaler, VU Programs), then
  list | detail.
- **One primary action**, filled, visible, with its progress where the work
  happens (`ui::PrimaryButton`, `ui::ProgressInline`). An action that STARTS
  something live (Run, the Vehicle Editor's *Drive it*) is the green "go"
  primary with a play triangle (`ui::GoButton`). Destructive actions are
  quiet until hovered (`ui::DangerButton`), usually behind a "..." menu.
- **Say what will happen.** Footers state exactly what a click writes
  ("writes res/models/cat.glb + cat.mtl"); an option states its consequence on
  a dim second line.
- **Show the links.** "Used by", "started by", "handled by" lines connect data
  to the graphs, menus and objects that use it, and click through to them
  (`ui::UsedBy`, `App::openGraphOf`).
- **Empty states say why and offer the action** (`ui::EmptyState`) instead of a
  bare "No X."
- **Sections over tabs when there are many** (the Debugger's grouped rail);
  quiet underlined tabs when there are a few.
- **Docked, not floating.** Tools that change the scene open beside the
  viewport (`App::dockToolWindows`: Side tools join the Properties column,
  Visual ones split off the viewport when it is wide enough, Pages become a
  tab in its node); a saved floating position is re-docked once per session.
- **Labels stay IDs.** Where the visible text names the object ("Import cat")
  the stable script name stays the ID (`ui::PrimaryButtonText`,
  `ui::OverlayMenuButton`).

The kit's sections are per area (`// --- runtime-a ---` ...): transport bars
and stroke icons that never depend on the font (`ui::TransportBar`,
`ui::RtIcon`), pills and chips (`StatusPill`, `PillTab`, `ChipButton`,
`ValueChip`), tiles (`ToolTile`, `StatTile`, `KpiTile`, `ChoiceCard`,
`ThumbTileEx`), notes (`NoteCard`, `Callout`, `Note`), meters and the
`RecordButton`.

**Controller.** Every place that shows or picks a pad button draws the same
DualShock 2 (`ui::PadDiagram`, the kit's `pad` section, `src/pad_widget.cpp`):
Remote Pad drives it (*Interactive*), the Input Map labels it (*Annotated*:
callout pills in fixed slots, leaders that never cross), pickers and AI cards
use a small one (*Compact*), and table cells and combo previews an inline
button token (`PadButtonGlyph`; `PadButtonCombo` is the picker). Pressed looks
the same whatever pressed it; the body follows the theme
(`theme::padColors()`: Graphite and light pages have their own tables, the
other themes derive theirs), but the four face symbols keep the real pad's
colours in every theme. Each element is an item named after its button (`L2`
... `R3`), so scripts click and assert it by name.

## Windows outside the main window

Drag a panel's tab (or a lone panel's header strip) out of the editor and it
becomes a window of its own - onto a second monitor, say. Drag its tab onto a
dock target in the editor to dock it back; dropped anywhere over the editor
without docking, it simply floats inside the main window again. *Edit >
Preferences > Appearance > Windows can leave the main window* turns this off
(editor.ini `multiViewports`, applied at once; on by default on Windows, macOS
and Linux X11, off and unavailable in a Wayland session - Wayland gives an app
no say over where its windows go, and ImGui's GLFW backend does not support
it there).

- **They belong to the editor**: no taskbar / Dock icon of their own, and they
  stay above the main window (an owned window on Windows, a child window on
  macOS - which also means it follows the editor when you move the editor).
  Popups and menus opened over one get an OS window of their own on top.
- **They look like the panels**: square corners and an opaque background (ImGui
  does that for any window that owns an OS window), the theme's colours, the
  same title strip. A viewport moved out keeps its frosted overlay cards - the
  blur reads the viewport's own image, not the main window. Previews (Prefabs,
  Asset Browser, Cutscene shots...) render as before: all GL work happens on
  the main context, whose textures every window shares.
- **Layouts remember them**: a saved layout stores a floating window's desktop
  position. When it is loaded, anything that would land off every screen (a
  monitor that is gone) or, with the preference off, outside the main window,
  is moved back in first.
- **Layout > Bring all windows back** brings back every window that sits in
  an OS window of its own or reaches past the editor's edge: a panel that was
  docked in the editor this session docks where it was (its old place - and
  when dragging it out had merged that place away, the same side of the same
  split, recreated); a window that was never docked (a tool opened floating)
  comes back inside, floating. Turning the preference off does the same.
- The command palette always opens over the main window and dims every window,
  the panels in OS windows of their own included; so does a modal dialog.

UNVERIFIED on Windows and Linux X11 (built and driven on macOS only); real
mouse dragging between OS windows was not tested by a human either.

## For developers: adding a colour, adding a theme

`src/theme.hpp` is the whole interface. The rule that keeps the themes honest:

> A widget asks for **meaning**, never for a colour.

Anything that cannot go through an `ImGuiCol_` — the top bar's vector icons, the
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
