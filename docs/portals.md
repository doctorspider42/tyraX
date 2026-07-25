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
   render budget, exactly like a Mirror's reflected-object list. Instead of
   (or on top of) listing names, set a **catch area**: everything inside an
   Area object's box joins the list, resolved at build so the count stays
   visible — see [areas.md](areas.md). Terrain and
   the sky dome have their own toggle (**Terrain + sky in view**, on by
   default). Keep the list to the landmarks that sell the destination.
   Or tick **All objects in view (experimental)**: every scene object
   renders in the through-view and the list is ignored. The virtual
   camera's frustum culling drops off-view geometry EE-side and draw
   distances are measured from the virtual eye, so the practical cost is
   what the destination actually sees — but a big scene pays a second
   submission pass whenever that portal's view is live; watch the
   FPS/profiler before shipping, and prefer the list for release builds.
   Static batching interplay: objects on a portal's view list are
   **excluded from static batching** at build (like a Mirror's list — the
   through-view re-submits them as solo bags), and in the **All objects**
   mode a batched object gets a one-time solo bake the first time a live
   view needs it, so batched decor shows through the portal either way.
   (The merged batch bags themselves are never submitted into a
   through-view: a merged bag can't drop just the members behind the
   destination's exit plane, and the wall the target portal is mounted on
   would fill the view with its backside.)
4. **A thrown (or dropped) pickable flies through any linked portal**, like
   the player does — no flag needed. The hop maps position and the full
   velocity vector through the pair, so it exits the target with the
   matching motion, and while the flight is aimed into an opening the wall
   the portal is mounted on stops colliding for it (the walkers' doorway
   rule) — wall portals swallow throws instead of bouncing them back. The
   released body stays "portal-free" until it settles to rest.
   **Carrying** a pickable through a portal works too: walk into the
   opening holding it and both you and the object come out the far side.
   As the object reaches the surface it flies on *through* — mapped to the
   far side, where you see it just beyond the portal (drawn by the
   through-view) — then re-anchors in front of the new camera the moment
   you cross. (This only works through a portal whose through-view shows
   the object — one set to **All objects in view**, or with the object on
   its view list. Through a plain **Teleport physics objects** portal,
   which renders no through-view, the carried object stops at the surface
   instead.)
   For *ambient* rigid bodies — anything that falls or rolls in on its
   own — the rule is **whatever the portal shows can also go through it**:
   an object on the portal's view list crosses, **All objects in view**
   opens the crossing to every rigid body, and the **Teleport physics
   objects** flag does the same when you want crossings without the
   through-view cost. A floor portal linked to a downward-facing ceiling
   portal makes the classic **infinite fall** — see `examples/portals`.
   Falls are capped at a 30 u/s terminal velocity, so the loop stays fast
   but readable instead of accelerating into a strobing blur; the portal's
   swallow zone is tested against the whole frame-to-frame motion segment,
   so a terminal-velocity faller cannot skip over it and snag on the
   terrain.

The editor viewport draws the surface as a translucent tinted quad, a
**bright arrow out of the entry face** (the +Z front — the side that shows
the view and teleports; flip the portal 180° if the arrow points into your
wall) and a link line to the target; the live view exists only in the game
(it is the PS2 in-place render).

## How the game renders it (and what it costs)

- Each frame the game picks up to **four** portals — the nearest linked
  ones the camera is in front of — and renders their through-views
  **in-place, at full resolution, straight into the framebuffer** (no render-to-texture, no
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
- The teleport uses the same mapping as the camera, with **no exit
  offset** — the isometry carries your overshoot past the target plane, so
  crossing is mathematically continuous (the arrival matches what the
  surface showed, frame to frame). The camera is rebuilt on the hop; no
  frame renders from the departure side. The player probes with two
  segments — waist (door-sized wall portals) and **feet**, so jumping or
  dropping into a floor portal teleports too.
- **The doorway moment**: with your eye a breath from the plane, parts of
  the quad fall behind the near plane and a clipped opening would let the
  world behind the free-standing surface peek around it for a frame
  ("looking through two portals at once"). Inside that zone — close,
  inside the rectangle, looking into the surface — the opening expands to
  the whole screen: the destination fills the view until you cross.
- **Dead zone**: the virtual camera sits behind the exit plane, and the
  PS2 has no oblique near plane to clip what lies in between. Both view
  objects and terrain chunks fully on the camera side of the target plane
  are skipped automatically (through a real hole they'd be invisible;
  chunks carry their exact height extent and objects project their exact
  OBB extent onto the plane normal, so a **wall the target portal is
  mounted flush on counts as behind** and never fills the opening with
  its backside — while geometry genuinely poking through the plane still
  renders) — a floor→ceiling pair keeps **Terrain + sky in
  view** on and the opening correctly shows the sky-dome gradient and
  whatever falls through, not the terrain's backside. A chunk that
  straddles the plane still renders whole, so on an extreme cliff-edge
  portal a backside sliver can peek in — nudge the portal off the
  geometry if it does.
- **Floor portals swallow**: while a body stands over a linked floor
  portal's rectangle (front normal pointing up), it stops colliding with
  the terrain — otherwise the ground would rest it before it could reach
  the crossing plane, and a portal lying ON the ground could never eat
  anything. Works for the player (walk into one and you drop in like a
  pit) and for physics objects; wall and ceiling portals are unaffected.
- **Doorways open in collision**: while the player's body column sits
  inside a linked portal's opening (any orientation), object collision
  ignores everything fully BEHIND that portal's plane (the same exact-OBB
  rule as the through-view) — so the wall a portal is mounted flush on
  lets you walk through the opening, while the same wall still blocks
  right beside it, and a crate standing in front of the surface still
  blocks too. Physics objects never collide with objects, so they need no
  equivalent.

## Limits (era-honest by design)

- **Up to four live views per frame** (nearest first; each is its own
  destination render, so every extra visible portal costs its content).
  Beyond the budget, a portal shows its tint. Where two openings overlap
  on screen, the nearer one wins its bbox (views are carved
  farthest-first).
- **No portal-in-portal recursion**: a portal never appears inside another
  portal's view (it would sample the very target being rendered).
- Walkers keep no horizontal velocity state, so a **tilted** pair (floor ->
  wall) carries only the vertical component of the crossing speed. Yaw-only
  pairs — the common teleporter — preserve movement perfectly.
- Animated models in the through-view render their last skinned pose
  (one frame stale).
- **Particle emitters show through** (fire, smoke, fog, rain, …): the VU1
  billboard program re-expands the same particle centers from the virtual
  camera's basis, so an emitter in the view (listed, or any emitter with
  **All objects in view**) faces the portal viewer correctly — the same
  simulation, a second cheap on-VU expansion. Mirrors, on the other hand,
  show only their glass through a portal (their reflected copies are a
  main-pass trick that would need its own bracket).
- With **terrain streaming** on, the view renders only resident chunks —
  keep both portals inside the streamed radius or turn the portal's terrain
  toggle off and list objects instead.
- Portals are baked into the `PORTALS` side table at build, so Live Link
  can live-move an existing portal but cannot spawn a new one (the chip
  flips to "LIVE (rebuild)"; same rule as mirrors).
