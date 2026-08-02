# Stage 2: true skeletal animation runtime ("the 2002 flex")

Follow-up to PR #12 (animated models stage 1: .glb baked to morph frames).
Read `docs/animated-models.md` first (and, for the stage-1 write-up, entry (25)
of the retired `PROGRESS.md` from git history — see `docs/backlog.md`); this plan
assumes stage 1 is merged.

## Goal

Replace the baked-morph-frame *runtime* with a real skeletal one - bone
keyframe tracks, EE pose evaluation with crossfade blending, matrix-palette
skinning - while keeping the entire authoring surface (glb import, named
clips, Properties fields, flow-graph nodes, script API) byte-for-byte
unchanged. Only the backend swaps.

Wins over stage 1:
- **RAM: ~10-50x less** - bone tracks instead of full vertex snapshots
  (stage 1: verts x frames x 48 B; stage 2: bones x keys x ~16 B + one mesh)
- **clip blending** - crossfade between clips, no more pose popping
- **no 12 fps sampling cap** - keys play at authored fidelity
- per-instance pose divergence for crowds comes free (poses are computed,
  not stored)

## What already exists (do not rebuild)

- `src/glbparser.cpp` parses everything a skeletal runtime needs: node
  hierarchy, skins + inverse bind matrices, JOINTS_0/WEIGHTS_0, per-channel
  keyframe tracks (times + values, slerp already implemented). The stage-1
  baker consumes this data on the PC; stage 2 mostly means *serializing it
  instead of sampling it*.
- `.tanm` writer/loader pair (`writeTanm` / engine `TanmLoader`) shows the
  binary-format conventions (LE, fixed-size names, whole-file read - fseek
  is unreliable on PS2 host fs).
- The generated game's dynamic pass (`updateAndRenderAnimObjects`,
  `setupAnimObject`, `resolveClipIndex`, RuntimeObject anim fields) is the
  integration point - its *contract* stays, its internals change.

## Architecture

### 1. New `.tskl` format (editor writes, engine loads)

- skeleton: joint count, parent indices, inverse bind matrices, bind-pose
  local TRS
- mesh: one flat triangle list per material (PS2 has no index buffers):
  positions, normals, UVs in bind pose + per-vertex `u8 joints[4]`,
  `u8 weights[4]` (normalized to 255)
- clips: per joint per channel (T/R/S) keyframe tracks. Quantize: times
  f32, rotations 16-bit-per-component quats, translations/scales f32 (or
  16-bit normalized against a per-track range header if easy). Only emit
  channels that actually animate.
- keep the `.tanm` path alive for a while (see Migration).

### 2. Engine runtime (vendor/tyra fork additions - mind the fork rules:
`Modified by tyra-editor` markers, LF endings)

- `SkelModel` (loader output, shared): skeleton, mesh arrays, clip tracks.
- `SkelInstance` (per scene object): current pose palette (M4x4 x joints),
  playback state x2 for crossfade (clipA, clipB, fade 0..1, per-clip time),
  skinned output buffers (verts + normals, Vec4-aligned).
- **Pose evaluation (EE, per frame per visible instance):** sample tracks
  (binary search + lerp/slerp - port from glbparser), blend A/B during a
  crossfade (nlerp is fine), walk hierarchy parents-first, palette =
  global x IBM.
- **Skinning milestone A (EE):** transform verts + normals into the
  instance buffers, submit through the EXISTING DynPip path with
  `verticesFrom == verticesTo` (interpolation 0) - zero new VU1 code, all
  of stage 1's lighting/tint/texture plumbing reused. Bump/refresh bbox
  per frame (frustum culling must never be None - see the engine skill).
- **Skinning milestone B (VU1, optional, high risk):** matrix palette in VU
  memory, weights in the vertex stream, new VCL microprogram. READ
  `vcl_sml.i`'s failure history and the tyra-engine-dev pitfalls before
  attempting; budget it as its own session. Palette per batch limited by VU
  memory (~24-32 bones) - split materials by bone usage if needed.

### 3. Codegen / game template

- `updateAndRenderAnimObjects` evaluates poses + EE-skins before the DynPip
  submit; playback state machine moves from DynamicMeshAnimation to the
  instance (keep the same RuntimeObject fields; `animFinished` semantics
  identical: once for one-shots, every wrap for loops).
- Crossfade surfacing: new optional "Fade" number on the Play Animation
  flow node (seconds, 0 = instant) + `playAnimation(..., fade)` default
  param in script.hpp. Both backward compatible.

### 4. Editor

- import path unchanged; `refreshGenerated` writes `.tskl` (and stops
  writing `.tanm`) for skeletal-capable files
- viewport preview already CPU-skins via glbparser - no changes needed
- memory estimate in the import status: switch to the stage-2 formula

## Migration / coexistence

Per-model automatic choice, no user setting: files WITH a skin/armature ->
`.tskl`; files with only rigid node animation may stay on `.tanm` (rigid
animation needs no skinning and morph frames of a rigid mesh are cheap) -
or port rigid too if it simplifies codegen to one path. Decide during
implementation; document in docs/animated-models.md either way.

## Milestones (each independently shippable + verified)

1. **M1 - parity:** .tskl + loader + EE pose eval + EE skinning, single
   clip, no blending. Prove: same scratch scene as stage 1 renders
   identically (SW-renderer screenshot compare), RAM drop measured (debug
   profile free-RAM overlay), FPS >= stage 1 for 2-3 characters (PCSX2 SW
   renderer, 3+ samples).
2. **M2 - blending:** crossfade state machine + Fade param on the node +
   script API + docs update. Prove: a flow graph switching two clips shows
   no pose pop (frame-step screenshots across the fade).
3. **M3 - VU1 skinning (optional):** only after M1/M2 land; separate
   session; measure EE headroom first to confirm it is even the bottleneck.

## Perf/format budget notes

- EE skinning cost scales with verts x visible instances; the 98k-vertex
  benchmark history (engine skill) says the EE has headroom but measure,
  don't assume. Target: 3 characters x ~1.5k verts at full 50 FPS.
- Track sampling: keep per-track "last key" cursors per instance -
  animation time moves monotonically, so sampling is O(1) amortized, no
  binary search in the hot loop.
- PAL/NTSC: advance clip time by `g_frameDt` seconds (wall clock), same as
  stage 1's speed formula.

## Pitfalls carried over from stage 1 (learned the hard way)

- upstream `DynamicMeshAnimation::restart()` was buggy for sub-range
  sequences (fixed in the fork) - if stage 2 stops using it, the fix still
  matters for any remaining .tanm path
- texture links are per MeshMaterial id and COPIES get fresh random ids -
  every instance must re-link its textures (see `setupAnimObject`)
- `usePipeline` switching reallocates everything - keep the
  reinitVU1Programs swap pattern
- point lights are baked into static vertex colors; skeletal models keep
  the directional-light approximation unless someone adds runtime point
  lights to the dynamic pass (possible stretch goal: nearest-N point
  lights as extra VU1 dir lights)
