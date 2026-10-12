# troubleshoot/tyra-engine-dev-vu1.md

The host VU1 simulator/generator, the packet tap, the VU0 raytracer, VU1/DMA
cycle costs, and the `vclpp`/`vcl` authoring traps. The clip program family is in
`tyra-engine-dev-clip.md`. Symptom first.

## Before you hand-edit a `.vclpp`: run it on the host first

**Cause:** the editor carries a **VU1 simulator and a microprogram generator**
(`src/vuir|vuasm|vusim|vugen`, `docs/vu-framework.md`). Instead of a Docker build
plus a PCSX2 boot to find out what a program does, run it here.
```bash
tyrax-editor --vu-list <file.vclpp>   # expand the vclpp layer + disassemble
tyrax-editor --vu-check               # parse ALL of them, simulate, diff, budget
```
**Fix / what to know:**
- **All 25 `.vclpp` files the engine ships parse and run**, including the clip family
  and the VU0 raytracer kernel. `--vu-list` shows the program AFTER macro expansion —
  the first thing to check when it does something you did not write.
- The simulator reports two things `vcl` and `dvp-as` will not: a **Q clobber** (a
  `div`/`rsqrt` whose result is overwritten before anything read it — the gotcha
  `CalculateTyraEnvStq` documents) and a **quadword address outside VU1 data memory**,
  which the hardware silently wraps.
- What it does NOT model, deliberately: cycle timing, dual-issue pairing, branch
  delay slots (all `vcl`'s job) and the MAC/STATUS flag registers (`fsand`/`fmand`
  yield 0 and warn).
- **All fifteen StaPip programs** (five `as_is` + five `cull` + the five
  Sutherland-Hodgman `clip`) also have C++ descriptions that generate them; a
  generated program is proven **bit-identical** to the handwritten one by simulating
  both on randomized input, and each emits the same instruction COUNT. If you change
  one of those fifteen by hand, `--vu-check` starts failing — update the description
  in `vugen.cpp` too. (The `as_is` five and the two clip IMAGES, `clip_c` and
  `clip_tc`, are *adopted*: the files in `vendor/tyra` ARE the generated ones, plus a
  few hand-kept comment blocks — re-adopt by patching the emitter's diff onto them,
  not by overwriting. The `cull` family and the three unlinked clip references —
  `clip_d`, `clip_td`, `clip_tce` — are still the handwritten originals.) **To prove a
  microcode edit is output-preserving**, change the description first and run
  `--vu-check` against the OLD handwritten file (`git checkout` it back for the run):
  IDENTICAL there, then IDENTICAL again after the hand edit, is old = new. That caught
  the cull loops building a single-colour corner as `vf00 + singleColor`, which adds
  vf00.w = 1.0 to the ALPHA, so a copy of the raw colour was one alpha step off — the
  replicated copies keep the `+ vf00`.
