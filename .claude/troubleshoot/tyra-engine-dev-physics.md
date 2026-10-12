# troubleshoot/tyra-engine-dev-physics.md

The XZ-grid triangle collider and its ray queries. Symptom first.

## Collision queries paid for every border triangle

**Cause:** `physics/CollisionMesh` is an XZ-grid triangle collider. The grid clamps an
outside query onto its edge cells, so a query that starts outside the mesh would otherwise
test every border triangle.
**Fix:** both queries test the mesh's own box FIRST — keep that early-out exact: the
sphere's reach is radius plus the **0.6** crossing band.

## The rigid-body solver needs the hit triangle's normal

**Cause:** the per-corner contacts need a mesh-local normal.
**Fix:** `raycast(..., outNormal)` also returns the hit triangle's mesh-local normal
(docs/physics.md). `Ray::intersectTriangle` is the triangle primitive underneath it.
