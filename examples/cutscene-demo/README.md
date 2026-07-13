# cutscene-demo example

A 14-second in-engine cutscene ("The Reveal") that exercises the full range of
the **Cutscene Director** (*Tools > Cutscene Director*): five camera entities,
hard cuts *and* a smooth blend between cameras, a dolly and a crane (two
cameras moving on their own tracks), per-shot FOV and handheld shake, object
tracks animating position/rotation and scale/colour and visibility, Cinema
2.39:1 widescreen bars that slide in/out, and a fade-in/out — all running on
the PS2.

![The Cutscene Director editing "The Reveal": sequence options (14 s duration, Cinema 2.39:1 bars, fades, skippable) above a dopesheet with a camera track and one lane per animated object (hero, obelisk, sparks, cam-dolly, cam-crane). Keyframes are draggable diamonds; the red playhead scrubs the whole scene live in the viewport.](../../docs/img/cutscene-director.png)

Open `cutscene-demo.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyra-editor.exe --build <this folder> --run`.

## What to do

The cutscene plays automatically on boot (**On Start**). When it ends the
camera is handed back to you; walk to the gold **pedestal** and press the
USE button to replay it (**On Used → Play Sequence**). It is **skippable**
— press START while it plays to end it early.

The shot list (one sequence, "The Reveal"):

1. **0.0 s** — wide establishing shot from `cam-wide` (FOV 65), behind cinema
   bars that slide in while the picture fades in from black. The red **hero**
   cube starts sliding toward the plaza centre, spinning.
2. **3.5 s** — hard cut (Step easing) to `cam-low`, a ground-level wide lens
   (FOV 90) with a light handheld **shake**.
3. **5.0 s** — cut to `cam-dolly` (FOV 45). The camera *entity itself* is
   keyframed in an object track, so the shot is a lateral **dolly** gliding
   across the plaza. Meanwhile the **obelisk** swells and heats from gold to
   molten orange (a scale + colour track) and the hero keeps spinning.
4. **10.0 s** — cut to `cam-hero`, a low angle looking up as the hero
   **ascends** spinning and the `sparks` emitter on the obelisk switches on
   (a visibility track). This shot **blends** (Smooth easing) into the finale…
5. **13.0 s** — `cam-crane`, itself craning up and back on its own object
   track, pulls away for the climax as the picture fades to black; the bars
   slide out and the game camera returns to the player.

## How it is wired

- **Camera entities** (`+ Add object > Gameplay > Camera`) — `cam-wide`,
  `cam-low`, `cam-dolly`, `cam-hero`, `cam-crane`. Each is a film-camera
  marker with an FOV frustum wedge in the viewport; every camera-lane key is
  *bound* to one and films from its live pose + FOV. **Two of them move on
  their own object tracks:** `cam-dolly` slides laterally (5–10 s) and
  `cam-crane` cranes up/back (10–14 s) while staying aimed at the hero — the
  same rig a phone-take import produces.
- **Object tracks** — `hero` (position + rotation), `obelisk` (scale +
  colour), `sparks` (visibility, a clean off→on switch with Step easing),
  plus the two moving cameras above. One track per animated object.
- **Cuts vs blends** — the first shots use Step easing (hard cuts); the
  `cam-hero → cam-crane` transition uses Smooth easing, so the camera flies
  between the two entities' poses.
- **Sequence options** — *Widescreen bars: Cinema 2.39:1* (slide-in 0.6 s /
  slide-out 1.0 s), *Skippable*, *Fade in 0.8 s*, *Fade out 1.0 s*, no loop.
- **Trigger** — the pedestal's flow graph fires **Play Sequence** from both
  **On Start** and **On Used** (the pedestal is marked *Usable*).

Everything compiles into `src/scripts/sequences.gen.cpp` (keyframe tables +
the director script + the bars/fade compositor) on every build — open the
file to see what the editor generates from the timeline.
