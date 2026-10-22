# A27 Ordinary Logging In The Moving Baseline

Source audit finds ordinary `TYRA_LOG` active in the bound A23..A26 lineage,
despite disabled frame/hardware profilers. A26's physical log contains 698 VEH,
240 VEHCONTACT and 2822 TERRAINSTRIP messages across warmup/reloads/travel.
These totals do NOT establish timed-window cost. Debug's active branch uses
`stringstream` construction and output/flush; NDEBUG's no-op does not apply.
No previous accepted log-cost intervention in this lineage was found in the
investigation register. Earlier quiet generators DO disable the active macro;
the current derived lineage does not retain that property.
The source transition is visible in `tools/tyrax2-paint-game.py` (A11): it
copies a fresh production vendor engine, then installs accounting hooks but
does not copy the earlier quiet debug macro. A13 disables profiler defines
without disabling ordinary logs, and later derived fixtures inherit it.

A27 measures this baseline inconsistency before asserting a hardware ceiling.
Same ELF uses ordinary logging Off/quiet/Off in day and night, only during gas.
Warmup, parked state, warnings/errors/assertions and raw test witnesses remain.
Both arms use the original SDK automatic header close, normal clip consumers,
geometry/classification/ownership, real dt, roads and live reflections.
No new hot clocks or log counters. Outside-price delimiters prove ordinary
messages actually appear in baseline travel windows and none in quiet windows.
Inherited sparse byte and input oracles plus all 60 readbacks/platform must pass.

V1 native binding covers 477 files; its hidden emulator completes six trips
and all readbacks. It is REJECTED by the ordinary-output window gate: a start
marker at BEGIN of offset 1319 admits a final parked-frame file log in phase4.
No physical deployment or timing claim follows this failed qualification.
V2 moves the marker to END of parked offset1319; EE game/info/SIF work has
already finished there. Gas gating stays at offset1320, unchanged. Keep zero
ordinary output required, rather than accepting the contamination.
Emulator ordinary output uses host log.txt file opens, not console text;
require baseline file opens and zero quiet opens, plus saved file content/hash.
PS2 uses console text; require baseline vehicle/terrain messages and zero quiet
LOG messages. Reader includes four transport/window corruptions in addition
to the 13 inherited controls. Both platforms still require exact all-arm
main/env images, source/ELF binding, cold input and SDK byte/state checks.

V2 native build and source/mirror/ELF binding PASS (477 source files), ELF:
`8c113a189df779c1ec8eec886776f5994acce782340953c1a8c968886ab5b4c7`.
Fixture `F:/Projects/tyrax2-log-gate-20261011-v2` completes hidden PCSX2 and
editor-protocol physical PS2 deployment. Both complete all six trips and all
60 exact main/env images; ordered cold inputs and SDK header/state oracles pass.
The 17 reader rejection controls pass. Owned emulator/client processes were
stopped after completion; devices remain powered on.

Physical baseline/quiet/baseline mean work (ms): day 18.717232 / 17.663279 /
18.728804; night 20.882234 / 19.681002 / 20.805743. Candidate savings against
bracketing baselines are 1.059739 ms day and 1.162986 ms night; equal-distance
savings are 1.251112 / 1.257983 ms. The first-order gate passes, but entry
cadence is only 38.0168 Hz day / 34.5455 Hz night, NOT 60 fps. This is not
pure EE cycles, production promotion or reverse-order qualification.
Physical baseline windows contain 440/432/435/431 ordinary messages; quiet
windows contain zero. Baseline TERRAINSTRIP counts are 413/403/408/404.
Emulator baseline host-log opens are 660/671/646/645, quiet zero.

Archive `log-v2/` contains 639 files, native binding, tools, logs, CSV, all
readbacks and the actual emulator host log. Read-only archived reanalysis with
external outputs reproduces both reports byte-for-byte. Emulator report SHA256:
`c8694621d11b35c3a5d09a101498ec333b9a36001336bdf85b3dc4b9a366be30`;
physical: `af522f0748fb1dee8dbcd0c647786af6c24dc0990202c641ab4ffd353cc78074`.
Ordinary silence is not observer-free: the terrain triangle census outside
TYRA_LOG and inherited periodic raw reports remain. Future causal fixes should
use the same quiet policy in BOTH arms, not subtract different ELF timings.

User's active goal requires 60 fps day with roads/live reflections (night
preferred), or hard workload-preserving impossibility evidence; tiny gains
do not complete it. Check Codex remaining allowance before major stages and
stop below60% remaining (last reading93%). User authorizes shutdown only when
goal finishes, allowance threshold is reached, or actual PS2 failure leaves
no further useful code/emulator work. No such condition currently applies.

Status: V2 emulator and physical first-order qualification PASS. This is not
proof that logging explains the missing frame budget. Prior A/B deltas still
compare the same log policy in both arms, but their absolute cadence must not
be called a clean no-ordinary-log ceiling. The closed A26 header result remains
no useful demonstrated gain under its own preserved workload.

## Next Source Finding: Camera Shake Drives Terrain LOD

The existing production path applies vehicle speed shake to cameraLookAt in
terrain_game.cpp before renderScene calls updateTerrainChunks(cameraLookAt.x,
cameraLookAt.z). updateTerrainChunks snaps this shaken focus using
floor(v / 32) * 32 + 16 on this map. The driven car stays at x=0, so millimetre
shake crosses the exact snapping boundary and switches the LOD focus between
-16 and +16 metres. This changes resident geometry and four neighbor edge
contracts; the budget of two limits per-frame rebuilds, not repeated churn.

Replaying the archived physical gas CSV through that exact snapping rule yields
camera-focus switches 53/55/55/53/54/55 across the six phases, versus six per
phase using car x/z. Camera lookX ranges approximately -0.0072..+0.0073 metres.
This explains a concrete mechanism for repeated builds, but does not yet price
their removal. Do not confuse existing half-chunk snapping with hysteresis:
there is no hysteresis at the snapped focus boundary. Existing view eviction
hysteresis does not prevent these LOD changes.

Next intervention: use the stable pre-shake camera target for terrain residency
and LOD, preserving shake in the rendered camera, normal LOD policy, neighbor
edge matching, roads and live reflections. Test same-ELF quiet/quiet A/B.
Changed transitional terrain rasters are expected; exact baseline/candidate
equality is not an appropriate gate for corrected LOD selection. Require exact
held-focus return controls, geometry/edge invariants and full moving captures.
No production change or performance claim follows this source finding yet.
