# Physical daylight substep attribution

The PS2 trial completes5400 loops and confirms actual two-substep activation,
but the observed vehicle wrapper is not the dominant source of the daylight
work spikes. The added step usually follows an already slow entry interval.
This supersedes the hardware-pending status in the immutable preparation
checkpoint one directory above; it does not change production physics.

| Driving raw cohort | Loops | Mean vehicle scope | Maximum scope | Mean inclusive non-pacing loop work |
|---|---:|---:|---:|---:|
| One executed substep | 107 | 0.5989 ms | 1.3782 ms | 14.3382 ms |
| Two executed substeps | 21 | 0.8187 ms | 1.3695 ms | 15.7232 ms |

The scope is the existing `stepVehicles` wrapper through
`updateVehicleTutorial`: cache invalidation and all actual `updateVehicles`
invocations, with the private counter overhead inside. Separately called
smoke/skids/debris, other game logic and rendering are outside it. These are
elapsed intervals and can include thread preemption; neither scope nor residual
is a pure EE instruction measurement.

Nineteen of128 driving raw loops exceed approximately16.6833 ms work: fourteen
have one substep and five have two. Their mean scoped vehicle time is0.6251 ms,
while **18.5047 ms remains outside that scope**. The largest observed work spike
is23.6103 ms with just one substep and0.5288 ms inside the vehicle wrapper.
The instrumented scope therefore does not explain the bulk of these spikes.
The0.2198 ms difference between step cohorts is descriptive, not a causal
price of an extra substep: dt, world pose and changing view differ.

## Transition ordering

All21 two-step rows have a preceding loop-entry interval above25 ms; none of
the107 one-step rows does. Of15 observed one-to-two transitions, fourteen have
an immediately preceding **measured one-step loop already above the work
budget**. For example:

| Raw frame | Executed substeps | Inclusive non-pacing work | Vehicle scope | Relevant entry interval |
|---|---:|---:|---:|---:|
| 917 | 1 | 23.6103 ms | 0.5288 ms | 16.6838 ms |
| 918 | 2 | 15.3012 ms | 0.7821 ms | 33.3678 ms |

One transition's preceding work is16.6672 ms, just below the nominal budget,
yet its following entry interval is33.3665 ms. Do not assert that work above
budget is the only missed-presentation mechanism: existing pipeline overlap,
presentation phase and unsampled timing remain relevant. Entry interval refers
to the preceding loop-entry period, not an independent future frame or scanout.

The data support extra physics as a response to prior cadence loss and a
possible smaller feedback cost. They do not show it initiating the largest
observed slowdown, or identify which remaining subsystem does.

## Observer calibration is not a clean scalar tax

Both parked controls have128 raw loops, one executed substep throughout,
full-rate entry intervals and exact cold selected-camera matching. Their
near-stationary car retains the parent's small reported creep.

| Parked phase | Timer groups | Group mean work, ms | Net ON-minus-OFF contrast | Outer-group spread |
|---|---|---|---:|---:|
| First | OFF40 / ON48 / OFF40 | 13.9319 / 13.9994 / 13.9319 | +0.06753 ms | 0.00005 ms |
| Repeat | ON40 / OFF48 / ON40 | 14.0650 / 14.0042 / 13.9330 | -0.00523 ms | 0.13196 ms |

The sign differs and the reverse outer spread is larger than either contrast.
Consequently these short windows **do not resolve a reproducible active
observer tax**; no uniform correction is subtracted. Common metadata/counter/
3072-byte row-buffer footprint remains unpriced. Vehicle scope includes its
internal counter and active elapsed-read path, so the table does not claim
exact uninstrumented production costs. The differences are small compared
with the many milliseconds of observed work outside the wrapper, but are not
an observer-zero-cost proof.

Across all five64-loop chunks, driving work/entry means are13.9584/18.4563 ms
(~54.18 Hz); parked first13.9575/16.6833 ms and repeat14.0008/16.6833 ms
(~59.94 Hz). These are this instrumented route's bounded descriptive windows,
not a same-pose A/B against the previous ELF. Every measured320-loop phase
has320 rendered completions and zero synthetic completions. Stable full-night
60 remains unresolved.

## Evidence and next target

[Strict protocol result](strict-result.json) validates all15 chunks,384 raw
loops,384 scope records, actual planned/executed agreement,512 additional scoped
Count reads, disabled scope zeros, all scope bounds and six cold contexts.
[Root qualification](root-qualification.json) ties the result to native ELF
`7438bf29233a25b846921376b61f1e9e9a932b95b77206b10bede886b41b483c` and records
the transition example and attribution limits. [Raw completed stdout prefix](completed-prefix/ps2client.stdout),
[sample file](completed-prefix/closeout-samples.txt), [normalized cold records](completed-prefix/normalized.log)
and [normalization proof](completed-prefix/freeze-proof.json) are preserved.
Only transport CR/LF are removed; no field/value repair or missing-row invention.

The verified same ELF's source/native/emulator and twelve parser controls remain
in the parent preparation archive. Physical serving client PID6016 is left
active after completion; archived output is an immutable completed prefix,
not a terminal-client-exit claim. Physical TV confirmation remains pending.

The next discriminating probe should partition **remaining game update** from
**render preparation/submission and existing waits**, in this same driving
window. The residual is not automatically renderer-only: it also contains other
logic, separately called vehicle effects, scheduling and waits. Then measure
the largest identified interval, potentially pairing it with actual clipping/
culling/package counts. Do not remove correct physics substeps to chase FPS.
