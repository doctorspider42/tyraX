# Loading screens

A **loading screen** is what the game shows while a scene loads — at boot (the
[start scene](#the-start-scene)) and on every *Switch Scene*. You define named
screens, each with a background color, images, texts and **progress bars**,
then assign one per scene or set a project default.

![The Loading Screens window: the boot sequence strip (Tyra logo, + Splash image, the loading screen, the start scene), the screen list with its "Used by" line and back-to-front elements, the preview with the selected progress bar at 65 percent, and the bar's inspector.](img/loading-screen-editor.png)

Open it from **Tools > Loading Screens** (or the button in *Project >
Preferences*). **Show between scenes** at the top right is the master switch
(the same setting as *Project > Preferences > "Loading screen between
scenes"*) — off, scenes cut straight in.

The window answers "what does the player see from power-on?" in one strip
across the top, the **Boot sequence**: *Tyra logo* (2 s, the engine's) → your
splash cards (or **+ Splash image**) → the loading screen the start scene
shows ("while Aster loads · real progress", *Built-in* or *No loading screen*)
→ the start scene. Click a splash card to edit it, drag it sideways to
reorder; click the loading-screen card to select that screen.

Below it, three columns: **Screens** (each with *default* when it is the
project default, and under the selected one a **Used by** line - the scenes
that pick it and those that inherit it as the default; an unused screen says
*Not shown anywhere* with a *Make default* link) and the selected screen's
elements **back to front** with one **+ Add** (image, text, progress bar);
the **preview** in the middle; the **inspector** on the right. The first
screen you create becomes the default, so it is shown straight away.

## Anatomy of a screen

Drawn back-to-front:

1. **Background color** — the whole screen is cleared to it.
2. **Images** — PNGs imported into `res/hud/` (same pipeline as the HUD:
   normalized center anchor, sized in pixels on the 512×448 screen, baked to a
   PS2-valid power-of-two texture). For a logo or artwork.
3. **Texts** — named strings baked to sprites at build (the PS2 engine has no
   runtime font — any TTF, size, color, drop shadow). For a "LOADING" caption
   or a tip line.
4. **Progress bars** — see below.

Select an element in the list or click it in the **preview** (512×448):
drag it to move it, drag a corner handle to resize an image or a bar about its
centre. Under the preview, **Load progress** scrubs the bars to any fraction,
and **Play load** simulates a load - the bar steps the way the game presents
it (a frame every ~1/24 of the work, unevenly paced). *Background* in the
list selects the screen itself: name, background colour, *Default at game
start*, and the **Scenes** that name it (one switch per scene - the same field
as *Scene > Preferences > Loading screen*), plus Duplicate / Delete.

## Progress bars

Two kinds, both colored rectangles (no texture needed):

- **Continuous** — a track (the *off* color) with a fill (the *on* color) that
  grows left-to-right with load progress.
- **Quantized** — `N` segments (2–16) with a configurable gap, lighting up one
  per `1/N` of progress (e.g. 5 dots that fill in steps). A quantized bar can
  swap the plain rect for a **segment PNG**: the same texture drawn per
  segment, tinted *on* when lit and *off* when unlit.

Position (normalized, center anchor), total size (px) and the on/off colors are
per-bar.

### The bar shows *real* progress

The load is otherwise a single blocking step, so the generated game counts the
work — assets to stream (textures/materials/models), objects to build, terrain
chunks in view — and presents a loading-screen frame every ~1/24 of the way
through. The bar advances as the scene actually loads, not on a timer. (Very
small scenes may jump straight to full; a big terrain or many assets make it
climb.)

### At boot

The very first scene is covered too. Boot order: the engine's **Tyra logo**
(held ~2 seconds) → any **boot splash screens** (below) → the loading screen
while the **chosen boot scene** loads → the scene. The first load is deferred into
the game loop on purpose — a frame presented from `init()`, before the main
loop, isn't vsync-paced and would flash by, leaving the boot loading screen
invisible. (The ~2s logo hold is the engine's `banner.show()` re-rendering the
splash for a fixed real-time window; it applies to every generated game,
loading screens or not.)

## Boot splash screens

The splash cards of the **Boot sequence** strip are images shown at startup,
in order, **after the Tyra logo and before the loading
screen** — a studio / publisher / "presents" card. Each splash is:

- an **image** (PNG, imported into `res/hud/`, baked to a PS2-valid size like
  any HUD image; full-screen 512×448 by default, position/size adjustable),
- a **background color** behind it (letterboxing for a non-full-screen image),
- a **duration** in seconds (0.1–10).

Splashes are project-wide and **independent of the loading-screen master
toggle** — they always play when defined. Only images for now. Drag a card
sideways to reorder; strip order is play order. They compile to a
`SPLASHES[]` table in `inc/loading_data.gen.hpp` and render from the game loop
like everything above, so each shows for its full (vsync-paced) duration.

## The start scene

The packaged game's default boot scene is a project setting: **Scene > Preferences >
Startup > "Boot into this scene"**, ticked on the scene you want. New projects
boot the first scene, and the *Project* panel marks the start scene with
**(start)**.

The tick can only be *moved*, never cleared — some scene always has to be the
start, so the checkbox is disabled on the scene that holds it and you tick a
different one instead. Scene Preferences edits the **active** scene, so switch
to the scene first.

It reaches the game as `START_SCENE` in `inc/scene_data.hpp`. **Play** in the
editor, including Play without build, boots the currently selected scene for
that run in PCSX2 or on PS2. The runner supplies a one-shot `bin/launch.scene`
marker; it does not change the saved default. CLI launches and exported games
use the project default.

It is also kept honest automatically: deleting a scene shifts the index so it
still points at the same scene (deleting the start scene falls back to the
first), and an out-of-range value — a hand-edited `.tyra`, a collaboration peer
with a different scene list — folds back to the first scene on load rather than
booting into nothing.

The player uses the chosen scene's spawn point and that scene's loading screen.

## Assigning a screen

- **Per scene** — *Scene > Preferences > Loading screen*: a screen by name, or
  `<default>` to inherit the project default.
- **From the Loading Screens window** — select the screen's *Background* row
  and switch a scene on under *Scenes*.
- **Project default** — mark a screen "Default at game start" in the Loading
  Screens editor. Scenes that don't name their own use it (including the start
  scene at boot).
- **Built-in fallback** — with no screens defined (or a scene on the default
  when there is none), the game shows the classic centered
  `res/hud/loading.png` on black, exactly as before this feature existed. The
  placeholder PNG is generated when missing; replace the file to customize it.

## How it reaches the game

The screens compile to `inc/loading_data.gen.hpp` (element tables + a
scene→screen map). At runtime `loadingscreen::renderFrame` draws the resolved
screen; progress bars are tinted quads over a shared white 8×8 sprite
(`res/hud/loading-white.png`, shipped automatically). Loading images and
segment textures are baked (power-of-two + quantized) like HUD images; loading
texts bake to `res/hud/text-ls-<screen>-<name>.png`. Everything joins the
disc's startup group on ISO export.

Loading screens are project-wide data (like Ambience presets and Color
Grading), so edits here are not part of undo — but they are ordinary unsaved
edits: the top bar counts them (**N unsaved**) and Ctrl+S writes them. The per-scene
*assignment* is undoable with the rest of the scene.
