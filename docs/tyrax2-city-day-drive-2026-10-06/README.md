# Full-city daylight driving versus a representative parked view

The physical PS2 trial reproduces daylight driving drops in the full Vehicle
Playground city, while two later parked windows retain approximately 59.94 Hz
at the representative midpoint view of the slowest driving window. This does
not support dismissing the entire city as uniformly too expensive. It also
does not isolate physics from changing visibility/render preparation.

| Observed physical window | Non-pacing work per loop | Entry period | Equivalent average rate |
|---|---:|---:|---:|
| Driving, all five 64-loop chunks | 13.7981 ms | 18.3519 ms | 54.49 Hz |
| Slowest driving chunk, 864–927 | 15.5815 ms | 21.1150 ms | 47.36 Hz |
| Parked representative view, first 320 loops | 13.5018 ms | 16.6833 ms | 59.94 Hz |
| Parked representative view, repeat 320 loops | 13.5003 ms | 16.6833 ms | 59.94 Hz |

These rates are reciprocals of bounded loop-entry means, not full-run minimum
FPS or a scanout trace. All three measured windows have 320 rendered
completions and zero synthetic completions. The 128 raw driving samples cover
900–1027, not the entire five-chunk drive: non-pacing work p95 is 19.3025 ms,
maximum 22.0631 ms, and 20 samples exceed the approximately 16.6833 ms budget.
There are 21 loop-entry intervals above 25 ms and 20 rendered intervals above
25 ms. Each parked raw window has zero such long intervals or over-budget
work samples; parked raw work maxima are 13.6301/13.6466 ms.

The inherited parser's `over16_68` statistic is **not** a missed-field count:
nominal 59.94 Hz itself has a period slightly above 16.68 ms. Use the actual
raw records, the approximately 16.6833 ms budget and the 25 ms long-interval
classification above. Non-pacing work is inclusive elapsed loop work after
subtracting existing pacing accounting. It is not pure EE execution, VU time,
or a measurement of one particular physics/render function.

## Controlled scope and limitations

This private generated-source clone derives from the previously qualified
quiet full-city motion fixture at public source base `5b6cd898`. The later
public code difference at `1823dbded` is an inactive frame-profile helper;
there is no later city/renderer behavior change to import. All receiver
semantics are selected explicitly, the mood starts at fixed noon, normal
physics, logic, camera update, audio, visual options and static batching stay
active. Healthy telemetry is privately quieted; warnings, errors and asserts
remain. FrameProfile and HardwareTrace are compiled out. The authoring file
still describes the parent fixture: the preparation script patches generated
defaults/receiver selection, so editor regeneration would erase this trial.

Phase0 uses the inherited throttle/steer/brake route over frames800–1119.
Five midpoint poses at832/896/960/1024/1088 are stored without new clock reads.
After all chunks finish, the maximum elapsed-minus-pacing chunk selects its
midpoint. PS2 selected chunk1, frame896, approximately car x=-0.814, z=-59.913.
At frame1700 the car is relocated once and its velocities cleared once,
outside measured windows. Normal physics continues with neutral input.
The selected camera is held after the ordinary camera update during both
parked phases; their measured windows follow about900 warm-up loops.

All four parked cold camera witnesses match the physical selected midpoint
bit-for-bit. The car settles and creeps slightly: observed horizontal drift
is 0.040–0.143 units and speed approximately -0.00294 units/s. This is a
near-stationary representative view, not a byte-identical vehicle-state replay.
The moving chunk changes view and workload throughout its64 loops. Therefore
the roughly2.08 ms contrast against its midpoint **cannot** be reported as a
causal physics-only saving, or as proof that every view in that chunk is cheap.

The ordinary sampler is unchanged: existing pacing/present stamps, five chunks
and128 raw samples per phase, no new synchronization/drain. Sparse pose storage,
selection, controller and parked-camera assignment are additional unpriced
scaffolding. No production optimization or observer-zero-cost claim is made.
The inherited `hourZero=1` field verifies **12:00** in this private fixture;
its historical name does not mean this trial ran at midnight.

## Evidence and verification

- [Native source/object/ELF binding](preparation/vehicle/native-proof.json):
  ELF SHA256 `d16d091065687984924488ad62ff3cd3242b932bfda30240500a14becb97dae8`;
  primary-agent source/hook/controller/asset review retained separately.
- [Emulator correctness result](emulator/strict-result.json):5400 loops,
  actual motion and representative parking, exact selected camera witnesses,
  inspected start/park rasters. Emulator timings are not hardware prices.
  Exact owned PID11568 was stopped and `/proc/11568` absence independently
  checked before physical launch; Xvfb was retained.
- [Physical result](physical/strict-result.json):5400 loops,15 chunks,
  384 raw records and six cold contexts, with strict pose/selection validation.
  The serving client PID44564 remains active after completion; archived stdout
  is a completed immutable prefix, not a terminal-client-exit claim.
- [Raw physical prefix](physical/completed-prefix/ps2client.stdout),
  [sample file](physical/completed-prefix/closeout-samples.txt) and
  [normalization proof](physical/completed-prefix/freeze-proof.json): transport
  CR/LF fragments are removed; only complete bounded ASCII records are extracted.
  No field/value repair or missing-record invention. Non-ASCII startup bytes
  are preserved in the raw prefix. Strict parsing checks independent sampler
  records against the selected pose's work ticks.
- [Ten replay parser controls](preparation/parser-controls.json) accept the
  complete reference and reject incorrect selection, pose ticks, camera,
  parking frame/state, velocity, drift, capture mask and duplicate selection.
  These controls are not new device measurements.

The first native attempt failed because a private line comment swallowed the
rest of a compact header statement; no device ran it. Both logs are retained,
and the repaired v2 build is the bound ELF. An initial host deployment wrapper
was invoked under legacy PowerShell, whose Path lacks GetRelativePath; it
failed before launching a client. The corrected PowerShell7 launch used a
fresh v2 evidence directory. Neither is a renderer runtime failure.

Physical TV confirmation is pending; the checked emulator raster does not
replace a human review of the PS2's image.

## Next discriminating measurement

The useful target is transient work during motion. Separately count actual
vehicle substeps, collision/road gathers and render preparation in slow versus
fast loops, with a calibrated observer. The source's vehicle step count rounds
`dt * 50` and becomes two once dt reaches about30 ms, so a missed presentation
can cause more physics work on the following loop. This is a source-derived
feedback hypothesis, **not** demonstrated attribution in this trial. Changing
visibility, culling/clip work and moving-car rendering remain plausible.
Do not disable correct timestep semantics merely to obtain a faster counter.

The full-night stable60 goal remains unresolved. This trial makes the daytime
symptom reproducible and narrows the next investigation; it does not establish
one universal city geometry ceiling.
