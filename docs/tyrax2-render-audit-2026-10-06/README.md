# Full-city daylight render audit

The private two-axis PS2 observer partitions the full-city daylight render phase
by game stage and engine work type, while retaining whole-loop accounting,
existing wait ownership and same-ELF observer controls.

Native ELF SHA-256 is
`d686eda2e78d488fd3c7dada64821a6663825c2f28aa566776150de245c16b00`;
launch source head is `6da8b74ad`. Native binding, 5400 owned software-emulator
loops, 16 parser rejection controls and 5400 physical loops pass. The physical
[strict result](physical/strict-result.json) and
[qualification](physical/root-qualification.json) bind 384 ordinary raw rows,
256 active partitions, actual preceding-loop completion ownership and both
exclusive render sums. No production behavior or performance improvement ships.
The emulator raster was inspected; physical TV and audio remain unqualified.

## Physical attribution

The following are **two alternative partitions of the same duration**. Never
add a game-stage number to an engine-work number. They include instrumentation
and elapsed preemption, and are not retired EE instruction prices.

| Game stage | All 128 driving rows, mean ms | 67 instrumented over-budget rows, mean ms |
| --- | ---: | ---: |
| Objects | 6.226763 | 6.556099 |
| Terrain | 3.054511 | 3.374428 |
| Wheels and animation | 1.542476 | 1.279905 |
| Reflection | 1.245321 | 2.377761 |
| Particles and beams | 0.925011 | 1.025641 |
| Sky | 0.824712 | 0.820703 |
| Roads | 0.609492 | 0.844621 |
| Static batches | 0.356136 | 0.467059 |

| Engine work type | All driving rows, mean ms | Instrumented over-budget rows, mean ms |
| --- | ---: | ---: |
| Submission/recording | 3.760193 | 4.474578 |
| Work outside annotated engine scopes | 3.138561 | 3.227919 |
| Static Core preparation, excluding nested types | 2.643109 | 2.750968 |
| Bounds | 1.719706 | 1.801534 |
| Package dispatch | 1.324001 | 1.661633 |
| Packet construction | 1.168058 | 1.508458 |
| Object state | 1.025727 | 1.067502 |
| Geometry copying/expansion | 0.759355 | 1.014750 |
| Texture handling | 0.167091 | 0.168605 |
| Explicit cache flush scopes | 0.009706 | 0.009515 |
| EE clipper | 0 | 0 |

Driving non-pacing work averages17.221934 ms, of which15.715508 ms is render,
1.461287 ms pre-render update and0.037992 ms engine remainder. Explicit VIF and
GS waits average0.005920/0.001226 ms. The two GIF waits occur inside existing
presentation pacing in every enabled row and are not charged a second time.
Pacing averages8.312916 ms. These are observed diagnostic values, not a new
normal-game FPS verdict. The maximum pre-render update is10.376116 ms, another
elapsed spike that cannot be called pure game computation from this evidence.

Submission includes qbuffer sendPacket, Vif1Queue submit and flushRecording;
it is not just the DMA launch register write. Queue recording, chain closure,
copy/rebase and sealing remain inside these scopes; annotated packet building
and cache flush are excluded, as are existing waits and pacing. Interleaving
may charge submission of earlier collected geometry to the later game stage
where it executes. Reflection includes secondary view work; this is daylight,
not an effects-disabled full-night test. Zero EE clip input is a positive
fixture observation, not a claim that all scenes avoid the EE clipper.

Driving counter totals are11203 bag calls,3428013 bag input vertices,23599
package routes,576237 classified vertex inputs and13916 geometry-copy calls.
No texture uploads are observed in these measured rows. These are input/event
counts, not unique mesh vertices or final GS triangles. The338112 submission
source qwords are not a physical DMA byte count; rerecording may count again.
Texture chain qwords would omit REF pixel payload.

## Observer cost and interpretation

