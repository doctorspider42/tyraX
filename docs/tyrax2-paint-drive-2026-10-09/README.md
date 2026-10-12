# A12 full-throttle paint integration evidence

Integrated VU paint completes day/night/day road travel in the native emulator,
with scalar/readiness checks, camera sweeps, fallback/LOD and scene reloads.
This qualifies moving-state behaviour, not PS2 speed or whole-scene pixel equality.

- [Accepted V2](accepted-v2/README.md): 471 bound files, three trips over 182
  units, 8,981 paint visits and 1,138 specifically at speed >5, zero errors.
  Correct live-prefix light counts are 0/8/0. Ten lifecycle and five travel
  negative controls are rejected. Both EE and VU paths execute while moving.
- [Superseded V1](rejected-v1/README.md): trips and paint checks complete, but
  the inherited light observer counts stale enabled slots beyond dynLightCount.
  The corrected analyzer rejects its return-to-day witness. No renderer fix
  was justified by that counter error.

Each subarchive has its own SHA-256 manifest; the top manifest covers both
subarchives and this index. The original authored maps are unchanged.
No physical deploy, reset loop, default promotion or shutdown occurred.
