# Temporal physics experiment

This experiment measures geometry coherence, persistent contact impulses and
independent dynamic islands without reducing collision coverage or solver passes.

The previous scalar integration is the reference. A same-ELF three-arm rig
compares reference, geometry/axis caches alone, and caches plus warm starting
and islands, with reversed arm order in a second repeat. Stage timings include
prediction, world contacts, body-grid construction, body contacts, solver and
write-back; nested query/SAT scopes must not be added twice to their parents.
Host gates cover cached-axis CCD conservatism, contact identity/invalidation,
unbounded history storage and stable island partitioning. Full-game and
physical-console qualification are recorded after the experiment.

## Implementation and gates

`TYRA_PHYSICS_COHERENCE` controls persistent world-box transforms and per-step
hull normal/edge transforms, plus a previous-frame separating-axis hint.
`TYRA_PHYSICS_WARM_START` controls impulse history, and
`TYRA_PHYSICS_ISLANDS` controls stable component grouping and coordinated
sleep/wake. The current provisional defaults are 1/0/0 pending console timing.
The prepared scalar/VU0 experiment remains separate and disabled in this rig.

Host gates passed 50,000 swept/rotated box pairs, including arbitrary cached
axes and separated-endpoint crossings. History gates passed one-to-one
matching, shape/state/scene/timestep invalidation, 4,096 simultaneous records
and stable island order. The first native integration build exposed an orbit
header dependency and a timing-scope visibility issue; both were corrected.
External-change revisions now live in always-regenerated side state, preserving
owned header layouts. The subsequent full native PS2 build passed.

The benchmark adds squared-velocity and maximum contact-depth diagnostics.
Squared velocity is not mass-weighted kinetic energy, and its units depend on
the frame timestep. Driving adds physical impulses, so settling and wake
behaviour must be assessed alongside FPS, rather than requiring monotonic
energy during a car impact.

The isolated PCSX2 software-renderer run completed six 600-row phases,
including both full-feature drive trials. Contact/candidate accounting and
disjoint-stage timing checks passed; history recorded 2,636 and 2,300 matches
in the two full-feature phases. No assert, exception or missing-resource
diagnostic occurred. Physical-console qualification follows separately.
