# Menu stylesheets

A game menu's look is a **stylesheet**: a CSS-shaped text file in
`<project>/menu-styles/*.menustyle`, edited with widgets in *Tools > Menu
Editor > Style*, baked into sprites at build. It replaces the four knobs menus
used to have (one accent colour, two font sizes, a panel width) with a box
model, per-state row styling, scrolling lists, descriptions and transitions.

Everything here is **baked on the host**, so the PlayStation 2 pays for pixels
and sprites, never for layout. The editor prints what each menu costs before
you build it.

- The file is the source of truth. The Style tab edits its rules; the
  *Stylesheet* tab is the same file as text, with parse errors listed by line;
  **Open in VS Code** hands it to a real editor (unsaved widget edits are
  written out first, so the two never disagree, and the bundled extension
  colours and completes `.menustyle`).
- A menu with no stylesheet (`style` empty) is the **Classic** look, and it
  bakes byte-for-byte identically to the pre-stylesheet editor. Existing
  projects change nothing until you pick a sheet.
- The menu's own fields — accent, title size, row size, panel width, font — are
  the **base of the cascade**. A sheet that says nothing about a colour keeps
  taking it from there; one that does wins. There is no second source of truth,
  only an order.

## Where you work

Three surfaces, and they all draw the same bake:

- **Tools > Menu Editor** - the menu's content, its *Style* tab, its
  *Stylesheet* tab, and a compact preview above them.
- **Tools > Menu Preview** - the same preview in a window of its own, with a
  zoom, its own display mode and the cost readout under it. It follows the Menu
  Editor's selection and carries a menu picker of its own, so it is useful with
  the editor closed.
- **Layout > Menu Designer** - the desk: the Menu Editor filling the left (Font
  Manager one tab away, because a typeface is the other half of restyling), the
  preview column on the right, logs along the bottom.

## The three layers, and what each costs

Every feature belongs to exactly one layer, and the layer is its price.

| Layer | Covers | Console cost |
|---|---|---|
| **Baked pixels** | gradients, 9-slice frames, rounded corners, drop shadows, outlines, letter spacing, per-element fonts and colours | **nothing** — texture bytes only |
| **Baked state cells** | `:selected` / `:disabled` rows, per-row descriptions, option labels and bars | one extra sprite per visible cell |
| **Sprite properties** | slide, fade, scroll, caret easing | a few floats per frame |

That is why a highlight bar with a gradient and a soft shadow is free, while
"twelve rows each with its own selected look" costs twelve baked cells — and
the *What it costs* readout counts them.

## A sheet

```css
@style "Neon"                    /* the name the editor shows */

:root {
  --accent: #78d1ff;             /* variables, used as var(--accent) */
  --ink:    #eaf6ff;
}

panel {
  width: 512px;                  /* 128 | 256 | 512, or `screen` */
  quant: 4bit;                   /* colour depth it ships at */
  padding: 14px 0 0 0;
  background: linear-gradient(180deg, rgba(6,12,26,0.95), rgba(10,20,42,0.8));
  background-image: url(res/hud/frame.png) 9-slice 24px;
  border: 1px solid var(--accent);
  border-radius: 8px;
  shadow: 0px 3px 10px rgba(0,0,0,0.65);
}

title {
  color: var(--accent);
  letter-spacing: 3px;
  text-transform: uppercase;
  text-shadow: 0px 2px 0px rgba(0,8,24,0.9);
  rule-below: 1px solid rgba(120,209,255,0.35);
}

row              { color: var(--ink); padding: 3px 0 8px 44px; icon-size: 18px; }
row:selected     { color: #ffffff; translate-x: 6px;
                   background: linear-gradient(0deg, rgba(120,209,255,0.42), transparent); }
row:disabled     { color: #4d5a6b; }
row.header       { color: #7d90a8; letter-spacing: 2px; selectable: no; }

list             { rows-visible: 8; }
value            { color: var(--accent); display: bar; bar-size: 90px 8px; }
description      { height: 44px; area: below; wrap: yes; }
hint             { content: "{{cross}} OK   {{triangle}} BACK"; color: #7d90a8; }

@transition open   { 180ms ease-out; fade; translate-y 10px; }
@transition cursor { 110ms ease-out; }

menu#pause { panel { width: 256px; } }   /* this menu only */
```

