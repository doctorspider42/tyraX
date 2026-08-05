# Menu styling: a baked stylesheet for game menus

Design doc, not a user guide. Status: **shipped** - M1 through M5 are in, plus
the per-resolution work that was not in the original plan (see "Resolutions" in
[docs/menu-styles.md](../../docs/menu-styles.md), the user guide). This file is
kept for the reasoning and the traps; where the two disagree, the guide is
current.

Four things the plan got wrong or under-specified, corrected in place below:
the compatibility story is a CASCADE rather than a switch (the menu's own fields
stay the base, so there is no "the sheet decides everything" moment); the
classic sheet has to be EMPTY of rules, not a copy of the defaults; a
`row:disabled` cell is only worth baking for a row something can actually
disable; and the framebuffer is a different SHAPE per scan mode, which needed a
per-axis `Sprite::drawSize` in the engine fork.

Goal: a project can author the kind of menu a commercial PS2 title shipped —
full-screen art, real typography, a selection treatment that moves, per-row
icons and descriptions, disabled rows, scrolling lists, sliders, open/close
transitions — and author it through widgets, not by editing a text file blind.

The one-sentence answer: **the look of a menu becomes a stylesheet
(`menu-styles/*.menustyle`, CSS-shaped), the editor bakes it through ONE layout
engine, and the console draws the result as a handful of sprites.** Nothing
about the look is decided in C++ any more.

## Why today's menu cannot do it

A menu is currently ONE baked sprite plus a cursor. Everything about its
appearance is hardcoded in `menubake::bakePanelRGBA`
(`src/menubake.cpp:695`), and the numbers are literals in that function:

- palette: `kBg{10,14,28,225}`, `kText{235,240,245}`, `kDim{150,160,175}`,
  `separator{70,90,120}` (`src/menubake.cpp:27`)
- a 2 px border on all four sides, in the menu's single `accent` colour
- title centred, `+8 px` block, a 1 px separator rule at a fixed inset of 16
- entry labels left-aligned at **x = 56**, row pitch = `entrySize + 9`
- a hint line `"X OK    ▲ BACK"` at 11 px, 18 px off the bottom
- value labels right-aligned at `panelW - 28`

What an author can change: `accent` (one RGB), font name, `titleSize`,
`entrySize`, `panelW` (128/256/512), screen position, `showTitle`, and images
in five fixed slots (`GameMenu`, `src/project.hpp:1995`). That is the entire
styling surface, and none of it can express a highlight bar, a second text
colour, a per-row icon, a rounded frame or a gradient.

Hard caps that also have to move: `menubake::kMaxEntries = 8`
(`src/menubake.hpp:21` — note the `static_assert` tying the save menu's
rows-per-page to it), 512 px per texture axis, and one texture per menu.

## The feasibility layers — the load-bearing insight

The PS2 has no font and no runtime layout, but the *editor* is a rasterizer on
a host with unlimited time. So every candidate feature falls into exactly one
of three buckets, and the bucket decides its cost:

| Layer | What it can do | Runtime cost | Bake cost |
|---|---|---|---|
| **1. Baked pixels** | gradients, 9-slice frames, rounded corners, drop shadows, blurs, outlines, letter-spacing, per-element fonts/colours, arbitrary composition | **zero** | texture bytes |
| **2. Baked state cells** | selected / disabled / hovered row looks, per-row descriptions, option labels | one extra sprite per visible state | texture bytes ×states |
| **3. Sprite properties** | slide, fade, scale, tint, scroll — anything continuous | a few floats + one quad | none |

Layer 1 is where "professional" mostly lives, and it is free. Layer 2 is the
existing value-strip trick (`MODE_REPEAT` sub-rect sprite, `src/templates.cpp:5247`)
generalized. Layer 3 exists because `Tyra::Sprite` already carries
`position`, `size`, `offset`, `scale` and `color` (with alpha) —
`vendor/tyra/engine/inc/renderer/core/2d/sprite/sprite.hpp` — and
`renderer_core_2d.cpp:98` passes the colour straight through.

Nothing in this plan needs an engine change.

### The numbers that bound it

Screen is 512×448 (`docs/ps2-viewport.md`), so **a 512-wide panel is exactly
full width, 1:1, no stretch**. Texture heap is ~282 000 words ≈ 1.08 MB with
~2048 words of padding per allocation (`docs/gs-vram.md`):

| Panel | 32bpp | 4-bit + CLUT |
|---|---|---|
| 256×256 | 67 584 w (24%) | ~10 200 w (3.6%) |
| 512×512 (full screen) | 264 192 w (**93%**) | ~34 800 w (**12%**) |

