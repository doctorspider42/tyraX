# lighting — the whole lighting batch in one dusk plaza

First-person scene showing every effect from the lighting batch at once:
dynamic point lights with flicker and visible beams, the camera flashlight,
sun lens flare + god rays, blob shadows, and per-object **projected
silhouette shadows** — all against a warm low sun so the shadows stretch.

Run it like every example: open `lighting.tyra` in TyraX and press **F5**
(or `tyrax-editor.exe --build examples/lighting --run`).

## The tour (spawn faces the plaza)

| What | Where | Effect it demonstrates |
| --- | --- | --- |
| Two **torches** flanking the path | left/right, just ahead | **Dynamic point lights**: warm flickering pools on the terrain and the crates, each with a **glow corona + cone shaft** (Beam: *Glow + cone*) breathing with the flicker |
| The blue **crystal** | center of the plaza | A dynamic light driven by its **flow graph**: *Every 6 s* → *Set Light ×1.8* → *Delay 3 s* → *Set Light ×0.7* — the pool and its corona pulse together |
| The blue **lamp** | far right | A **baked** point light for contrast: static pool, zero runtime cost, steady corona |
| **Monolith, orb, obelisk** | back of the plaza | **Projected silhouette shadows** (*Cast shadow (projected)*): real shapes on the terrain — a rotated slab, a circle, a triangle — displaced along the low sun |
| Two **crates** | drop at boot | Physics + **blob shadows** (project preference): the soft dark quad sticks to the terrain while they fall and land |
| The **sun** | high ahead | **Lens flare** (ghosts along the view axis, occlusion-raycast — hide it behind the pillar) + **god rays** streaking the bright sky |
| The tall **pillar** | far left | Walk behind it looking at the sun: the flare **eases out** when the ray from the camera to the sun is blocked, and back in the open |

## Controls

- Left stick walks, right stick looks, X jumps (standard FPP template).
- **Circle** toggles the **flashlight** — near a torch, watch objects pick
  whichever light contributes most (one light slot per mesh; the engine
  picks the strongest, so the beam "wins" surfaces the torch barely reaches).

## What to look at

- **Flicker vs baked**: the torch pools breathe; the blue lamp's pool never
  moves (baked into vertex colors — free, but frozen).
- **The crystal's pulse** is authored entirely in its flow graph — open the
  crystal's *Flow Graph* tab in the editor to see the 4-node loop.
- **Projected vs blob**: the monolith's shadow is its actual rotated
  silhouette; the crates get the cheap soft blob. Both fade honestly — the
  blob with height, the projected ones with camera distance (35..50 units).
- **God rays** are strongest looking toward the sun with geometry breaking
  the sky — stand so the monolith or the pillar clips the sun's glow.

## How it's authored

Plain editor authoring, no scripts: per-light *Dynamic (live)* / *Flicker* /
*Beam* in Properties, *Cast shadow (projected)* on the three hero objects,
blob shadows in *Project > Preferences > Shadows*, flare and god rays in
*Tools > UI Editor* (per-scene overridable in *Scene > Post effects*), the
flashlight on the Player object. The crystal's pulse is the only logic, and
it is a flow graph.
