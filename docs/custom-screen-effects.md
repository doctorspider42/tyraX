# Custom screen effects

The editor ships two built-in full-screen post effects — **bloom** and **film
grain** (Tools ▸ UI Editor). When you want an effect the editor doesn't have,
you can define your own — **per project, no editor rebuild** — as a `.screenfx`
text file in the project's `screen-effects/` folder. It appears in the UI Editor
screen stack, where you position it (under the HUD, over everything, …) and set
its parameters, exactly like the built-in effects.

There are no pixel shaders on the PS2: a screen effect is a handful of **GS
framebuffer blits** at the end of the frame — the same technique bloom and film
grain use. So a custom effect is written **low-level**, in raw C++ that appends
GS primitives. The editor wraps the fiddly frame-state setup/teardown for you;
your code only draws.

This is the screen-effect analogue of [custom flow-graph
nodes](custom-flow-nodes.md): a self-contained file next to the project, so
moving an effect elsewhere is a copy.

## Quick start

1. Open **Tools ▸ UI Editor ▸ Screen effects ▸ Custom effects… ▸ New starter
   effect**. This writes `screen-effects/example.screenfx` (a working "Color
   Wash" effect) and loads it.
2. It appears under **Screen effects** with a **+ Add** button. Click it to drop
   the effect into the screen stack, then drag it where it should composite.
3. Select it in the stack to set its parameters. Build (F5) — your code lands in
   `src/scripts/screen_fx.gen.cpp` and runs at the effect's stack slot.
4. Edit the file (format below), then **Custom effects… ▸ Reload from folder** to
   pick up manifest changes.

## File format

A `.screenfx` file is a `key = value` header, a line that is exactly `---`, then
the raw C++ effect body:

```
# Lines before --- starting with # are comments.
title = Color Wash
param0 = Amount, 0.35, 0, 1
param1 = Red, 0.1, 0, 1
param2 = Green, 0.0, 0, 1
param3 = Blue, 0.2, 0, 1
---
u8 a = (u8)({p0} * 128.0F);
if (a == 0) return q;
u8 r = (u8)({p1} * 255.0F), g = (u8)({p2} * 255.0F), b = (u8)({p3} * 255.0F);
q = fx.flatQuad(q, fx.currentFbVram(), fx.currentFbBufW(), 0xFF000000u,
                r, g, b, a, GS_SET_ALPHA(0, 1, 0, 1, 0));
```

### Header keys

| Key           | Meaning                                                    | Default       |
|---------------|------------------------------------------------------------|---------------|
| `title`       | Display name in the UI Editor stack                        | the file name |
| `param0`…`param3` | A slider: `Label` or `Label, default, min, max` (default 0, range 0..1). Define them in order from `param0`. | *(no params)* |

The file **name** (without `.screenfx`) is the effect's identity —
`vignette.screenfx` → `custom:vignette`. Keep it identical when copying the
effect to another project, or its placement won't resolve.

### The effect body

Everything after `---` is emitted **verbatim** into `screen_fx.gen.cpp`, inside a
function that runs your effect. These are in scope:

| Symbol   | What it is |
|----------|------------|
| `fx`     | the `Tyra::RendererCorePostFx` — the drawing API (below) |
| `q`      | the `qword_t*` GS packet cursor — **append primitives and advance it**, then `return q;` (the generated function already ends with `return q;`, but returning early like the example is fine) |
| `param`  | `const float[4]` — the placement's parameter values |
| `{p0}`…`{p3}` | expand to `param[0]`…`param[3]` (the same values) |

The GS packing macros (`PACK_GIFTAG`, `GS_SET_*`, `GS_REG_*`, `GS_PSM_*`,
`GS_SET_ALPHA`, …) and PS2 types (`u8`, `u32`, `qword_t`) are already included.

> **Packet budget:** the effect runs in a 224-qword GS packet; after the
> engine's setup that leaves room for roughly **18 blits or 33 flat quads**. A
> screen effect that needs more than that is doing too much for one frame on the
> PS2.

## The drawing API (`fx`)

`fx` is the engine's `RendererCorePostFx`. The two primitives the built-in
effects use are public:

```cpp
// Untextured full-screen sprite: flat RGBA color blended over the frame.
// fbmsk protects framebuffer bits from the write (use 0xFF000000u to keep the
// framebuffer alpha byte, which the GS reads back). alpha = a GS_SET_ALPHA(...)
// blend equation. Returns the advanced cursor.
qword_t* fx.flatQuad(q, dstVram, dstBufW, fbmsk, r, g, b, a, alpha);

// Textured sprite: src rect (UV in 1/16 texel) -> dst rect (pixels).
// linear = bilinear filter, wrap = repeat vs region-clamp, abe = enable blend.
qword_t* fx.blit(q, srcVram, srcBufW, texW, texH, u0,v0,u1,v1,
                 dstVram, dstBufW, x0,y0,x1,y1, linear, wrap, abe, alpha);
```

Accessors for what those calls need:

| Call | Returns |
|------|---------|
| `fx.currentFbVram()`, `fx.currentFbBufW()` | the framebuffer being composited (word address, 64-aligned buffer width) |
| `fx.screenW()`, `fx.screenH()` | screen size in pixels |
| `fx.noiseTexVram()`, `fx.noiseTexSize()` | the shared animated-noise texture (64², filmic dark-skewed) — for grain-like effects |
| `fx.lowBuf0()`, `fx.lowBuf1()`, `fx.lowResW()`, `fx.lowResH()`, `fx.lowResBufW()` | two quarter-res scratch buffers (bloom's working buffers) — free to reuse within your callback (e.g. downsample the frame, blur, add it back) |
| `fx.nextRand()` | advance & return the shared PRNG (per-frame animated offsets, like the grain scroll) |

### GS blend cheat-sheet

The GS blend unit computes `(A − B) × C >> 7 + D`, where each of A/B/D is
`Cs` (source), `Cd` (destination framebuffer) or `0`, and C is `As`, `Ad` or a
`FIX` constant. `GS_SET_ALPHA(a, b, c, d, fix)` selects them (0=source,
1=dest, 2=zero for a/b/d; 0=As, 1=Ad, 2=fix for c). Common recipes, all as
`fx.flatQuad(q, fbVram, fbBufW, 0xFF000000u, r, g, b, alphaByte, <ALPHA>)`:

| Effect | `GS_SET_ALPHA(...)` | Notes |
|--------|---------------------|-------|
| **Mix toward a color** (fade / tint) | `GS_SET_ALPHA(0, 1, 0, 1, 0)` | `(Cs − Cd)·As>>7 + Cd`, `alphaByte` = strength 0..128 |
| **Add** (brighten) | `GS_SET_ALPHA(0, 2, 2, 1, 0)` | `Cd + Cs·As>>7` |
| **Subtract** (darken) | `GS_SET_ALPHA(2, 0, 2, 1, 0)` | `Cd − Cs·As>>7` |

For textured tricks (blur, feedback, grain) study `RendererCorePostFx::apply()`
in `vendor/tyra/engine/src/renderer/core/postfx/renderer_core_postfx.cpp` — it is
the reference for how bloom and grain drive `blit()`.

## Positioning in the screen stack

The **UI Editor screen stack** is the frame's draw order: the 3D scene at the
bottom, then HUD sprites, texts and the effect layers. An effect placed at a
slot composites **right before** the HUD sprite at that slot — so sprites above
it draw crisp on top, sprites below get the effect. At the top of the stack
(the default) the effect draws over the whole HUD, under the USE prompt / texts /
pause menus. Drag the entry to move it; uncheck **Enabled** to keep a placement
without compositing (or generating) it.

Parameters are **per project** (baked into the generated code at build), not
per scene, and not runtime-controllable from the flow graph yet — see
[Limitations](#limitations).

## No editor preview

Screen effects are GS operations that only exist on the PS2 / in PCSX2, so the
editor viewport does **not** preview them (the same is true of bloom). The stack
entry is only for positioning; build the game to see the effect.

## Moving effects to another project

An effect's **identity is its file name** (`vignette.screenfx` →
`custom:vignette`), and placements reference it by that identity. To reuse it:

1. **Copy the `.screenfx` file** into the other project's `screen-effects/`
   folder (keep the file name identical), then **Reload from folder** (or reopen
   the project).
2. Add it to that project's stack and set its params.

> ⚠️ **The file must travel with the project.** When the editor loads a project,
> a screen-effect placement whose `.screenfx` file is missing is **dropped** (the
> same way unknown flow-graph nodes are cleaned up). So if you copy a `.tyra`
> that uses `custom:vignette` but forget the file, that placement silently
> disappears on load and is gone on the next save. Copy the file *first*. This is
> why effects live in plain files next to the project rather than baked into the
> `.tyra`.

## Limitations

These are follow-ups, not built yet:

- **No per-scene parameter overrides.** Params are project-wide (bloom/grain can
  be overridden per scene; custom effects can't yet).
- **No flow-graph runtime control.** There's no "Set <effect> param" node
  (bloom/grain have Set Bloom / Set Grain). Params are fixed at build time.
- **No custom VRAM / uploaded textures.** An effect can reuse the shared noise
  texture and the two quarter-res scratch buffers, but can't allocate its own
  VRAM buffer or upload its own texture.

## Reference

- Folder: `<project>/screen-effects/`, scanned for `*.screenfx` in alphabetical
  order (the UI list order).
- Loaded on project open and on **Custom effects… ▸ Reload from folder**.
- **Custom effects… ▸ Jump to effect file** opens a specific `.screenfx` in
  VS Code (whole-project context, so the engine headers resolve for the body).
  The Tyra VS Code extension gives `.screenfx` files syntax highlighting,
  snippets and validation — see [the VS Code extension](vscode-extension.md).
- Implementation: `src/screenfx.cpp` (loader/parser), `src/screenfx.hpp`
  (registry + `CustomScreenFx`), `ScreenFxPlacement` in `src/project.hpp`
  (the project-side placement), code generation in `screenFxSource()` /
  `screenFxHeader()` (`src/templates.cpp`), and the engine surface
  `RendererCore::applyCustomPostFx` / `RendererCorePostFx::applyCustom`
  (`vendor/tyra/engine`).
