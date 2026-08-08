# video-modes — display mode / widescreen test bed

A minimal project for testing the **video output features**: the four scan
modes (interlaced 480i/576i, interlaced with **true field rendering**,
progressive 480p, 1080i), the **16:9 widescreen** switch, and the **runtime
mode switching with the keep-or-revert prompt** — all driven from a proper
in-game menu.

Open `video-modes.tyra` in the editor and Build & Run (F5), or headless:

```powershell
build\tyrax-editor.exe --build examples\video-modes --run
```

It doubles as the test bed for **menus at every resolution**: the menu is
styled with a [stylesheet](../../docs/menu-styles.md)
(`menu-styles/neon.menustyle` — the built-in *Neon* sheet, installed into the
project), and the project declares its **supported resolutions**, so the Menu
Editor's *Preview in* list offers each of them and shows the panel at the size
the game really draws it. Switch modes in-game and the panel keeps the same
physical size and position on the TV — the framebuffer is 512x448 interlaced but
448x540 in 1080i, and the runtime scales the menu by both factors.

**STANDARD 4:3 / WIDESCREEN 16:9 is the same test one axis over**, and the most
useful thing this project shows: 16:9 on the PS2 is *anamorphic* — the
framebuffer does not change and the TV stretches it — so the panel is squeezed
horizontally to exactly cancel that (see
[menu-styles.md](../../docs/menu-styles.md) "Widescreen"). Toggle it with the
menu open and watch: the panel narrows to three quarters of its width **in the
framebuffer**, which on a 16:9 set is the same physical panel it was before,
with the same letter shapes. Measured on the console at 512x448: 575 px wide in
4:3, 431 px in 16:9 (0.7496 against the 0.75 the arithmetic asks for), the same
height and the same centre. In a PCSX2 window left at 4:3 both the world and the
panel look horizontally squeezed — that is the anamorphic signal being shown
un-stretched, not a bug. The Menu Editor's **Aspect** control previews both
without touching the project.

## The VIDEO OPTIONS menu

The menu opens **automatically at boot** (title screen) and any time later
with **Start** (it is the project's pause menu). Navigate with the d-pad,
select with **X**, back out with **Triangle**:

| Entry | Action |
|---|---|
| INTERLACED 480I | Switch to the stock interlaced mode |
| PROGRESSIVE 480P | Switch to progressive 480p |
| HD 1080I | Switch to 1080i |
| 480I FIELD RENDER | Switch to interlaced **field rendering** (a fresh half-height 512x224 image every field — 50/60 distinct pictures per second for ~half the fill/VRAM) |
| STANDARD 4:3 / WIDESCREEN 16:9 | Aspect-ratio switch (applies instantly) |
| CLOSE | Dismiss the menu |

The highlighted row draws from a **baked state cell** (the gradient plate, the
white label and the 6 px step-out come from `row:selected` in the sheet), and
the caret eases onto it — both free on the console: one extra sprite for the
selected row, a float for the caret.

Picking a scan mode **closes the menu and arms an 8-second confirm window**:
the game shows `KEEP VIDEO MODE? X = YES / BACK IN n` and reverts to the
previous mode automatically if X is not pressed in time — so a mode your TV
can't display never leaves you on a black screen. The menu entries fire flow
events consumed by the graph on the `aspect-ball` object (On Menu Event →
Set Display Mode / Set Widescreen); set "Confirm s" to 0 there to switch
blind.

## What to look at

- The **white sphere** in the middle is the aspect-ratio judge: it must stay
  (roughly) round on a real TV in every mode, in 4:3 **and** in 16:9 — the
  projection re-derives from each mode's display window. On a 4:3 monitor /
  PCSX2's default 4:3 presentation the widescreen picture correctly looks
  squeezed (anamorphic).
- The **colored pillars** (N red, S blue, W yellow, E green) show how much
  world fits horizontally — widescreen brings more of them into view.
- After every switch the whole VRAM layout is rebuilt and textures re-upload;
  the checkerboard terrain, the menu panel and the overlay must come back
  intact.

## Real hardware notes

- 480p and 1080i output **only over component (YPbPr) cables**; on RGB/composite
  the screen goes dark until the auto-revert kicks in (that is the prompt's
  whole point). PCSX2 displays every mode regardless.
- The interlaced modes (stock and field rendering) follow Preferences > Build >
  Target system (PAL/NTSC/auto); the DTV modes always run at 60 Hz.
- Field rendering outputs the same 480i/576i signal as the stock mode, so it
  works on any cable/TV; expect a slightly softer static picture (each field
  is a half-height image) and judge the motion smoothness on a real CRT —
  PCSX2's deinterlacing hides most of the difference.
