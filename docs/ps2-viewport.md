# PS2 output in the viewport

Two ways of looking at the same scene:

- **Editor** (default) — the viewport's own image. Full monitor resolution, and
  as wide as you docked the panel.
- **PS2 output (GS)** — what the console draws. The scene is rasterized at the
  **GS framebuffer size** of the project's display mode and then shown the way a
  television shows it: fitted into a 4:3 or 16:9 picture, point sampled, with
  everything outside that picture black.

Switch in the **gear in the viewport's bottom-left corner** or *View > Viewport
output*. Machine-global (`editor.ini`, key `viewportPs2`) like the safe areas —
a way of looking, not project data, so it never dirties the `.tyra` and never
travels to a collaboration peer.

## Why

The editor's viewport is a lie in three specific ways, and each one costs real
time later:

1. **Resolution.** A 1600×900 panel has about six times the pixels of a PS2
   frame. Detail that reads cleanly at the desk can be two pixels wide on the
   console; a texture that looks crisp can be a shimmering mess in motion.
2. **Framing.** The panel is whatever shape you docked it to. The console
   outputs one fixed shape, and everything outside it does not exist. The
   [safe-area guides](safe-areas.md) draw that rectangle; this mode *renders*
   into it, so the camera really does frame what the player will see.
3. **Pixel shape.** GS pixels are not square (below).

Everything that costs the console nothing to be honest about is on: the
resolution really is 512×448, the raster really is that coarse, and the picture
really is that shape.

## What changes when it is on

| | Editor | PS2 output |
|---|---|---|
| Rasterized at | panel size | the GS framebuffer of the display mode |
| Projection aspect | panel width / height | the engine's `RendererSettings::aspectRatio` |
| Scaled up with | — | nearest neighbour, into the display window |
| Vertical resolution in `interlaced-field` | full | **halved** — the mode's whole point |
| Flicker filter | — | on for `interlaced` and `pal576`, as on the console |

The editor's own decoration (grid, selection outlines, markers, the light
gizmos) is rasterized with the scene, so it goes chunky too. The transform
gizmo, the axis gizmo, the measuring tape and the safe-area guides are drawn by
the UI *over* the image and stay sharp — and they stay correctly placed:
picking, both raycasts and the gizmo all travel through the same letterbox the
picture does.

### The GS geometry per display mode

The host twin of Tyra's `RendererSettings::updateGeometry` (see
`App::ps2ViewportOutput`):

| *Preferences > Display mode* | GS framebuffer | Notes |
|---|---|---|
| `interlaced` | 512×448 | the stock signal; flicker filter on |
| `interlaced-field` | 512×**224** | true field rendering — half the lines |
| `progressive` (480p) | 448×448 | |
| `1080i` | 448×540 | 4:3 is pillarboxed inside the 16:9 raster |
| `pal576` | 512×512 | the full-height PAL frame |

*Widescreen* widens the projection and the picture; it does not change the
framebuffer.

**One thing the editor cannot know**: with `videoSystem: auto`, whether
*PAL picture* (`palFullHeight`) promotes `interlaced` to the 512-line frame
depends on the region of the console that boots the disc. The viewport assumes
NTSC there — the shorter picture — so what it shows is what *every* console
renders at least. Force the video system to `pal` to author the taller frame;
the safe-area overlay's *NTSC picture inside PAL* guide covers the difference
from the other side.

## GS pixels are not square, and they are not square by 16.7%

Tyra builds its projection with `aspectRatio = 512/448 = 1.143` for a 4:3
display window (`renderer_settings.hpp` keeps the stock value as its 4:3
baseline and scales it with the shape of the mode's window). The television then
shows those 512×448 pixels as a 4:3 picture — 1.333 wide.

So the console's picture is **horizontally stretched by 1.333 / 1.143 = 1.167**.
A sphere is a circle in the GS framebuffer and an ellipse on the TV. Measured on
a centred sphere: 0.99 wide/high in the editor mode, **1.18 in PS2 output**.

This is the engine's own convention, not something the viewport introduces —
until now the editor simply did not show it. Author against it: circular things
authored to look circular in the editor will read slightly wide on the console.

## What it does not simulate

Deliberately, because the engine does not do these things and inventing them
would trade one lie for another:

- **16-bit dithering.** The GS framebuffer here is `GS_PSM_32` — the engine
  renders in full 32-bit colour, so there is no 4×4 dither pattern to show.
- **Texture palettization.** *Preferences > Texture quantization* palettizes
  textures at build (4-bit by default), and the viewport still samples the
  full-colour source. The Material Editor's **PS2 CLUT** display mode answers
  that question per material today; bringing it to the whole scene is a separate
  job.
- **Interlace flicker.** The flicker *filter* (the vertical blend the console
  applies) is reproduced. The temporal flicker between fields is not: the editor
  does not run at the console's field rate, so a simulated strobe would be a
  different artifact wearing the same name. Field rendering's real cost — half
  the vertical resolution — is shown, and that is the part you author against.
- **Texture filtering** needs no simulation: the engine draws bilinear with
  mipmapping off (`max_level = 0`), which is what the viewport already did. The
  distance shimmer that produces appears on its own once the scene is rasterized
  at 512×448.

## Notes

- The [phone-camera](phone-camera.md) viewfinder streams the presented image, so
  the phone sees the console's picture too, bars and all.
- Nothing about this reaches code generation. It is a viewing mode; the game is
  identical either way.
