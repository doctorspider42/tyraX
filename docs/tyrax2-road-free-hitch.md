# Road-free location-specific hitch

Follow-up to [reflection isolation](tyrax2-road-free-reflections.md).
The nine physical drives all hit their worst frame near z=-53.593, including
both reflection bypasses. Update stayed near 1.1 ms. Attribution is not yet known.

`tools/tyrax2-hitch-probe.py <new-fixture> <source-project>` generates three
identical normal-reflection drives (control / locally scoped / control), ending
at z=-40. Only phase 1 and z in [-58,-49] record elapsed scopes. Existing render
stage boundaries are reused WITHOUT enabling the synchronous cost profiler.
Nested terrain builds, object geometry/LOD, texture acquisition and frame
begin/end are recorded. Intervals are inclusive and must not be added together.
The bounded 8192-event RAM buffer is exported only after the driving phase;
overflow or export failure invalidates the run. No added GPU fences or per-frame
file I/O. Common buffer footprint and scope-gate overhead remain unpriced.

Native source/mirror/ELF binding, emulator smoke, then PS2 confirmation are
required. This is a private diagnostic, not a production optimization.

Optional `--preload` acquires the two observed masks (`s0_3_4.png`,
`s0_0_4.png`) at phase 1 offset 1100, before sampling starts, and releases those
extra references before phase 2 scene reload. This is a route-specific causal
intervention, NOT a shipping fix or a scalable streaming policy. The analyzer
compares scope events at the closest frame to z=-53.593 even if preloading moves
the maximum elsewhere.

## Localization result (2026-10-08)

Fixture `F:/Projects/tyrax2-roadfree-hitch-20261008-v1`, ELF SHA-256
`3014d0a001237a78184ba1779df6f35f8e5f9c0080d261211b418c816c32f1f3`.
Native source/mirror/ELF agreement: 467 files. Emulator and physical PS2 each
completed all three phases. Physical rows: 352/351/350; 3517 local events,
zero drops (196608-byte event buffer). Parser rejects five malformed controls.

Physical maximum non-pacing elapsed: 41.960 / 41.712 / 52.241 ms, all near
z=-53.593. The middle phase's inclusive scopes:

| Scope | Elapsed ms |
|---|---:|
| Terrain | 33.255 |
| updateTerrainChunks | 30.567 |
| buildTerrainChunk, slot 22 | 14.718 |
| acquireTexture inside slot 22 | 14.220 |
| buildTerrainChunk, slot 23 | 15.824 |
| acquireTexture inside slot 23 | 15.348 |
| renderTerrain | 2.682 |
| endFrame (including pacing) | 8.459 |

Two texture acquisitions account for 29.568 ms. The code path is
`renderScene -> updateTerrainChunks -> buildTerrainChunk -> acquireTexture`:
the newly needed ground-shadow masks are loaded in the active render frame.
The host log identifies `gshadow/s0_3_4.png` and `gshadow/s0_0_4.png`; both are
128x128 PNGs. The acquisition scope includes existence checking, file loading,
PNG decoding, repository insertion and initial texture use. It does NOT isolate
network, decode, upload or wait costs. Small compressed file size is not a
measurement of latency or decoded-memory footprint.

The `acquireTexture` comment says its upload is outside the frame, but this
caller violates that assumption. Do not bypass synchronization to disguise it.
The emulator finds the same chain, with 11.550 ms combined acquisition; those
timings are not physical PS2 prices. Scopes are inclusive, not an additive
EE/VU/GS budget. This identifies one hitch, not all turning/reflection costs.

The first preload fixture (`...hitch-preload-20261008-v1`) mistakenly pinned
`s0_3_3.png` and `s0_4_3.png`, which were already loaded during warmup. It did
not remove the hitch. Full chronological logs, rather than their earlier tail,
identify the actual newly loaded row-4 masks. Preserve this failed intervention;
the corrected trial uses a fresh v2 fixture.

## Corrected causal intervention

Fixture `F:/Projects/tyrax2-roadfree-hitch-preload-20261008-v2`, ELF SHA-256
`66dec4dd85972f85d9ef2783e1b6299541a2ca7344603b91056851be558dead4`.
Both native binding (467 files) and all three emulator/physical phases pass.
Physical rows: 352/353/351; 3689 local events, zero drops. The analyzer also
rejects all five malformed controls.

| Physical PS2 | Control before | Preload | Control after |
|---|---:|---:|---:|
| Non-pacing elapsed at z=-53.593, ms | 41.018 | 12.311 | 40.788 |
| Maximum moving non-pacing elapsed, ms | 41.018 | 16.714 | 40.788 |
| Moving entry periods over 25 ms | 1 | 1 | 2 |

