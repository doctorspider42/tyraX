# Loading screens

A **loading screen** is what the game shows while a scene loads — at boot (the
first scene) and on every *Switch Scene*. The editor lets you define several
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

## Assigning a screen

- **Per scene** — *Scene > Preferences > Loading screen*. Pick a screen by name,
  or leave it on `<default>` to inherit the project default.
- **Project default** — mark a screen "Default at game start" in the Loading
  Screens editor. Scenes that don't name their own use it (including scene 0 at
  boot).
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