- **`--vu-replay <projectDir>` re-runs a REAL console capture on the host** and diffs
  it against what the hardware produced (`examples/vu-lab` is the fixture; 36/36 GS
  vertices bit-identical). Two limits: only the LAST mesh of a chain can be replayed
  (the Debugger's flush picker gets a single-mesh flush), and a project with no
  flow-graph node compiles the devkit layer away entirely.
- **The simulator rounds toward zero, because VU1 does.** An x86 rounds to
  nearest-even; that difference shows as one or two units in the last place of the
  24-bit Z (the coordinate scaled by 8388607.5). Found by replaying a console capture.
  If you add arithmetic to `vusim`, do not compute outside `run()`'s rounding scope.
- **Nothing in `vendor/tyra` is generated yet.** `--vu-emit` writes to a directory you
  name on purpose: adopting generated microcode needs the full Docker + hardware pass.

## The VU1 packet tap (`src/renderer/3d/pipeline/static/core/stapip_vu_tap.*`)

**Cause:** the editor can ask for one VU1 DMA chain and decode it (docs/devkit.md,
PROGRESS 195).
**Fix:** the engine side is deliberately tiny — a **null function pointer**
(`Tyra::g_vuPacketHook`) and the branch that tests it in
`StaPipQBufferRenderer::sendPacket()`, i.e. once per bag flush, never per vertex.
The packet tap names the program each mesh actually runs (`program @<addr>`, matched
against the cache order): `vclab`'s twelve meshes all run `programs[1]`, so passing 2
instead of `count` frees ~1600 words and the scene still boots. The
capture itself lives in the generated game's devkit TU. **The second hook**
(`g_vuMemHook`) is called after the send, once VIF1 and the microprogram are idle,
with all of VU1 data memory — that is how the editor reads what the program PRODUCED.
It costs a pipeline stall, so the devkit installs it for one frame and uninstalls
itself from inside the callback. VU1 memory holds only the **last** MVP uploaded, and
**one flush carries several meshes in one chain** — pairing an input block with an
output packet needs a single-bag flush plus the object-data chain (PROGRESS 196). **The
rule that bites**: the pipeline sends vertex arrays **by reference** — a
`ref`/`refs`/`refe` DMA tag whose `qwc` counts quadwords at *another* address, with the
tag itself being ONE quadword. So (1) any chain walker advances by 1 for those tags
and `1 + qwc` only for inline `cnt`/`next`, or it decodes data as tags; and (2)
anything that wants the geometry must dereference **on the EE**, while those addresses
are live.

## `vclpp` / `vcl` authoring traps

**Cause:** the VU preprocessor and assemblers have silent failure modes.
**Fix:**
- A `;` comment line INSIDE a `#macro` body makes vclpp SILENTLY swallow the whole
  macro — every call site expands to nothing, the program builds fine and just misses
  your code (document ABOVE the `#macro` line instead; the existing `;// TyraX` notes
  sit there). Commit `93a7657` (2026-07-14) did this to `PerformClipCheck` in
  `vcl_sml.i` and silently removed the frustum clip check from `mcpip_cull` until
  2026-08-04 — the tell is a program ~27 instructions shorter in `.vsm`. After touching
  any `.i` macro, re-expand `mcpip_cull_vu1.vclpp` in the toolchain image and
  `grep -c clipw` the output. vclpp does not expand nested
  macro calls (a macro invoking another macro leaves mangled text that `vcl` rejects
  with "can't find instruction" — inline the callee by hand). And vclpp expands
  `#define`s only ONE level — an alias define (`#define A B` where B is another
  define) reaches dvp-as unresolved ("unresolved expression"), so VU1-side defines must
  be literals. Related: `[..]` is vclpp's **register-array index** (`t_lightMatrix[0]`),
  so a field suffix like `[w]` on a macro parameter breaks expansion the same silent
  way. When VU1 output looks wrong,
  `docker exec <proj>-compiler-1 cat /tyra/engine/obj/.../<prog>.o.vcl` shows exactly
  what vclpp produced.
- Three VCL-proper traps (paid for by the VU0 rt kernel): symbolic register names that
  collide with VU special registers OR instruction mnemonics (case-insensitively) are
  rejected — don't name a register `r`, `q`, `i`, `p`, `acc`, or anything like
  `mAx`/`sub` ("can't use r as a name" / "invalid name for register"); a broadcast
  field selector is only legal on the SECOND source operand — `max.x m, vf00, v[y]`
  works, `max.x m, v[y], vf00[x]` fails with a misleading "used before set"; and
  `ERROR: no opt table .. something failed making table .. for <label>` means the
  REGISTER ALLOCATOR ran out (31 VF ceiling), not a syntax problem — shrink the live
  set (reload fixed-address parameters at their use sites with `lq`, lane-pack related
  scalar fold state into one register — e.g. `add.x/y/z reg, vf00, src[x]`, fold once
  as a vector).
- **`begin:` in every pipeline program is a LOOP** (`b begin` at the bottom, one
  iteration per batch), so anything loaded above it must stay live through the whole
  body — and openvcl reuses those registers per vertex, storing garbage GIF tags on
  every batch after the first (50 FPS of missing terrain, no assertion). **Load
  per-batch values next to the store that reads them, inside the loop** — the nine
  `*_c` / `*_tc` / `*_tce` / `*_td` programs now do. The check
  that finds this class is described in `docs/toolchain-image.md`, "A miscompile this
  uncovered".
- **A `GIF A+D` giftag whose NLOOP undercounts its register writes stalls the GIF
  forever** — the stray qword parses as a new giftag with a garbage NLOOP. Symptom:
  the game hangs on the loading screen (spinning in `draw_wait_finish()`), no assert,
  clean log. Count the qwords after every PACK_GIFTAG edit.
- **Do not treat REGLIST as a generic replacement for PACKED A+D state.** Its register
  selectors are four bits wide; TEST, TEX1 and ALPHA are not directly selectable (TEX0
  is). A 2026-09-21 `cull_tc` spike combined four unchanged A+D payloads behind one
  four-loop PACKED tag, shrinking the header from 9 to 6 QW and the VU program from
  345 to 342 instructions. PCSX2 rendered it correctly, but physical hardware stalled
  on the first gameplay frame and never completed the benchmark, so it was reverted.
  Also resize the static `packet2_t` when adding uploads: two CNT/data pairs plus END
  need 5 QW, not the original 3.
- **Reuse complete GS state; do not compact its tag syntax.** The accepted hardware
  path keeps all state GIFtags one-loop, emits TEST/TEX1/TEX0/ALPHA on package zero of
  a material bag, and lets later cull/as-is TC/TD packages emit only PRIM. Bit 15 of
  their non-clip count word carries the explicit choice; clip programs cannot use it
  because bits 10..15 are the active-plane mask. Reset the decision at every
  bag/material boundary. The physical garage saved 1 864-1 984 GS QW/frame but only
  0.04-0.20 ms.

## Count VU words with `nm`, not by counting rows in the `.vsm`

**Cause:** the uploader sizes each program from `<name>_CodeEnd - <name>_CodeStart`
(`VU1Program::calculateProgramSize`, rounded up to even — MPG uploads 64-bit pairs),
and row counts run 2-6 high per program. On the resident ten that is a ~7-word error.
`nm` currently says **SCE 2028, openvcl 1996** against a 2042 ceiling. And words are
NOT the perf metric — the cost words do not show is FMAC read-after-write stalls.
**Fix:**
```bash
docker run --rm -v "tyra-engine-<hash>:/tyra:ro" tyrax-toolchain:local sh -c 'mips64r5900el-ps2-elf-nm /tyra/engine/obj/renderer/3d/pipeline/static/core/programs/clip/stapip_clip_c_vu1.o | grep -i Code'
```
The volume name is in the project's `docker-compose.yml`.

## What a VU1 cycle and a DMA cache flush actually cost (2026-09-15)

**Cause:** two physical-PS2 experiments, both reverted, priced the next two
candidate directions.
**Fix:** **VU1 arithmetic is worth 0.0768 ms of frame time per cycle per triangle** in
the Motor District garage view, and 97% of it surfaces as VIF1 DMA wait (adding twelve
cycles per triangle moved render submission 40.830 -> 41.752 ms and the wait bucket
6.794 -> 7.688 ms). Read a program's real cost out of the generated assembler — the
lines between the loop label and the loop branch in the native cache's `.o.vsm` are
**cycles per triangle**. Under the shipped `openvcl`: `cull_tc` 133, `cull_c` 130,
`cull_tce` 109, `cull_td` 107, `clip_tc` 269. **AIM A VU1 EXPERIMENT AT THE PROGRAM THE
SCENE ACTUALLY RUNS** — a generated game attaches a lighting bag only to dynamically
lit objects, so ordinary static geometry has `lighting == nullptr` and selects the
COLOUR programs; `getCullProgramByParams(isLightingEnabled, isTextureEnabled)` is the
whole decision. **`dma_channel_send_packet2(p, ch, true)` is `FlushCache(0)`** — syscall
100, a write-back invalidate of the entire 8 KiB data cache — then
`dma_channel_send_chain`. At 131.65 submissions per garage-day frame the whole bracket
is 2.358 ms, 17.9 us per call. Adding redundant flushes measures the marginal cost:
0.61 ms for the first, 0.437 ms for each further one on an already-clean cache.
**Suppressing the flush hangs the console** (an arm passing `flush_cache = false`
wedged ps2link until a physical Reset). **ps2sdk offers no cheap range write-back, and
that is still not a reason to fork it**: `SyncDCache` is not the range operation its
signature suggests — `_SyncDCache` loops over **all 128 cache indices in both ways**,
issuing `sync` before and after every `cache` instruction, and only then compares tags
against the range, which is why the earlier experiment that synchronised each DMA REF
range separately came out slower than the baseline. Full numbers and the reproduction
recipe: `docs/vu1-and-dma-cache-cost.md`.

## The VU0 micromode ray tracer

**Cause:** raytraced mirror reflections, `docs/raytraced-reflections.md`.
**Fix:** `renderer/rt/vu0_raytracer.{hpp,cpp}` + `vu0_rt_kernel.vclpp`, exported by
the `<tyra>` umbrella header. It is the ONLY VU0 microprogram in the codebase — built
through the same vclpp/vcl/dvp-as pipeline as the VU1 programs but uploaded by the EE
to VU0 micro memory (0x11000000) and kicked with `vcallms 0` per image row,
params/results through VU0 data memory (0x11004000), sync by polling VPU STAT bit 0
(`cfc2 $29`). VU0-honest kernel: no XGKICK/EFU/xtop; branchless nearest-hit via
saturation step masks (`clamp(x*1e38,0,1)` — VU floats saturate, no inf/nan). Runs
synchronously: macro-mode COP2 (Vec4/M4x4) shares VU0's register file, so the tracer
never overlaps engine math, and clobbering VF01+ between kicks is safe because every
macro op reloads its operands.