Two conclusions the design has to carry:

1. **Full-screen menus are affordable only palettized.** Menu panels are *not*
   quantized today — `texbake` handles `res/models|materials|textures`, HUD
   images, fonts and credits pages, but nothing under `res/menus`
   (`src/texbake.cpp:366`+). A per-menu `quant` (the `CreditsRoll`/`GameFont`
   precedent) is a prerequisite, not a nice-to-have. Menu art is flat colour
   and text: it palettizes almost losslessly.
2. **Textures per menu, not sprites per menu, is the budget.** Aim ≤4 textures;
   and since every `renderer2D.render` call does a `useTexture` + CLUT update,
   draws must be **grouped by texture**, never interleaved per row.

## The decision: a real stylesheet, with a real GUI over it

`menu-styles/<name>.menustyle`, a CSS-shaped text file, is the source of truth
for how menus look. The Menu Editor edits it with widgets and shows the baked
result; a raw-text tab is there for people who prefer it.

This is the **Material Editor arrangement** (`.mtl` on disk is the truth, the
GUI stages and writes it), not a new invention, and it is the same
project-defined-text-file shape as `flow-nodes/*.flownode` (`src/flownode.cpp`)
and `screen-effects/*.screenfx` (`src/screenfx.cpp`) — loaded into a registry
by `menustyle::loadForProject` from `project::load`, before menus are read.

Why a file and not 60 more `GameMenu` fields:

- a look is worth **copying between projects** and **diffing in review**
- five selectors × ~25 properties as struct fields is a 120-field POD that has
  to be threaded through JSON, codegen, `operator==` and the collaboration wire
- the built-in styles ship as *content*, embedded like `ai-support/`, not as C++

What we give up by not putting it in the `.tyra`: undo/redo of style edits does
not come for free. The Material Editor already answers that — **its own undo
stack** — and this follows it.

### Compatibility: the switch is explicit, so nothing drifts

`GameMenu::style` empty (every existing project) = the **Classic** look, driven
by today's `accent`/`titleSize`/`entrySize`/`panelW`/`showTitle` fields, and it
must come out **pixel-identical** to the current bake. That is the M1 gate.

The moment a user clicks *Customize style…*, the editor writes a sheet seeded
from those fields, sets `GameMenu::style`, and from then on **the sheet decides
everything** — the legacy widgets disappear for that menu with a note saying
where they went. No format migration, no two-sources-of-truth window, no silent
change to any project that never opens the new tab.

## The language

Not a CSS parser — a CSS-shaped one, and the docs will say so plainly. ~400
lines: tokenizer, selector, declaration block, `var()` substitution, cascade
(element < class < id, state on top), and a diagnostics list with line numbers.

```css
@style "Neon"                    /* display name in the editor */

:root {
  --accent: #78d1ff;
  --ink:    #ebf0f5;
  --dim:    #96a0af;
}

panel {
  width: 512px;                  /* 128|256|512, or `screen` (crisp tiles) */
  padding: 18px 24px;
  quant: 4bit;                   /* palettization; the VRAM readout follows */
  background: linear-gradient(180deg, #0a0e1c, rgba(10,14,28,0.55));
  background-image: url(res/hud/frame.png) 9-slice 24px;
  border: 2px solid var(--accent);
  border-radius: 10px;
  shadow: 0 4px 12px rgba(0,0,0,0.6);
}

title {
  font: "Display"; font-size: 28px; letter-spacing: 3px;
  color: var(--accent); text-shadow: 0 2px 0 #001018;
  text-transform: uppercase; align: center;
  rule-below: 1px solid rgba(120,209,255,0.35);
  margin-bottom: 14px;
}

row            { height: 26px; padding-left: 40px; font-size: 16px;
                 color: var(--ink); icon-size: 18px; }
row:selected   { color: #ffffff; translate-x: 6px;
                 background: linear-gradient(90deg, rgba(120,209,255,0.35), transparent);
                 marker: url(res/hud/caret.png) left; }
row:disabled   { color: #55606f; }
row.danger     { color: #ff8a80; }
row.header     { font-size: 12px; color: var(--dim); letter-spacing: 2px;
                 align: left; selectable: no; }

value          { align: right; margin-right: 24px; color: var(--accent); }
value.bar      { display: bar; bar-size: 90px 8px; bar-fill: var(--accent);
                 bar-track: rgba(255,255,255,0.15); }

description    { area: below; height: 44px; font-size: 12px;
                 color: #b9c2cf; align: left; wrap: yes; }

hint           { font-size: 11px; color: var(--dim); align: center; }

list           { rows-visible: 8; scroll-marker: url(res/hud/arrow.png); }

@transition open   { fade 200ms ease-out; translate-y 12px; }
@transition cursor { 120ms ease-out; }

menu#pause     { panel { width: 256px; } }   /* per-menu scoped override */
```