Parked controls retain the same ELF, buffers and normal physics with the camera
at the selected moving-chunk midpoint. Phase1 is Off40/On48/Off40; phase2 is
On40/Off48/On40. Work contrasts are **+1.752987/+1.402939 ms**, with outer spreads
0.009091/0.001981 ms. The instrumentation is materially expensive and can change
deadline crossings, presentation cadence, motion integration and which drive
pose is selected. The67 heavy rows cannot be compared as normal-game missed
frames to the preceding observer's20 rows. The selected pose is not the old
trial's pose, and a per-stage subtraction or a uniform scalar tax is rejected.

There are394346 additional render Count reads plus3328 balance reads in the
256 enabled raw rows. Balance makes13 reads per enabled row:3 markers plus
4 VIF and1 GS wait with two reads each. Covered GIF waits make no new clock
reads. Render scopes have two reads per distinct tag entry/exit pair, with
nested equal tags elided. No clocks are added inside per-vertex copy loops.
OFF retains branches, metadata, reset/copy work and the common data footprint.
The inherited `CLOSEOUTPHASE countReads=262` describes only the ordinary sampler.

Bound symbols are6144/420 bytes for ordinary samples/chunks,17408/136 for
balance samples/current row,24064/188 for render samples/current row, and240/132
for stage/work call arrays:48732 bytes in these named buffers alone. Common
footprint and compiler/cache effects are unpriced. The next-entry minus prior
whole-loop endpoint records inter-loop gaps, including sample postlude and
next-loop prelude: ON-minus-OFF gap contrasts are+0.001504/-0.000806 ms.
These are elapsed observations, not exact reset/copy instruction costs.

This gives useful priorities, not the final causal picture. Objects/terrain
are the largest game regions; submission/recording and Core preparation are
the largest annotated engine regions. Next use a **selective, coarse observer**
around the queue recording subpaths, with activation/mode/qword counters and
the same enclosing wait/pacing guards. Separately split Core preparation and
the explicit3.138561 ms unannotated render bucket. Price each selective mode
before testing an optimization. The present dense profiler should not become
production telemetry or be used to promise stable60.

## Pacing correction and rejected first build

[Rejected v1](rejected-v1/emulator/completed/runtime-emulator.log) completed with
`valid=0`: a GIF leaf wait made by flipBuffers while pacing was already excluded
consumed that exclusion. The new observer detected the overlap; strict parsing
rejected the result. It was never deployed to PS2. Preserve its source, native
identity and runtime rather than reusing its timing.

The qualified v2 starts exclusion at the existing presentation t0 before
flipBuffers. Calls inside pacing are counted separately and receive no nested
leaf clock/span. All256 active physical rows witness exactly two covered GIF
calls. Neither an SDK wait nor presentation behavior is changed.

This also corrects the earlier loop-balance observer: its two GIF waits were
inside pacing but timed again as explicit waits (about0.001743 ms mean in that
historical drive). Its claim that every category was disjoint was too strong.
Keep that immutable archive unchanged; use this follow-up for corrected
accounting. The render-dominance lead remains, but the earlier exact exclusive
render number subtracts that tiny overlapped interval.

The inherited coverage reviewer in v2 still prints a15872-byte v1 buffer
descriptor. The actual linked symbol and native proof are17408 bytes;
[supplemental correction](qualified-v2/vehicle/v2-correction-review.json)
explicitly resolves the stale descriptor and pacing coverage without rewriting
the launch-pinned reviewer. Source snapshots and scripts are archived in
`qualified-v2/` and `rejected-v1/`; preparation v1 is followed by the archived
v2 source changes. Paths to shared native mirrors are historical provenance,
not a claim those caches still contain the old objects.

Raw physical stdout is frozen through successful completion. Normalization
removes transport CR/LF only and extracts known records; the strict parser
checks identities independently. A live client is not claimed to have exited.
`inventory.json` pins every archived file except itself.
