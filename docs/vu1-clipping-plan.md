# Moving StaPip clipping from the EE to VU1 — design & plan

Developer design doc (not a user guide). Status: **M0–M4 done** — VU1
clipping is the **default** for new projects and the first choice in the
Preferences combo; all five clip program variants live (c / d / tc / td +
tce for reflective materials), verified pixel-identical to the EE precise
clipper in PCSX2 SW renderer. M4 was originally gated on a real-PS2 perf
pass, but the owner flipped it early (2026-07-14) after in-situ testing:
the EE clipper showed a close-up/screen-edge corruption on reflective
geometry that the VU1 path does not have, plus the known EE cost. The EE
clipper is NOT deleted — it stays selectable in Preferences ("Precise
clipping on EE (legacy)") and pre-M4 projects keep it on load (a missing
`clipping` key still reads as `precise`). The hardware perf pass (clipbench
PERF + ADC check on a real console) is still outstanding — do it before
deleting the EE path. Owner of the idea: upstream's own TODO in
`stapip_clipper.hpp` ("clipping algorithm should be moved to VU1 and 'AsIs'
VU1 program should be renamed to 'Clip' - I don't want to do it now, too
much time").

## Why — measured numbers (real PS2, 2026-07-11)

Benchmark: fresh `fpp` project, 128×128 terrain at detail 128 (~98k verts),
sky dome, FPP camera auto-spinning at eye height. COP0 timers around
`clipper.clip()` plus loop-entry / pre-`endFrame` markers; PERF line per 100
frames over `host:`; 4 runs × ~2400 frames.

| mode    | vsync | frame_avg          | pre-endFrame (EE) | `clipper.clip` EE     | packages clip+cull |
|---------|-------|--------------------|-------------------|-----------------------|--------------------|
| precise | on    | 40.0 ms (25 FPS)   | —                 | 8.6–9.8 ms (max 11.6) | 181 + 175          |
| fast    | on    | 40.0 ms (25 FPS)   | —                 | 0.4 ms (sky dome)     | 6 + 841            |
| precise | off   | 33.8 ms (~30 FPS)  | ~33 ms            | 9.4 ms avg            | ~356               |
| fast    | off   | 27.2 ms (~37 FPS)  | ~26.5 ms          | 0.4 ms                | 847                |

Conclusions that shape the design:

- The frame is **100% EE-bound** (endFrame = 0.5 ms with vsync off; GS and
  VU1 are idle). Moving work from the EE to VU1 is nearly free wall-clock.
- The EE clipper costs **~1.3 µs (~380 cycles) per vertex** of a
  frustum-crossing package; on this scene ~52% of surviving packages cross.
- "Fast" is not a free alternative: without per-package exact classification
  it submits 2.4× the packages (841 vs 356 — nearly the whole terrain every
  frame) and still runs ~26.5 ms EE, plus it drops big near triangles.
- VU1 clipping alone ⇒ ~24.4 ms EE on this scene: still 25 FPS vsync-locked.
  The realistic wins are: ~9–11 ms EE headroom for game logic, borderline
  scenes flipping 25→50, ~30→41 FPS with vsync off, and removing the
  fast/precise quality-vs-speed dilemma. To get *this* benchmark under 20 ms
  it must ship together with packager pooling (see Companion work).
- PCSX2 undercounts the clip cost by ~15–20% — always confirm on hardware.

## Current architecture (what moves)

Precise path today (`fullClipChecks=true`, requires `frustumCulling=Precise`):

1. `StaPipPackager` classifies each package exactly against the frustum:
   fully-outside → dropped, fully-inside → `cull()` (full-occupancy qbuffer,
   VU1 cull program, no vertex copies on the EE), crossing → `clip()` at
   ≤ maxVertCount/3 occupancy.
2. `StaPipClipper::clip` (EE, all inside the measured 9.4 ms): spot-light
   injection into vertex colors, MVP transform of *every* vertex, outcode
   early-out, Sutherland–Hodgman vs 6 planes for actually-crossing triangles,
   perspective divide, rewrite of the whole qbuffer (pos/ST/color/normal).
3. `as_is` VU1 programs (c/d/tc/td) draw the pre-divided vertices as-is.

Codegen forces `fullClipChecks=true` regardless of the user preference for
the sky dome, animated objects (StaPip since PR #29), particles and hulls —
those all ride the EE clipper even in "fast" mode.

## Proposed design

Replace the `as_is` family with a **`clip` family** (same 4 variants:
c / d / tc / td) that receives raw object-space vertices + MVP exactly like
the cull programs, and per triangle:

1. Transform to clip space; compute a plane mask against the **near plane
   and a ±2×-screen guard band** (the same bound the cull programs already
   use via `clipw`).
2. All three verts inside the guard band → emit unchanged with ADC
   pass-through (the common case — cheap).
3. Crossing → Sutherland–Hodgman in VU memory. Near plane must be clipped
   exactly; X/Y only need clipping back inside the guard band, because the
   GS scissor trims pixels correctly as long as coordinates stay inside the
   4096-px raster window (the wrap bug only bites far beyond the guard
   band). Clipping to the guard band instead of the exact frustum cuts the
   worst case from 6 planes to ~3 and keeps microcode small.
4. Build the output GIF packet in VU memory with a patched NLOOP (a clipped
   triangle fans into up to 5 triangles), then XGKICK.

Kept as-is:

- **EE per-package exact classification stays.** It is what keeps the VU1
  vertex stream at 356 packages instead of 847 on the benchmark, and it is
  the routing point between cull and clip programs.
- Clip-classified packages keep the ≤ maxVertCount/3 occupancy (bounded
  output expansion) — the packager does not change.

Retired / simplified:

- `StaPipClipper` + `PlanesClipAlgorithm` EE path (StaPip) — delete after
  the escape-hatch release (see M4).
- The EE spot-light injection hack in `StaPipClipper` (`addSpotToColor`):
  the clip programs receive raw vertices, so they evaluate
  `CalculateTyraSpotLight` exactly like the cull programs do today.
- The editor's "Triangles" preference eventually collapses to a single
  correct mode (keep "fast" only if a measurable VU1-side win remains).

## Risks and prior art (read before writing any VCL)

- **`vcl_sml.i` history is mandatory reading.** Three guard-band attempts in
  the *cull* programs (I-register scale, STATUS-flag w test, DIV/Q constant)
  all corrupted ADC bits under VCL's register allocation. This plan is a
  different approach (a real clipper in a separate program, not a widened
  cull test), but the same register-pressure minefield applies. Budget for
  VCL fights; symptom of ADC corruption = stray smeared triangles at screen
  edges (SW renderer / hardware only — PCSX2 HW masks it).
- **VU1 micro memory is 16 KB** shared by the resident programs, which
  already grew with fog (XYZF2) and the spot light (PR #34). The clip
  program is the biggest one yet. The guard-band simplification (3 planes)
  exists mostly to fit; if it still doesn't fit, options are per-variant
  program swapping (costly: program uploads mid-frame) or dropping the d/c
  variants to a shared slower path.
- **Variable-length output** (NLOOP patching, double-buffer XTOP/XITOP
  handling) is the fiddliest VU1 part; get it right on the c variant first.
- No host compile for VU code — every iteration is a Docker game build;
  vclpp chokes on CRLF (LF enforced by .gitattributes under vendor/tyra).
- Judge correctness on PCSX2's **software renderer** and on the real
  console; the HW renderer masks raster-window wrap.

## Milestones

- **M0 — measurement. DONE 2026-07-11** (numbers above; reusable
  instrumented scene in `%TEMP%\tyra-editor-test\clipbench`, details in the
  `vu1-clipping-cost-measurement` project memory).
- **M1 — skeleton. DONE 2026-07-12.** Clip program family (cull clones at
  this stage) behind the hidden `"clipping": "vu1"` mode; clip-classified
  packages route to the clip programs with raw object-space vertices.
  Verified pixel-identical (0 diff) to both fast and precise on a
  no-texture scene, PCSX2 SW renderer. Fixed en route: subpackages smaller
  than maxVertCount/3 straddle the 1/3 bbox grid — classification now
  merges every overlapped part's bbox.
- **M2 — real clipping, c variant. DONE 2026-07-12.** Sutherland–Hodgman in
  a scratch area at the top of VU1 data memory, near plane first, fan
  triangulation, NLOOP patching. Verified pixel-identical (0 diff) to the
  EE precise clipper on a terrain-detail-8 scene where fast differs by 31%
  of pixels. Two traps recorded for posterity: `fcand` yields 0/1, not the
  masked bit pattern (no per-plane trivial reject); clipping X/Y at exactly
  ±w lands vertices on GS coordinate 4096.0 which wraps the 12.4 XYZ2
  field — the side planes cut at 0.9w (`VU1_CLIP_XY_BAND`).
- **M3 — remaining variants. DONE 2026-07-12.** tc interpolates STQ through
  the cuts (perspective-corrected with the position Q at emit, like cull);
  d/td evaluate directional lighting on the original vertices and
  interpolate the lit colors (Gouraud-equivalent; keeps the light registers
  out of the clipper); spot light evaluates on raw object-space verts in
  the c/tc programs before clipping (the EE `addSpotToColor` hack is dead
  code on this path — it gets deleted with the EE clipper in M4). The fog
  coefficient is computed from the interpolated clip-space w at emit, so
  fog survives clipping by construction. All four programs share one
  emitter (scratch polygon → fan) — separate inline fast paths overflowed
  the 16 KB VU1 micro memory (silently, in release builds). Clip package
  occupancy must stay a multiple of 3 (`StaPipCore::clipPackageSize`) or
  the vertex loop runs off the end of VU1 memory. Verified: full showcase
  scene (dome, terrain, textured boxes, models) 0 diff vs precise; a
  textured box straddling the camera 0.065% (LSB texel shifts on cut
  edges); skeletal-animation scene (d/td) renders correctly at 50 FPS.
- **M4 — flip the preference. DONE 2026-07-14** (flipped by owner decision
  ahead of the original hardware gate — the EE clipper corrupts close-up /
  screen-edge frames on reflective geometry, the VU1 path does not).
  Default clipping for new projects is `vu1`; the Preferences combo offers
  "Precise clipping on VU1 (default)" / "Precise clipping on EE (legacy)" /
  "Fast culling". Backward compatibility: a `.tyra` without a `clipping`
  key loads as `precise` (EE), so existing projects keep their behavior
  until the user opts in. Shipped together with the fifth clip variant
  **clip_tce** (`stapip_clip_tce_vu1.vclpp`): the env/matcap program that
  computes the sphere-map ST from the object-space normal *before* the
  Sutherland–Hodgman pass (so ST lerps through cuts like a regular texture
  coordinate), letting reflective materials run in VU1-clipping mode — the
  EE-computed-ST fallback in codegen is deleted. Micro-memory: cull_tce +
  clip_tce on top of the 8-program vu1 set measured 2162 instructions
  against the 2042 ceiling (2048 − the 6-instruction draw-finish helper).
  First attempt shipped a 9-program set (no cull_tce) with every env-bag
  package force-routed through clip_tce at clip occupancy — correct but
  ~5× slower on the env pass (14.2 ms vs 2.7 ms on the reflections
  showcase: 1/5-occupancy subpackages, copies, and the clip program's
  per-triangle work on VU1 that the EE then waits on). The shipped fix
  instead shrinks all five clip programs: the fan-triangulation emitter is
  ONE macro instance in a rotating 3-iteration loop (`fanEmitLoop`,
  corner pointer rotates srcBase → fanPtr → fanNext) instead of three
  inlined copies, freeing enough micro memory for the full 10-program vu1
  set (cull ×4 + clip ×4 + cull_tce + clip_tce) — env bags route exactly
  like any textured bag (in-frustum → cull_tce, crossing → clip_tce). The
  EE `StaPipClipper`/`PlanesClipAlgorithm` are NOT deleted yet — that
  waits for the still-outstanding hardware pass (clipbench PERF + the
  SW-renderer-vs-hardware ADC check on a real console).
- **M5 — companion work (independent, do in parallel). DONE 2026-07-15**
  (in PCSX2 terms). Package array pooling landed earlier (PROGRESS 79,
  worth ~2%); the real companion win was rebuilding the per-package frustum
  classification (PROGRESS 100): object-space planes computed once per bag +
  p-vertex/n-vertex AABB test per package, duplicate zero-guard-band
  re-check deleted, VU0 min/max bbox scan, memcpy qbuffer fills. On the 98k
  benchmark (PCSX2 SW renderer, vsync off, debug profile): precise mode
  47 → 73 FPS pixel-identical, and **this VU1-clipping mode 120 FPS** —
  2.55× the old precise baseline. The hardware half of the original M4
  gate (real-PS2 clipbench re-run + SW-vs-hardware ADC check) is still
  owed before the EE clipper can be deleted.
- **M6 — compact the triangle crossing test. DONE 2026-08-11.** The CLIP
  flag is a four-entry history, so the six per-triangle judgements are now
  consumed by two positional `fcand` reads rather than six individual reads
  plus four `ior`s. Four dead `mul.w` copies of the already-live guard were
  also removed from every clip program. The handwritten family and the C++
  description changed together. `--vu-check` proves all 15 generated static
  programs bit-identical to their handwritten twins across 60 randomized
  trials each; a Docker/Sony-VCL rebuild of `examples/showcase` reduced the
  exact ten-program resident set from 2040 to **2026 of 2042 slots** and
  produced a PS2 ELF successfully.
- **M7 — pack the matcap basis for VU1. DONE 2026-08-11.** The EE now
  transposes the camera right/up vectors and folds their `+0.5/-0.5` scales
  into three basis qwords. `CalculateTyraEnvStq` evaluates both dot products
  together in the accumulator, reducing the macro from 16 to 10 instructions
  per input vertex and its scratch set from four VF registers to two. The
  formula and packet size are unchanged. The exact Sony-VCL sizes fell by 18
  slots for `as_is_tce`, 18 for `cull_tce`, and 16 for `clip_tce`; the resident
  VU1-clipping set is now **1992 of 2042 slots**. The host validator stages a
  valid non-axis-aligned packed basis and kept all 15 handwritten/generated
  programs bit-identical across 60 trials; Docker rebuilt and linked
  `examples/showcase` successfully.
- **M8 — opt-in static-pipeline telemetry. DONE 2026-08-11.**
  `StaPipCore::setTelemetryEnabled` now exposes interval counters for final
  cull/clip/outside package and triangle routes, a 0–6 active-plane histogram,
  qbuffer flushes and VIF1/VU1 wait ticks, plus billboard/resident program-set
  swaps and their full drain/upload time. `takeTelemetry` returns and resets
  the interval. The disabled path retains the original first-partial-plane
  AABB early-out and performs no COP0 reads, so a release that does not opt in
  pays no diagnostic cost. This is the measurement base for active-plane
  clipping and program-set sorting rather than another temporary global.

## Verification protocol (every milestone)

1. Build via `--build`, run on PCSX2 **software renderer** → screenshot
   (`.claude/skills/tyra-testing/scripts/screenshot-window.ps1`), compare
   against the precise-mode baseline at the same fixed camera angles.
2. Re-run the clipbench PERF instrumentation (recipe in the
   `vu1-clipping-cost-measurement` memory) on the real PS2; worktree deploys
   need the MAIN checkout's ps2client on PATH.
3. Watch for the known failure signatures: vanishing near triangles (bad
   near clip), giant smeared polygons (raster-window wrap / ADC corruption),
   single-triangle notches at screen edges (guard-band misclassification).