Per-menu overrides live in the sheet as `menu#<name> { … }` blocks, which is
what keeps ONE source of truth when the Style tab is showing one menu.

## Architecture

Four new files, all following existing shapes:

| File | Shape it copies | What it is |
|---|---|---|
| `src/menustyle.hpp/.cpp` | `flownode.cpp` / `screenfx.cpp` | the parser, the `Sheet`/`Rule`/`Decl` model, the registry (`loadForProject`), diagnostics, the writer the GUI saves through |
| `src/menulayout.hpp/.cpp` | `aobake.cpp` / `decalproj.cpp` — host-only, no GL, no ImGui | **the layout engine**: `(GameMenu, Sheet, Project) → Layout` — a box tree with resolved rects, row geometry, state cells, texture list and VRAM estimate |
| `src/menustyle_ui.cpp` | `credits_ui.cpp` / `mateditor_ui.cpp` | the Style tab: widget groups per selector, raw-text tab, live preview, cost readout, its own undo |
| `menu-styles/*.menustyle` (embedded) | `ai-support/` + `cmake/embed_ai_support.cmake` | the built-in sheets, installed into a project on demand |

`menubake.cpp` keeps the rasterizer (it already owns fonts, `{{icons}}`,
`drawText`, image scaling, PNG encode) but stops deciding geometry: it consumes
a `Layout`. `menubake::panelLayout` becomes a thin wrapper over the engine for
the Classic sheet, so callers do not all change at once.

### The single-source rules (these are the invariants that keep it honest)

1. **One layout engine.** The baker, the editor preview and codegen all call
   `menulayout::layout()`. The credits precedent, stated in
   `credits_ui.cpp`: the bake IS the preview.
2. **The runtime is a data-driven compositor.** `renderGameMenu` gains no look
   of its own — it walks tables from `menu_data.gen.hpp`
   (`MenuLayerData`: texture, size, cell pitch; `MenuRowGeom`: rect, state cell
   ids; tween params) and draws them grouped by texture.
3. **A property that changes pixels only** never reaches codegen. A property
   that changes *where a sprite goes* is exactly what codegen carries.
4. **Every state that costs a cell is reported**, in textures and in % of the
   VRAM heap, before the build (the Credits Editor readout precedent).

### Baked artifacts per menu