**Elements**: `panel`, `title`, `list`, `row`, `value`, `description`, `hint`,
`marker`, `image`.
**States**: `:selected`, `:disabled` (rows).
**Classes**: `row.<name>` styles the rows whose *Class* field says `<name>`.
**Scope**: a `menu#<name> { ... }` block styles one menu.
**Cascade**: element < class < state, and a menu-scoped rule beats a general
one. A state rule only ADDS to the normal one.

Five sheets ship with the editor as starting points — **Classic**, **Neon**,
**Blade**, **Parchment**, **Minimal**. Four of them carry motion, and each one
argues for a different amount of it: Neon has the full set (slide, fade, a
breathing highlight, a slow sheen), Blade is faster and harder with one quick
edge of light and no pulse (its plate is already loud), Parchment is slow and
soft, and Minimal allows itself a fade and a caret that drifts — because there
the caret IS the selection. Classic has none, as it should. *Install a copy…* writes one into the
project, where it is yours; *Delete sheet…* removes one the project owns (with a
confirm that names the menus using it — they fall back to Classic). A built-in
is not a file and cannot be deleted, which is what keeps a fresh copy always
available.

### Properties

Box: `width` `height` `padding` `margin` (+ `-top/-right/-bottom/-left`)
`background` `background-image` `slice` `border` `border-color` `border-radius`
`shadow` `opacity` `gap` `quant`.
Text: `font` `font-size` `color` `align` `letter-spacing` `text-transform`
`text-shadow` `text-outline` `rule-below` `content`.
Rows and list: `translate-x` `icon-size` `marker` `marker-side` `selectable`
`rows-visible` `scroll-marker`.
Values: `display` (`text` | `bar`) `bar-size` `bar-fill` `bar-track`.
Description: `height` `area` (`below` | `right`) `wrap`.

Every one of them carries a tooltip in the Style tab, generated from the same
table the parser reads (`menustyle::propSpecs`), so the editor and the parser
cannot disagree about what exists.

## Rows

A row carries four fields beyond its label and action (*Content* tab, under
*Style / description*):

- **Class** — which `row.<class>` rules apply.
- **Icon** — a *UI Editor* button-icon name, drawn in the row's icon column
  (`row { icon-size: 18px }`). A `{{token}}` inside the label still works and
  needs nothing here; this one is for a column that lines up.
- **Description** — shown while the row is selected, in the description pane.
- **Enabled when** — a save value that gates the row: 0 means the row draws in
  its `:disabled` style and the cursor skips it. That is how LOAD GAME greys
  out until there is something to load.

The **Label** action is a row that draws its text and does nothing — a section
header, or a spacer with an empty label. The cursor skips it, including on the
frame a menu opens, so a menu whose first row is a header still lands on
something usable.

Menus hold up to **32 rows**. Past what fits, give `list` a `rows-visible` and
the list **scrolls**: the rows move into their own texture and the game shows a
window of it, so scrolling is an offset change and a 32-row menu costs what an
8-row one does.

## Motion

Nothing here re-bakes anything: every one of these is a sprite property the
console changes per frame - an alpha, a position, a texel offset - so a menu
that never stops moving costs what a still one costs.

**Transitions** react to something:

```css
@transition open   { 180ms ease-out; fade; translate-y 10px; }
@transition close  { 140ms ease-in; fade; }
@transition cursor { 110ms ease-out; }   /* the caret eases between rows */
@transition scroll { 120ms ease-out; }   /* a long list settles into its window */
@transition value  { 180ms; }            /* the row whose value changed flashes */
```

