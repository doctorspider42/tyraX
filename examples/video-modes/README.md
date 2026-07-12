# video-modes — display mode / widescreen test bed

A minimal project for testing the **video output features**: the three scan
modes (interlaced 480i/576i, progressive 480p, 1080i), the **16:9 widescreen**
switch, and the **runtime mode switching with the keep-or-revert prompt**.

Open `video-modes.tyra` in the editor and Build & Run (F5), or headless:

```powershell
build\tyra-editor.exe --build examples\video-modes --run
```

## Controls (shown on the in-game overlay)

| Button | Action |
|---|---|
| Square | Switch to interlaced 480i/576i |
| Triangle | Switch to progressive 480p |
| Circle | Switch to 1080i |
| L1 / R1 | Widescreen off (4:3) / on (16:9) |
| X | Confirm ("keep") a switched mode |

Every scan-mode switch is armed with an **8-second confirm window**: the game
shows `KEEP VIDEO MODE? X = YES / BACK IN n` and reverts to the previous mode
automatically if X is not pressed in time — so a mode your TV can't display
never leaves you on a black screen. The buttons are wired in the flow graph
on the `aspect-ball` object (Set Display Mode / Set Widescreen nodes); set
"Confirm s" to 0 there to switch blind.

## What to look at

- The **white sphere** in the middle is the aspect-ratio judge: it must stay
  (roughly) round on a real TV in every mode, in 4:3 **and** in 16:9 — the
  projection re-derives from each mode's display window. On a 4:3 monitor /
  PCSX2's default 4:3 presentation the widescreen picture correctly looks
  squeezed (anamorphic).
- The **colored pillars** (N red, S blue, W yellow, E green) show how much
  world fits horizontally — widescreen brings more of them into view.
- After every switch the whole VRAM layout is rebuilt and textures re-upload;
  the checkerboard terrain and the overlay must come back intact.

## Real hardware notes

- 480p and 1080i output **only over component (YPbPr) cables**; on RGB/composite
  the screen goes dark until the auto-revert kicks in (that is the prompt's
  whole point). PCSX2 displays every mode regardless.
- The interlaced mode follows Preferences > Build > Target system (PAL/NTSC/
  auto); the DTV modes always run at 60 Hz.