| File | Contents | When |
|---|---|---|
| `<menu>.png` | chrome + title + decorations + hints + every row in its NORMAL state | always (a menu with no state styling stays ONE texture — today's cost exactly) |
| `<menu>-rows.png` | one cell per (row, state) that differs from normal | only when `:selected`/`:disabled` change pixels |
| `<menu>-values.png` | option labels / bar tracks | as today |
| `<menu>-desc.png` | one cell per row's description | only with a `description` area |

Scrolling comes free from the same trick: the static layer holds *all* rows and
the row-list sprite is a sub-rect **window** into it, so a 32-entry menu is one
256×512 texture and scrolling is an `offset.y` change. That is why the row list
must be its own box, separate from the chrome.

## Data model

`GameMenu` gains: `style` (sheet name), `styleClass`, `quant`, `rowsVisible`.
`MenuEntry` gains: `styleClass`, `icon` (text-icon name or PNG), `description`,
`enabledWhen` (save-value condition → the disabled state at runtime), and two
non-selectable kinds (`Header`, `Spacer`). `kMaxEntries` 8 → 32 (mind the save
menu `static_assert`).

Chain per the `tyra-editor-dev` rules: struct + `operator==` (undo drops fields
missing from it) → `write/readMenusSection` → Menu Editor UI + `commitChange()`
→ `menuDataHeader` codegen → the runtime compositor. Plus
`version::kFormatVersion++`, editor semver MINOR, and **no** migration step
(new fields default; Classic reproduces the old look).

Two easy-to-miss registrations: every asset path a *stylesheet* names
(`url(res/hud/frame.png)`) has to reach `App::retargetAssetPath` and
`App::rebuildAssetUsage` (assetbrowser.cpp) or the Asset Browser silently
breaks it on a move; and the new generated files must join
`refreshGenerated`'s always-overwritten list, or they are written once at
project creation and never refreshed (the `live_pad.gen.cpp` mistake).

## The editor UI

Three panes: menu list | **live preview** | tabs *Content* / *Style* / *Text*.

- **Preview** is the baked pixels at PS2 scale with the safe-area overlay, and
  it is *interactive*: arrows move a simulated cursor so `:selected` and the
  description pane are visible while editing; a Play button runs the open
  transition. It draws exactly what the console gets — including the
  palettization, so a 4-bit banding artifact is visible in the editor.
- **Style** is widget groups per selector (Panel / Title / Rows / Selected /
  Disabled / Values / Description / Hints / List / Motion) — colour pickers,
  gradient editor, 9-slice picker, sliders — each writing the sheet AST and
  re-baking immediately. Own undo stack.
- **Text** is the raw sheet with the diagnostics list under it.
- A **cost line** always visible: textures, KB, % of heap, sprites/frame, plus
  the honest failure states (*content clipped*, *cells clipped*) rather than a
  silent cut.
- Ship five sheets: **Classic** (compat), **Neon**, **Blade** (angular sci-fi),
  **Parchment**, **Minimal**.

## What this deliberately will NOT do

Say it in the docs, because the CSS framing invites all of it:

- no runtime layout or text reflow — a baked string is a build-time snapshot
  (a rebind row stays the atlas-font exception, `src/templates.cpp:8521`)
- no `%`/`em` for text sizes; px only, and pow2 texture axes ≤512
- no per-frame blur/shadow; those are baked, so they cannot follow motion
- no arbitrary z-order — the layer order is fixed (dim, chrome, rows, values,
  description, overlay, cursor)
- no animation of baked pixels; motion is sprite position/tint/scale only
- no media queries, no inheritance beyond the documented cascade, no
  calc()/functions beyond `var()` and the colour/gradient forms

## Milestones

Each is one PR with docs in the same commit.

- **M1 — engine + sheet, no new looks.** `menustyle` + `menulayout`, Classic
  sheet, `menubake` consuming a `Layout`. Gate: **pixel-identical panels** for
  every example project (`--refresh-gen`, then diff `res/menus/*.png` against
  `origin/main`'s). Nothing user-visible.
- **M2 — the GUI.** Style tab, live interactive preview, raw text + diagnostics,
  built-in sheets, per-menu `quant` in `texbake`, the cost readout. This is the
  milestone where a project can already look completely different.
- **M3 — states and volume.** `:selected`/`:disabled` cells, per-row class /
  icon / description, `Header`/`Spacer` rows, scrolling lists (`kMaxEntries`
  → 32), the runtime compositor + codegen tables.
- **M4 — motion.** Open/close transitions, cursor easing, value bars.
- **M5 — full screen.** `width: screen` (crisp tiles), tab/column layouts, and
  an example project that shows the whole thing off.

## Verification (per `tyra-testing`)

- a 40-line host harness over `menulayout` + `menustyle` alone (both are
  GL-free, ImGui-free, `Project`-light) — layout math, cascade, cell counts,
  and a parse round-trip (`sheet → text → sheet` must be stable, or the GUI
  silently rewrites people's files)
- `--bake-menus` (new headless verb) dumping every panel PNG for eyeballing and
  for the M1 pixel diff
- `--refresh-gen` on every example: codegen tables compile, no drift
- Docker + PCSX2 e2e with `--pad` driving a menu (up/down/left/right/X) and
  screenshots per state; `--ui-script` driving the Style tab in the editor
- VRAM: the readout's number checked against `emulog.txt` residency on the
  full-screen example; `--audit-release` unaffected (no devkit surface added)

## Traps to expect (paid for elsewhere in this repo already)

- **`refreshGenerated`'s overwrite list is hand-written** — a new generated file
  missing from it is written once and never again.
- **`operator==` gates undo** — a new `GameMenu`/`MenuEntry` field missing from
  it makes undo silently drop the edit.
- **Texture switches, not sprite count, are the cost** — group draws per texture
  or every row re-uploads a CLUT.
- **A `Selectable`'s label is its ImGui id** — the style tab will list class
  names and sheet names in combos that can collide; suffix `##idx`
  (`droneui.cpp`'s rule).
- **`OpenPopup`/`BeginPopupModal` must use the same string**, and an
  open-but-undrawn modal eats every click (PROGRESS 215).
- **Wrap every pixel literal in `scaled()`**; the preview is full of them.
- **Mirror the format in the VS Code extension** (`tools/vscode-tyrax`) the way
  `.flownode`/`.screenfx` are, or `.menustyle` files get no highlighting. Adding
  the grammar is only half of it, and the other half is what actually bit: the
  extension ships as a **committed `.vsix`** that nothing rebuilds, so the
  language was registered in the sources and invisible to every user until the
  package was regenerated (`package-vsix.py`). The same had already happened to
  the VU language one release earlier.
- **`ai-support/` describes the project format to agents** — a new menu-styling
  surface belongs there in the same commit.