A closing menu keeps drawing until its transition finishes - that is why the
menu index is parked rather than cleared. Only the plain dismissals animate: a
scene switch or a hand-off to the save menu clears immediately, because a panel
lingering over a loading scene is worse than no transition.

**Animations** are loops that never stop:

```css
@animate selected { pulse 1.8s 0.22; }                      /* the highlight breathes */
@animate marker   { bob 0.9s 3px; }                         /* the caret drifts */
@animate panel    { sheen 3.4s 52px rgba(255,255,255,0.18); } /* light sweeps across */
```

The sheen is a soft band drawn additively and **cropped to the panel** as it
enters and leaves (a 2D sprite is clipped by nothing, so without the crop the
sweep is visible beside the menu before it arrives - which is exactly how it was
first reported). Its texture is procedural and shared by every menu that asks
for one.

### Animating what a gradient cannot

Baked pixels never move: a gradient inside the panel is frozen at bake time, and
no amount of style will slide it. What moves is a texture's sampling **window**,
so an animated background is a layer of its own:

```css
panel { background-anim: url(res/hud/stars.png) scroll 12px/s -4px/s; }
panel { background-anim: url(res/hud/flame.png) frames 8 1.2s; }
```

- **scroll** tiles the image and walks the window across it. The bake lays two
  copies along each scrolled axis, so the window travels a full tile and lands
  back where it started without ever sampling past the edge - no wrap mode, the
  same rule the value strip follows.
- **frames** stacks a vertical frame strip and jumps the window between frames.

Either way it is **one sprite and one texture**, drawn under everything the
panel bakes - so give the panel's own background some transparency or the layer
will not show. This is the mechanism to reach for when you want a living
backdrop: a drifting starfield, a slow gradient wash, a flickering torch.

### The selection caret

The blue arrow is an ordinary asset, and there are four ways to change it:

```css
marker { marker: url(res/hud/caret.png) left; }  /* your own image, this sheet */
marker { marker: none; }                          /* no caret at all */
marker { translate-x: 40px; }                     /* move it (32 by default) */
@animate marker { bob 0.9s 3px; }                 /* make it drift */
```

Or replace **`res/hud/save-cursor.png`** (16x16) in the project: the built-in
sprites are written only when missing, so an edited one survives every build -
but note that file is also the SAVE menu's cursor, so it changes both. A sheet's
own `marker: url(...)` changes only the menus that use that sheet, and the
preview draws whichever of the two applies.

A style whose `row:selected` paints a full-width plate usually wants
`marker: none` - the caret would just sit on top of the plate. The three
built-in sheets that paint one already say so.

A sheet **installed into a project is a copy**, so it does not pick up later
changes to the built-in it came from. If a Blade or Neon menu still draws a
caret over its plate, that copy predates `marker: none` - add the line, or
install a fresh copy under another name and switch to it.

## Resolutions

The framebuffer is not the same shape in every scan mode:

| Mode | Buffer |
|---|---|
| 480i / 576i interlaced | 512x448 |
| 480i / 576i field rendering | 512x448 logical (half-height buffers) |
| 480p progressive | 448x448 |
| 1080i | 448x540 |
| 576i full PAL | 512x512 |

Menus are authored in the logical **512x448** space and the runtime scales them
by `(width/512, height/448)`. That keeps a panel the same **physical** size on
the TV: the GS display window maps whatever the buffer is across the same
raster, so 448 columns cover exactly the width 512 do. The two axes therefore
scale by *different* factors, which is why the engine's `Sprite` carries a
per-axis `drawSize` (a single `scale` could not express it, and `size` is the
source rect for the sub-rect sprites).

Nothing is baked twice: one panel serves every mode. The trade is softness — at
1080i the vertical factor is 1.2, so a 4-bit panel scaled up shows its palette
a little. Author the panel for the mode you ship in and the others follow.

*Preferences > Display > Supported resolutions* declares which modes a player
can end up in. It is a declaration the **editor** reads:

