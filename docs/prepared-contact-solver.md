# Prepared contact solver

The prepared contact solver evaluates the existing rigid-body model from compact aligned state and a response matrix cached once per contact and step.

It preserves collider shapes, inertia, the slip-direction Coulomb friction
model and circular friction bound, contact order, and six velocity plus three
split-impulse passes. There is no simulation-rate reduction or contact cap.
Impulse accumulation still starts from zero each step; this experiment does
not introduce warm starting or change sleeping rules.

For a contact impulse `p`, the relative velocity change is `K p`, where `K`
combines inverse masses, inverse inertias and the contact offsets. The repeated
friction solve can use this prepared response instead of recomputing those
terms for its current slip direction. Normal angular responses are cached too.
The scalar and VU0 macro implementations share prepared inputs and retain
sequential dependencies between contacts. VU0 updates operate directly on
aligned buffers; they do not stage each impulse through temporary arrays.

## First physical PS2 trial

One ELF compared the prior solver from `2ed7cb6f7`, prepared scalar and prepared
VU0 from `6d610765f`, across seven workloads and reversed arm order on repeat.
All arms shared the existing step layout, renderer and complete baked assets.
The first implementation used 128-byte solver bodies and 160-byte contacts.

| Additional piled boxes | Prior FPS | Prepared scalar FPS | Prepared VU0 FPS |
| --- | ---: | ---: | ---: |
| 32 | 48.2 | 49.4 | 49.1 |
| 64 | 35.4 | 35.1 | 33.0 |
| 128 | 19.2 | 19.4 | 19.8 |

This is **not a convincing general speedup**. Contact trajectories and active
populations differ, and small pooled FPS differences must not be interpreted
as isolated kernel gains. The 64-box VU0 case is worse. Both repeats and raw
frames are retained in the [wide-buffer summary](../tools/ps2rig/results/solver-wide-2026-10-29.json)
and [compressed CSV](../tools/ps2rig/results/solver-wide-2026-10-29.csv.gz).

All 42 phases / 25,200 measured frames passed the completeness checks. The
physical-console startup comparison on 64 coupled graphs found zero errors:
maximum scalar/reference component difference 0.000000063 and VU0/scalar
0.000000047. The host's 2,000-graph reference comparison also passed. Numerical
agreement is a correctness result, not evidence of a speedup.

## Compact representation follow-up

The next representation uses 112-byte bodies and 128-byte contacts, maintaining
16-byte alignment. Inverse mass and existing body indices use otherwise unused
vector lanes. The physical response matrix is symmetric, so six independent
coefficients retain the full response; no matrix direction is removed.
The host reference comparison's maximum component difference is 0.000000268.

The follow-up rig also measures contact preparation, all solver passes and
copy-back separately from detection. Its `solver_ticks` scope includes the
packing/preparation cost that a standalone arithmetic kernel would miss.
The complete-contact accounting and the three-arm reversed-order comparison
remain mandatory. Hardware qualification and the default backend decision
are recorded after the follow-up run.


A 20-phase partial compact run exposed that eager matrix preparation did not
amortize consistently. Its inclusive drive solver cost per contact was about
16.6/18.3/18.8 microseconds for the prior solver at +32/+64/+128 piles and
18.9/19.1/19.1 for prepared scalar. Different contacts and trajectories limit
these ratios; this partial run is not a completed reversed-order comparison.
It was intentionally stopped before a further implementation change. Its
[partial summary](../tools/ps2rig/results/solver-eager-partial-2026-10-29.json)
and [raw rows](../tools/ps2rig/results/solver-eager-partial-2026-10-29.csv.gz)
are retained as negative evidence.

The final preparation computes the six response coefficients directly from
inertia/contact offsets, and defers them until friction needs a nonzero update.
A zero normal impulse plus zero accumulated friction has an exactly zero
Coulomb-friction update; that arithmetic is skipped while all normal and split
passes remain. The physical model and nonzero friction updates are unchanged.
The independent 2,000-graph host reference comparison has maximum component
difference 0.000000231. The final full console run is qualified separately.
