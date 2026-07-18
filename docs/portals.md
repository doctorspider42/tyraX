# Portals

A **Portal** is a scene object (Insert > Gameplay > Portal): a rectangle that
links to another Portal in the same scene. The surface shows a **live view
through to the target** — a second camera, kept in lockstep with the player
camera — and anything that walks into the front face is **teleported to the
target portal with position, view angle and vertical velocity carried
through**. Two portals pointed at each other make a seamless two-way door
between distant parts of the map.

## Authoring

1. Insert two portals (Insert > Gameplay > Portal), place them where you want
   the ends of the corridor. The **+Z face is the front** — the side that
   shows the view and accepts the crossing (same convention as Decal/Mirror).
   Scale X/Y sets the rectangle; a door-sized 1.6 x 2.4 is the default.
2. In Properties > Portal, pick the **Target portal**. Links are one-way by
   design; use **Link back (make two-way)** to complete the pair.
3. List the **objects visible through** the portal — the explicit list is the
   render budget, exactly like a Mirror's reflected-object list. Terrain and
   the sky dome have their own toggle (**Terrain + sky in view**, on by
   default). Keep the list to the landmarks that sell the destination.
4. Optional: **Teleport physics objects** carries physics-enabled objects
   that cross the surface too (the player always teleports). Vertical
   velocity maps through the pair, so a floor portal linked to a
   downward-facing ceiling portal makes the classic **infinite fall** — see
   `examples/portals`. (Object physics clamps falls at a 50 u/s terminal
   velocity, so the loop stays smooth instead of accelerating forever.)

The editor viewport draws the surface as a translucent tinted quad plus a
link line to the target; the live view exists only in the game (it is a PS2
render-to-texture pass).

## How the game renders it (and what it costs)

- Each frame the game picks **one** portal — the nearest linked one the
  camera is in front of — and renders its through-view **in-place, at full
  resolution, straight into the framebuffer** (no render-to-texture, no
  resampling — the opening is pixel-for-pixel as crisp as the scene around
  it). The GS has no stencil, so the shaped opening is carved with the
  z-buffer: the destination view renders right after the frame clear,
  scissored to the quad's screen bbox; then the bbox depths are re-farred,
  the quad interior is capped at the surface depth (a z-only triangle fan
  from the quad's 4 corners, frustum-clipped on the EE — a handful of
  flops), and the spill outside the opening is repainted with the clear
  color. The main scene then draws around it: walls in front still occlude
  the view, the wall behind loses the z-test, and DoF/particles treat the
  surface as solid geometry.
- Content: sky + terrain (optional) + the authored object list, submitted
  through the normal VU1 static pipeline — transform and clipping stay on
  VU1, the virtual-camera math runs on the VU0-macro Vec4/M4x4 ops, and
  the EE only packages the extra submissions.
- The virtual camera is the player camera mapped through the pair (source
  local frame -> 180° flip -> target frame) with the **same projection as
  the screen** (only the view matrix swaps), so the destination lands
  exactly where the opening sits — correct parallax with no per-pixel
  work.
- Every other portal (and a portal with no/dangling target) draws as a
  translucent quad tinted with the object color. Hiding a portal (layer or
  Show/Hide Object) disables its view *and* its teleport.
- The teleport uses the same mapping as the camera, so the arrival matches
  what the surface showed — crossing is seamless. The frame camera is
  rebuilt on the hop; no frame renders from the departure side.

## Limits (era-honest by design)

- **One live view per frame.** Facing two linked portals at once, the
  farther one shows its tint. A pair member's own view activates as you
  approach it.
- **No portal-in-portal recursion**: a portal never appears inside another
  portal's view (it would sample the very target being rendered).
- Walkers keep no horizontal velocity state, so a **tilted** pair (floor ->
  wall) carries only the vertical component of the crossing speed. Yaw-only
  pairs — the common teleporter — preserve movement perfectly.
- Animated models in a view-object list render their last skinned pose
  (one frame stale).
- With **terrain streaming** on, the view renders only resident chunks —
  keep both portals inside the streamed radius or turn the portal's terrain
  toggle off and list objects instead.
- Portals are baked into the `PORTALS` side table at build, so Live Link
  can live-move an existing portal but cannot spawn a new one (the chip
  flips to "LIVE (rebuild)"; same rule as mirrors).
