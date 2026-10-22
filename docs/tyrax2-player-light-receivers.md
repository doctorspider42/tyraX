# Private player-only dynamic light receiver experiment

This is a private night-game quality experiment, not an editor preference or a
production renderer change. The same native ELF compares full model lighting
against player/driven-vehicle receivers, first retaining scene light pools and
then suppressing those pools as a separate comparison. Both comparisons use
full/candidate/full and candidate/full/candidate orders.

The later [public receiver option](dynamic-light-receivers.md) is a separate
implementation. It retains projected pools and has its own qualification;
the archived prices below do not measure its common code or broader ownership.

## What the candidate changes

The receiver classifier uses the live player object indices and the currently
driven vehicle object. It is not a hardcoded authored object number. Scoped
identity is restored when each draw/sample scope exits. The engine skips the
per-bag live light pick for world receivers and supplies an explicitly disabled
spot light. Merely skipping the pick would be wrong: null means the global
camera flashlight, not no light.

Normal-lit and animated models have a separate EE `dynLightAt` ambient pickup;
that route uses the same receiver rule while retaining probe/sun/baked terms.
The tested scene has zero sparse scalar pickup invocations, so it does not
qualify savings or appearance on animated recipients.

Driven wheel geometry is separated from other vehicles in restricted modes.
A shared wheel bag can contain several cars; a single player flag on that bag
would wrongly illuminate the whole batch. Separate buffers retain the existing
slot/stamp rules. Full mode keeps its original definition-wide wheel batching.
Vehicle glass and deferred highlight body draws also propagate receiver identity.

The pool-cut comparison disables the existing scene pool family, including the
camera flashlight pool. Beams, coronas and vehicle lamp glow remain submitted.
Vehicle projected headlight work is retained. This is an intentional appearance
tradeoff: environment models cease reacting to live lights and the second mode
also removes scene pools. It is not an equivalent-output optimization.

## Measurement and validation

Both modes use the unchanged ordinary loop sampler, fixed camera, 24-bit night
configuration and no new clocks, fences or GPU holds. Receiver counters run
only at offsets 750 and 1155, outside the priced 800..1119 loop interval.
Common receiver classifier/branch/scoped bookkeeping remains in both arms and
is not separately priced. Do not subtract results from different compiled
fixtures as though this measured that common cost.

Native V30 binds 501 source files, 492 mirrored inputs, actual object/link/VU
images, matching ELF/symbol text and 298 runtime assets. The target sampler ABI
is unchanged. Actual defined-method object checks distinguish the compiled
split vehicle/physics/scene files from historical unused copies.

Both emulator comparisons completed 5400 loops and captured all three phase
images. Sparse mode 1 witnesses allow 5 bags and reject 129, compared with 134
allowed in full mode. Counts include bags that already opted out of live light
selection, so they are not counts of eliminated light-pick calls. These are
reached preparation invocations, not emitted
DMA packets or a full-window count. Mode 2 suppresses scene pool submission
while beam/corona and vehicle glow witnesses remain positive. Twelve malformed
mode-1 records and three malformed pool-cut records are rejected by the strict
host dialect. Cold rows do not introduce writes inside the timed interval.

Both comparisons complete both physical orders, each with 5400 loops, 384 raw
samples and 15 chunks under the same source and ELF.

| Candidate | Order | Full mean (ms) | Candidate mean (ms) | Candidate minus full (ms) |
|---|---|---:|---:|---:|
| Player receivers, pools retained | 0 | 19.588981 | 18.091850 | -1.497131 |
| Player receivers, pools retained | 1 | 19.592186 | 18.184715 | -1.407471 |
| Player receivers, scene pools removed | 0 | 19.520603 | 17.273355 | -2.247247 |
| Player receivers, scene pools removed | 1 | 19.610320 | 17.235693 | -2.374627 |

Receiver-only savings repeat at 1.407-1.497 ms. Removing scene pools as well
saves 2.247-2.375 ms against each run's full controls. These two comparisons
are not an additive stage decomposition. Restricted mode keeps all existing
receiver-scoped branch and wheel-split costs; common compiled code remains
unpriced. Full and receiver-only rendered periods remain about 33.367 ms.
The scene-pool-cut candidate means are 30.499-30.760 ms, with mixed cadence;
neither reaches stable 60 fps. Non-pacing candidate work remains 17.236-17.273
ms, above the one-refresh budget on average.

The initial kind11/order0 host client disappeared after the user-reported
Codex crash, without completion. Its raw/partial artifact is preserved and
excluded. A fresh reset and retry complete that order. This is a host
interruption observation, not evidence that the console hung. The final
reverse run remains on PS2 in mode 2, and the user confirmed the car, beams,
headlights, shadows and HUD look normal without new artifacts. The intended
absence of scene pools was explicit in that question. Console screenshot and
TV photon cadence are not measured.


## Limits and next work

The trial qualifies this fixed night fixture, not arbitrary projects. Production
receiver options require typed object/batch ownership through generated render
paths, portals/mirrors, secondary views, spawned player models and animated
receivers, plus editor model/serialization/codegen/UI parity and documentation.
Do not promote the scoped experiment itself as that complete public API.

The inherited analysis report's generic interpretation string is legacy text;
use `receiverModes`, `extraDisabledMasks`, sparse receiver witnesses and the
explicit result summary to identify these comparisons. Counts and elapsed
validation remain strict. Any initial host orchestrator failure before its
empty log acquired data is recorded separately; the already-running console
trial was preserved and completed by its exact owned waiter.

The [source/evidence archive](tyrax2-player-light-receivers-2026-10-05/README.md)
preserves source postimages, raw runs, native/host authorities and owned phase
images. Binary resources, ELF and objects are hash-only. Production engine,
editor code and project files are unchanged.
