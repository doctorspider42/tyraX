# Loading screens

A **loading screen** is what the game shows while a scene loads — at boot (the
[start scene](#the-start-scene)) and on every *Switch Scene*. The editor lets you define several
named loading screens, each with a background color, image and text elements,
and **progress bars**, then assign one per scene or set a project default.

Open the editor from **Tools > Loading Screens** (or the button in *Project >
Preferences*). The master on/off switch is *Project > Preferences > "Loading
screen between scenes"* — with it off, scenes cut straight in with no screen.

## Anatomy of a screen

Each screen is drawn back-to-front as:

1. **Background color** — the whole screen is cleared to it.
2. **Images** — PNGs imported into `res/hud/` (same pipeline as the HUD:
   positioned by a normalized center anchor, sized in pixels on the 512×448
   screen, baked to a PS2-valid power-of-two texture). Use them for a logo or
   artwork.
3. **Texts** — named strings baked to sprites at build (the PS2 engine has no
   runtime font — any TTF, size, color, drop shadow). Use them for a "LOADING"
   caption or a tip line.
4. **Progress bars** — see below.

Elements are listed in the middle column; select one to edit it on the right.
The **preview** at the bottom renders the screen at 512×448 aspect, with a
*Preview progress* slider so you can see the bars at any fraction without
running the game.

## Progress bars

Two kinds, both authored as colored rectangles (no texture needed):

- **Continuous** — a track (the *off* color) with a fill (the *on* color) that
  grows left-to-right with load progress.
- **Quantized** — `N` segments (2–16) with a configurable gap; segments light
  up one at a time, one per `1/N` of progress (e.g. 5 dots that fill in steps).
  A quantized bar can optionally use a **segment PNG** instead of a plain rect:
  the same texture is drawn per segment, tinted with the *on* color when lit and
  the *off* color when unlit (color modulation of one image).

Position (normalized, center anchor), total size (px), and the on/off colors
are per-bar.

### The bar shows *real* progress

The load is otherwise a single blocking step. To make the bar mean something,
the generated game counts the work a scene load does — assets to stream
(textures/materials/models), objects to build, and terrain chunks in view — and
presents a loading-screen frame every ~1/24 of the way through. So the bar
advances as the scene actually loads, not on a timer. (Very small scenes load
so fast the bar may jump straight to full; give a heavy scene a big terrain or
many assets to see it climb.)

### At boot

The very first scene is covered too. Boot order is: the engine's **Tyra logo**
(held ~2 seconds) → any **boot splash screens** (below) → the loading screen
while the **start scene** loads → the scene. The first load is deferred into the game loop
on purpose — a frame presented from `init()`, before the main loop, isn't
vsync-paced and would flash by, so the boot loading screen would be invisible.
(The ~2s logo hold lives in the engine's `banner.show()`, which re-renders the
splash for a fixed real-time window; it applies to every generated game,
loading screens or not.)

## Boot splash screens

The **Boot splash screens** section (collapsing header at the top of the
Loading Screens window) lists images shown at startup, in order, **after the
Tyra logo and before the loading screen**. Use them for a studio / publisher /
"presents" card. Each splash is:

- an **image** (PNG, imported into `res/hud/`, baked to a PS2-valid size like
  any HUD image; full-screen 512×448 by default, position/size adjustable),
- a **background color** behind it (for letterboxing a non-full-screen image),
- a **duration** in seconds (0.1–10).

Splashes are project-wide and **independent of the loading-screen master
toggle** — they always play when defined. Only images are supported for now.
Reorder them with *Move up* / *Move down*; the list order is the play order.
They compile to a `SPLASHES[]` table in `inc/loading_data.gen.hpp` and, like the
Tyra logo hold and the loading screen, are rendered from the game loop so each
is shown for its full (vsync-paced) duration.

## The start scene

Which scene the game boots into is a project setting: **Scene > Preferences >
Startup > "Boot into this scene"**, ticked on the scene you want. New projects
boot the first scene, so nothing changes until you move it, and the *Project*
panel marks whichever scene it is with **(start)** next to its name.

The tick can only be *moved*, never cleared — some scene always has to be the
one the game starts in, so the checkbox is disabled on the scene that already
holds it and you set a different scene instead. Scene Preferences always edits
the **active** scene, so switch to the scene first.

It reaches the game as `START_SCENE` in `inc/scene_data.hpp`, which is the only
argument the boot `loadScene()` takes — so a wrong value cannot desynchronize
anything, and everything else about a scene (its ambience, its cycle, its
loading screen) is unaffected by *being* the start scene.

Two things follow it automatically: deleting a scene shifts the index so it
still points at the same scene (deleting the start scene itself falls back to
the first one), and a value that somehow ends up out of range — a hand-edited
`.tyra`, a collaboration peer with a different scene list — is folded back to
the first scene on load rather than booting the game into nothing.

## Assigning a screen

- **Per scene** — *Scene > Preferences > Loading screen*. Pick a screen by name,
  or leave it on `<default>` to inherit the project default.
- **Project default** — mark a screen "Default at game start" in the Loading
  Screens editor. Scenes that don't name their own use it (including the start
  scene at boot).
- **Built-in fallback** — with no screens defined (or a scene set to the default
  when there is no default), the game shows the classic centered
  `res/hud/loading.png` on black, exactly as before this feature existed. The
  placeholder PNG is generated when missing; replace the file to customize it.

## How it reaches the game

The screens compile to `inc/loading_data.gen.hpp` (element tables + a
scene→screen map). At runtime `loadingscreen::renderFrame` draws the resolved
screen; progress bars are tinted quads over a shared white 8×8 sprite
(`res/hud/loading-white.png`, shipped automatically). Loading images and segment
textures are baked (power-of-two + quantized) like HUD images; loading texts are
baked to `res/hud/text-ls-<screen>-<name>.png`. Everything is bundled into the
disc's startup group on ISO export.

Loading screens are project-wide data (like Ambience presets and Color Grading),
so edits save immediately and are not part of undo; the per-scene *assignment*
is undoable with the rest of the scene.
