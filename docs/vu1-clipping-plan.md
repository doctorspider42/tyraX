# Moving StaPip clipping from the EE to VU1 — design & plan

Developer design doc (not a user guide). Status: **planned, not started**.
Owner of the idea: upstream's own TODO in `stapip_clipper.hpp` ("clipping
algorithm should be moved to VU1 and 'AsIs' VU1 program should be renamed to
'Clip' - I don't want to do it now, too much time").

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
- **M1 — skeleton.** `clip_c` program that only does transform + guard-band
  ADC accept (no actual clipping yet — behaves like cull), wired behind a
  hidden third clipping mode (`"clipping": "vu1"` in project.json, no UI).
  Gate: pixel-identical to "fast" on a no-texture scene, no VRAM/ADC
  corruption on SW renderer + hardware.
- **M2 — real clipping, c variant.** Near-plane exact + guard-band X/Y
  Sutherland–Hodgman with color interpolation, NLOOP patching. Gate:
  pixel-compare vs "precise" baseline screenshots at fixed camera angles
  (SW renderer), clipbench PERF shows clip_avg ≈ 0 EE and pre-endFrame
  ≈ 24 ms.
- **M3 — remaining variants.** tc (ST interpolation), d/td (normals +
  lighting), spot-light evaluation in the clip programs; delete the EE
  `addSpotToColor` hack. Gate: flashlight scene pixel-compare, fog scene
  (XYZF2 fog coefficient must survive clipping — interpolate w-based fog
  like position).
- **M4 — flip the preference.** "Precise clipping" routes to the VU1 path;
  EE clipper stays available behind an env/config escape hatch for one
  release, then `StaPipClipper`/`PlanesClipAlgorithm` are deleted. Update
  the preferences tooltip (no more "costs EE time") and the terrain-detail
  hint (big triangles near the camera become cheap — coarser grids are
  viable). Gate: showcase + clipbench + one textured/fogged/flashlit scene
  all pixel-match and hold ≥ the old FPS on hardware.
- **M5 — companion work (independent, do in parallel):** packager package
  array pooling (per-frame allocation today). Target: clipbench pre-endFrame
  < 20 ms ⇒ 50 FPS vsync-locked on the 98k benchmark.

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
