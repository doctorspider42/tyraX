# night-walk example

A dark backlot, a torch, and everything the
[flashlight](../../docs/flashlight.md) can do — this is the example for the
light itself. Two building facades, a fence, a dumpster, a truck and a couple
of sheds stand around a dirt yard (models from Kenney's CC0 *Retro Urban Kit*),
and the whole scene is arranged to be read by torchlight.

Open `night-walk.tyra` in the editor and Build & Run (`F5`), or build headless:
`tyrax-editor.exe --build <this folder> --run`. It ships in the **debug**
profile, so the FPS / MEM / VRAM readouts are on screen. Walk with the left
stick, look with the right, and **Circle** switches the torch off and on.

## What it shows

- **The pool.** Point the torch at the ground: that ellipse is a **projected
  texture** (`res/hud/flashlight-gobo.png`), per pixel, taking its coordinates
  from the beam's own frustum — not the terrain's vertex lighting. It
  stretches as you lower the beam and follows the dirt's relief.

- **Light on real geometry.** Shine at a facade, the truck, the dumpster:
  whatever solid thing the beam meets is rendered a second time, additively,
  with the gobo projected onto its real triangles. The west facade is turned
  24° off the world's axes to prove the receivers are oriented boxes, not
  AABBs. Sweep the beam from the dirt up a wall — the light crosses the join
  without blinking.

- **Shadows, both ways.** This project ships with **Flashlight shadow
  volumes** ON (*Preferences > Rendering*): put the dumpster or the truck in
  the beam and its shadow is CARVED out of the light, per pixel, on the ground
  and on the facade behind — stand behind the truck and the torch genuinely
  does not reach you. Turn the setting off to compare with the silhouette
  mode, where the flagged casters (*Cast shadow (projected)* on the dumpster
  and the truck) throw mesh-shaped patches from the torch instead.

- **Two sheds, one of them pre-lit.** The same textured model in the same
  light. The left one had the scene's light
  [baked into its texture](../../docs/prelit-models.md)
  (`--bake-object-light night-walk shed-lit`), so its planks read per pixel
  and the torch's pool lands on them; the right one takes the ordinary
  per-vertex route and the cone floods it to white at close range. That is
  the whole argument for pre-lighting a textured model, in one screenshot.

- **The cone, kept in its lane.** Small props — the trees, the pallets, the
  street light — take the engine's per-vertex spot term, deliberately dimmer
  (the torch's colour is ~0.6, not white) and soft-edged, because with no N·L
  a bright cone flattens a low-poly prop into a cutout. The terrain and the
  facades never take it at all; their light is the projected pool.

- **A street lamp that lights its street.** The lamp by the pallets carries
  a dynamic **spot light** (*Spot (cone)* on a point light): the cone points
  down the light object's local -Y, nearby props take it per vertex, and the
  pool under it is the same per-pixel gobo projection the torch uses - from
  the lamp's own frustum. It flickers a little, wears a corona, and answers
  the *Set Light* flow node like any dynamic light.

## Things worth trying

- *Preferences > Rendering > Flashlight shadow volumes* — flip it and rebuild;
  same yard, same casters, two philosophies of shadow.
- The torch's **Reach** and **Cone half-angle** (Properties on the player)
  size the pool as well as the light.
- Replace `res/hud/flashlight-gobo.png` with your own gobo — a cross, a
  grille, a cracked lens. Shape in RGB, black border, power-of-two sides.

## Assets

The buildings, fence, props, trees and the truck are from
[Kenney's Retro Urban Kit](https://kenney.nl) (CC0). The facades are several
kit wall tiles merged into ONE .obj each — deliberately: the torch lights the
nearest three solids in its cone, so a wall must be one object to light as one
wall. The sheds and the gobo are project-made. The heightmap is generated
value noise with a flat clearing at the spawn.