At the target frame, the two cache-hit acquisitions total 4043 Count ticks
(0.01371 ms), terrain residency/build drops to 0.956 ms, and the terrain stage
is 3.905 ms. Same scene, geometry, shadows and reflection policy; no rendering
pass was removed. The control after releasing the extra references restores
the hitch. This supports moving preparation of these assets out of the render
hot path. It does not price file I/O separately from decoding/initial use.

The preload arm still has one 33.37 ms entry period near z=-63.567, following
a 16.714 ms non-pacing frame. Thus this is NOT a claim of perfectly stable
60 fps, nor a fix for turning reflections. Emulator smoke passed; screenshots
are not a pixel-equivalence oracle.

At this causal-intervention stage no production fix was enabled. The next implementation had to define bounded
ground-shadow residency and preparation during scene/layer loading, account
for decoded memory, and preserve texture ownership through chunk reuse and
scene changes. Merely moving synchronous loads from render to update moves
the hitch; preloading every texture without a budget is not a general solution.
The route-specific two-name intervention is evidence, not shipping policy.

Evidence: [dated archive](tyrax2-road-free-hitch-2026-10-08/README.md).
The source map remains unchanged. All owned host/emulator processes were
stopped; final network reset returned zero with a ten-second settle (screen
not independently verified). No shutdown was requested or performed here.

## Production integration (1.173.3)

The editor now generates scene-owned references for complete ground-shadow
sets whose accounted decoded cost is at most 512 KiB. Codegen estimates
Indexed4 pixels + 64-byte palette + 1024 bytes of per-mask overhead. Runtime
checks loaded format/dimensions and the cumulative charge, rolling back pins
on a mismatch. Larger scenes keep the existing per-chunk streaming path.
This is a bounded small-scene optimization, not a universal asynchronous loader.

`resetTerrainChunks` releases old chunk references and then the scene pins,
including before its terrain-disabled return. Preparation runs only during
scene loading, before chunk rendering. Ordinary chunk references and VRAM
eviction remain unchanged. No masks or render passes are removed. Both generated
game headers carry the ownership fields. No settings or project-format change.

The road-free map qualifies with 18 masks, 167040 accounted bytes (163.125 KiB).
Production A/B/A fixture: `F:/Projects/tyrax2-ground-shadow-pins-20261008-v1`,
ELF `749339f9cb589bb92028416c293c8b3033cbb23334895271a8816c9088919c40`.
Only the scene-preparation call is gated off/on/off; no filenames are hardcoded.
Physical target-frame non-pacing elapsed is **40.709 / 12.276 / 41.412 ms**
(see archived CSV for full precision). Scene pins are **0 / 18 / 0**.
Both PS2 and emulator complete all phases with valid records and zero event
drops. The pinned arm still has one long entry period elsewhere on the route.

Windows editor build and native PS2 build pass. The host ASan/UBSan test
`verify-ground-shadow-pins.py` compiles the actual reset/preload template bodies
and checks cache-hit reuse, scene changes, terrain disabled, absent masks,
invalid formats, over-budget rejection, runtime rollback and the no-ground
compile path. Empty/no-terrain codegen also passes. Native Linux editor build
was not run; production changes are shared C++ with no platform branches.

Final default-on fixture: `F:/Projects/tyrax2-ground-shadow-final-20261008-v1`,
ELF `e3544513f54c85895833fe02fe25a05605a97611a006b81b64bb6eb197498afb`.
`--all-pins` leaves the production preparation call unconditional and extends
all three drives to z>=108. Analyze with `--full`. Both PS2 and emulator finish
2505 sampled rows, with 18 pins after every scene reload. Native binding again
covers 467 files; five analyzer negative controls pass. Physical moving results:

| Phase | Moving rows | Loop Hz | Mean non-pacing ms | p95 ms | Max ms | Periods >25 ms |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 715 | 59.94 | 9.79 | 12.61 | 15.48 | 0 |
| 1 (local scopes) | 715 | 59.94 | 9.93 | 12.75 | 15.60 | 0 |
| 2 | 715 | 59.94 | 9.78 | 12.60 | 15.51 | 0 |

One presentation per recorded loop. This qualifies this road-free daytime
straight-drive route with normal reflections, not arbitrary camera rotation,
night driving or the original road-heavy map. No broad rendering reduction
or devkit was enabled. Common sampler footprint remains unpriced; these are
not pure EE execution times. Emulator moving captures for phases 1 and 2
are recorded with read-only PINE offsets; no pixel-equivalence claim.

Production evidence is in `ground-pins-ab` and `ground-pins-final` inside the
dated archive. The editor executable was rebuilt to 1.173.3. Existing projects
get the fix by rebuilding with that executable; their authored files need no edit.