- the Menu Editor's *Preview in* list offers exactly those modes, each showing
  the panel at the size the game will draw it, inside that mode's buffer and at
  its physical aspect;
- the preview says so when a panel **runs off the screen** in a mode;
- the scaffolded DISPLAY menu row offers those options.

Menus scale to whatever mode the player picks either way; ticking the boxes is
how the editor knows which ones to check.

## What a menu ships

| File | Contents | When |
|---|---|---|
| `res/menus/<menu>.png` | chrome, title, decorations, hints, and every row in its normal state | always |
| `res/menus/<menu>-rows.png` | one cell per (row, state) the sheet paints | only when a state changes pixels |
| `res/menus/<menu>-values.png` | option labels or bars | only with Toggle / Choice rows |
| `res/menus/<menu>-list.png` | every row, for a scrolling list | only when the list scrolls |
| `res/menus/<menu>-desc.png` | one cell per row with a description | only with a description pane |
| `res/menus/<menu>-bganim.png` | the moving background layer | only with `background-anim` |
| `res/menus/sheen.png` | the swept band, procedural and shared | only when some sheet sweeps one |

A state cell carries the panel's **own background** at that row, which is what
lets it be drawn over the baked normal row and cover it whatever the
highlight's alpha.

`quant: 4bit` is the lever that makes big menus affordable: menu art is flat
colour and text, so palettizing is nearly lossless, and it takes a full-screen
512x512 panel from 93% of the GS texture heap to about 12%
([gs-vram.md](gs-vram.md)). Menu panels were never quantized before; the
default still is not, so nothing changes until you ask.

## What this does not do

The CSS framing invites all of these, and none of them are here:

- no runtime layout or text reflow — a baked string is a build-time snapshot
  (an in-game key-rebind row stays the exception: it draws from a glyph atlas)
- `px` only, no `%` or `em`; texture axes are powers of two, 512 max
- shadows and outlines are baked, so they cannot follow motion - and a baked
  gradient cannot slide (that is what `background-anim` is for: a layer whose
  sampling window moves)
- the layer order is fixed: dim, chrome, rows, values, description, overlay,
  caret
- motion is sprite position / tint / size / texel offset only — never a re-bake,
  which also means per-row entry animations are out: rows live inside the panel
  texture, so staggering them would need a sprite each
- no media queries, no inheritance beyond the cascade above, no functions
  besides `var()` and the colour / gradient forms

Failure is reported, never silent: *panel taller than 512 px*, *too many state
cells for one atlas*, *more rows than a strip holds*, *runs off the screen in
this mode* all appear in the Menu Editor before a build.

## How it fits together

```
menu-styles/*.menustyle ──parse──> menustyle::Sheet
                                        │
GameMenu (accent, sizes, font) ──base───┤
                                        ▼
                            menulayout::compute()  ← THE layout engine
                                        │
              ┌─────────────────────────┼──────────────────────────┐
              ▼                         ▼                          ▼
      menubake (textures)      Menu Editor preview        templates.cpp
      res/menus/*.png          (the same pixels)          menu_data.gen.hpp
                                                                   │
                                                                   ▼
                                                   TerrainGame::renderGameMenu
                                                   (a compositor over the table)
```

Three rules hold it up:

1. **One layout engine.** The baker, the preview and codegen all call
   `menulayout::compute`. Nothing re-derives geometry.
2. **The runtime decides nothing about the look.** `renderGameMenu` walks
   `MenuData` / `MenuEntryData` and draws sprites. A new look is a new sheet,
   never a code change.
3. **The preview is the bake.** `menubake::bakeMenuPreviewRGBA` is the single
   entry point the Menu Editor draws, and it composites exactly what the
   console composites — panel, scroll window, states, values, description.

Source map: `menustyle.{hpp,cpp}` (model + parser + registry),
`menulayout.{hpp,cpp}` (the engine), `menubake.cpp` (rasterizer),
`menustyle_ui.cpp` (Style tab + preview + cost readout), `templates.cpp`
(codegen + the runtime compositor).
