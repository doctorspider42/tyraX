# tyra-editor-dev — portals

From `## Static shading and portal bounds (1.77.1)` and `## The portal doorway
rule (1.81.0)`. See `docs/portals.md`, `docs/collision-boxes.md`.

## Static shading and portal bounds (1.77.1)
**Cause/rule:** `templates.cpp` uses Gouraud for static object/batch vertex
colours; the `viewport.cpp` PS2 preview must stay in sync. Terrain remains flat.
This does not change OBJ face normals or grant imported meshes a lightmap. Portal
exit rejection uses `objectCollisionBox` and `boxRotate`, including the mesh's
offset centre; never infer an imported object's extent from scale alone.
**Fix:** portal static bags crossing the exit additionally use
`renderExitClipped`: interpolate all attributes. Since 1.77.2 `GeoPart` owns
clipped streams per portal, keyed by source bboxVersion/pointer/count/layout,
model matrix and exit plane. Only an invalidation drains PATH1 before replacing
buffers; view-camera changes reuse them. Refresh live bag descriptors even on
hits. Whole-front bags bypass it; animated bags retain their bounds-only path.
Portal Point Light effects are authored through the portal object list
(`props_ui.cpp`): the generated `updateAndRenderLightBeams` accepts the virtual
eye/look target and portal index, reusing `portalShowsObject`, exit-plane and
draw-distance filtering before rendering inside the destination depth/mask
bracket; its default arguments retain the ordinary camera pass. Baked GI and
projected light pools are separate paths.

## The portal doorway rule (1.81.0)
**Cause/rule:** `TerrainGame::portalDoorwayOpens(obstacle, plane, pierce)` is the
ONE implementation of "while a body's motion pierces a linked opening, the
geometry that opening was cut into stops blocking". It used to be four hand-copied
snippets — `collidePlayer` (armed by `updatePortalPass`), `sweepSphere` (armed by
`armSweepPass`), the physics static-solid pass in `updateObjectPhysics`, and the
render side's exit test — and the three collision copies all read the obstacle's
extent off `0.5 * scale` and its centre off `o.data.position`, i.e. they described
a unit primitive.
**Fix:** use `objectCollisionBox` + `boxRotate` (with the box's own off-origin
centre), the same idiom `renderOnePortalView` carries for the exit plane. **The
rule has TWO halves and a merged mesh needs the second one.** Fully behind the
plane is not enough: one exported model can hold the back wall, the side walls,
the door jambs and the roof at once, so its box straddles the portal plane and can
never be wholly behind it while its world box seals the authored opening
(`examples/showcase`'s `district-pavilion`, seen with `showCollision`). So the box
also qualifies when it CONTAINS the point where the motion pierces the opening.
That point is `portalCarryAim`'s own plane-crossing point, which it used to
compute and discard — it publishes it as `portalAimPoint`, `armSweepPass` copies
it into `sweepPassPoint`, the physics pass keeps it beside its local `aimPlane`,
and `updatePortalPass` (which has no motion segment) publishes the walker's probe
pushed onto the portal plane as `portalPassPoint`. **A plane and its pierce point
always travel together**; a new caller that arms one without the other silently
loses half the rule.

## The player walks through but the ball bounces
**Cause:** mesh (per-triangle) collision used to exist only in `collidePlayer`; a
body INSIDE a merged building's AABB was "penetrating" it and got ejected through
the floor.
**Fix:** since 1.83.0 the static-solid pass of `updateObjectPhysics` has its own
`collision == 1` branch over the same `GameModel::collider` (a vertical floor ray
from the previous underside to the current one, then a side-aware `resolveSphere`
for the walls, velocity reflected along the push). `sweepSphere` (camera boom,
carried object, carry whisker, the non-physics thrown arc) still collides against
the whole-mesh box (`objectCollisionBox`). **The doorway rule opens WALLS, not
floors, for a wall portal**: both `collidePlayer` and the physics pass compute a
`doorway` flag and, when `|plane.y| < 0.5`, keep the obstacle's ground response
(the mesh floor ray, a box's walk-onto top / landing-on-top) while dropping its
sides and overhead — skipping the whole obstacle dropped the walker under the map
on arrival, since the cellar mesh IS its own floor. A floor portal still opens
everything. **Whatever hopped through a portal is shown by it**:
`portalLastCrossed[oi]` (set by both hop paths — updatePortals' object block and
the thrown arc via `portalLastHop`) makes `portalShowsObject`/`portalCanCross`
true for that portal and `renderOnePortalView` draws those objects on top of the
authored list — the ball used to vanish at the plane because the list names the
room, not the ball. `Portal: player crossed` / `Portal: object N crossed` log
lines are the replay-readable signal. Replay with an ABSOLUTE project path or from
the repo root: the native build script now gets `fs::absolute(p.dir)` (a relative
`--replay examples/x` used to cd into `examples/x/examples/x`).

## Reference facts (upstream `scene-objects-and-gameplay.md`, portals)
- Doorway rule (1.81.0): `TerrainGame::portalDoorwayOpens(obstacle, plane, pierce)` is the ONE implementation (was four copies: `collidePlayer` armed by `updatePortalPass`, `sweepSphere` armed by `armSweepPass`, the physics static pass, the render exit test); obstacle extent via `objectCollisionBox` + `boxRotate`. Two halves: fully behind the plane, OR the box CONTAINS the pierce point; pierce point sources `portalCarryAim`/`portalAimPoint`, `sweepPassPoint`, `aimPlane`, `portalPassPoint`. A WALL portal (`|plane.y| < 0.5`) opens walls not floors; a floor portal opens everything.
- Whatever hopped through is shown: `portalLastCrossed[oi]`, `portalLastHop`, `portalShowsObject`/`portalCanCross`, `renderOnePortalView`; log lines `Portal: player crossed` / `Portal: object N crossed`; `--replay` with an absolute path. Portal exit rejection uses `objectCollisionBox` + `boxRotate`; static bags `renderExitClipped`; `GeoPart` owns clipped streams per portal key. Portal Point Light effects via `updateAndRenderLightBeams`.
