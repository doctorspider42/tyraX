# cutscene-demo example

A 14-second in-engine cutscene ("The Reveal") built with the **Cutscene
Director** (*Tools > Cutscene Director*): camera shots bound to Camera
entities, a dolly tracking shot, a hard cut, camera shake, animated FOV,
Cinema 2.39:1 widescreen bars and a fade-in/out — all running on the PS2.

Open `cutscene-demo.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyra-editor.exe --build <this folder> --run`.

## What to do

The cutscene plays automatically on boot (**On Start**). When it ends the
camera is handed back to you; walk to the gold **pedestal** and press the
USE button to replay it (**On Used → Play Sequence**). It is **skippable**
— press START while it plays to end it early.

The shot list (one sequence, "The Reveal"):

1. **0.0 s** — wide establishing shot from the `cam-wide` entity (FOV 65),
   behind cinema bars that slide in while the picture fades in from black.
   The red **hero** cube starts sliding toward the plaza center, spinning.
2. **3.5 s** — hard cut (Step easing) to `cam-low`, a ground-level wide
   lens (FOV 90) with a light handheld **shake**.
3. **5.0 s** — cut to `cam-dolly` (FOV 45). The camera *entity itself* is
   keyframed in an object track, so the shot is a lateral **dolly** gliding
   across the whole plaza while the hero spins in place.
4. **10.0 s** — cut to a free low-angle shot looking up: the hero **ascends**
   spinning; the `sparks` emitter on the obelisk switches on through a
   visibility track. The shot blends (Smooth easing) into...
5. **13.0 s** — a high pull-back as the picture fades to black; the bars
   slide out and the game camera returns to the player.

## How it is wired

- **Camera entities** (`+ Add object > Gameplay > Camera`) — `cam-wide`,
  `cam-low`, `cam-dolly`. Each is a film-camera marker with an FOV frustum
  wedge in the viewport; a camera-track key *bound* to one films from the
  entity's live pose with the entity's FOV. `cam-dolly` is additionally the
  target of an object track (position keys at 5 s and 10 s) — that is the
  entire dolly rig.
- **Object tracks** — `hero` animates position + rotation (4 keys);
  `sparks` animates only visibility (off at 0 s, on at 9.5 s — visibility
  steps between keys, so it is a clean switch).
- **Sequence options** — *Widescreen bars: Cinema 2.39:1*, *Skippable*,
  *Fade in 0.8 s*, *Fade out 1.0 s*, no loop.
- **Trigger** — the pedestal's flow graph fires **Play Sequence** from both
  **On Start** and **On Used** (the pedestal is marked *Usable*).

Everything compiles into `src/scripts/sequences.gen.cpp` (keyframe tables +
the director script + the bars/fade compositor) on every build — open the
file to see what the editor generates from the timeline.
