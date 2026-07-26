# TV safe areas

Guides in the viewport for framing something a real television will not crop.
They live behind the **gear in the viewport's bottom-left corner** so they cost no
screen space until wanted, and the settings are machine-global (`editor.ini`) like
the axis gizmo — a viewing aid, not project data.

## Why they exist

A CRT does not show the whole picture. The tube is scanned past the edges of the
visible glass ("overscan"), and how much varies per set, per region and with age.
The broadcast convention that outlived the technology is two insets:

| Guide | Inset | What belongs inside |
|---|---|---|
| **Action safe** | 90% (5% per edge) | Anything the player must see — a character, a pickup, the crosshair |
| **Title safe** | 80% (10% per edge) | Text and HUD readouts |

PAL and NTSC share those fractions. What differs between them is the **picture
height**, and only in one case this project can produce — see below.

## What the overlay draws

- **Picture frame** — the rectangle the console actually outputs, with everything
  outside it dimmed. The viewport is whatever shape you docked it to; the TV is
  not, so this is the first thing worth seeing. Labelled `4:3` or `16:9`.
- **Action safe (90%)** in amber, **title safe (80%)** in green, both concentric
  with the picture.
- **Centre + thirds** — a centre cross and a rule-of-thirds grid, for composition
  rather than safety.
- **Aspect** — *Follow project* reads *Preferences > Widescreen*; forcing 4:3 or
  16:9 checks how the same shot frames in the other case without touching the
  project.
- **Opacity**.

## PAL vs NTSC

The two regions only show different amounts of picture when the project boots the
**full-height PAL frame**: `Preferences > PAL picture` (`palFullHeight`) with the
region-following `interlaced` display mode. Then a PAL console renders **512
lines** where NTSC renders **448**, so Europe sees more at the top and bottom of
the *same* scene.

**NTSC picture inside PAL** draws that inner rectangle (87.5% of the height, the
missing 56 lines split top and bottom) so you can check a shot works in both. The
option is disabled otherwise, because with any fixed display mode both regions get
the identical letterboxed picture and a second rectangle would be a lie.

## The guides draw the picture; the PS2 output mode renders into it

The same gear also switches the viewport to
[PS2 output](ps2-viewport.md), which rasterizes the scene at the GS framebuffer
size and fits it into **the same rectangle** these guides outline. With it on the
frame is not a guide over a wider image any more — it is the edge of the picture,
and the camera frames what the player will actually see. The two fits are kept
identical on purpose (`App::drawSafeAreaOverlay` and `Viewport::ps2LetterBox`),
so the guides keep landing on the picture either way.

## What it does not cover

The overlay is drawn **over the viewport image by the editor's UI**, so it does
**not** appear in the phone-camera stream — that is a readback of the rendered
frame, taken before any UI is composited (see [phone-camera.md](phone-camera.md)).
Compose to the guides at the desk; the phone shows the clean picture.
