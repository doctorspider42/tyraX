# VU1 microprograms: tools, vclpp/VCL traps, micro-memory budget, program families

Read when: you edit any `.vclpp`/`.i`/VU-side `.h`, `vugen.cpp`, the clip/cull/as_is/billboard programs, `stapip_vu1_experiments.hpp`, `Path1::createProgramsCache`/`setProgramsCache`, or reason about VU1 cost or micro-memory headroom. Build plumbing for VU objects: build-and-toolchain.md.

## Run it on the host first
The editor has a VU1 simulator + generator (`src/vuir|vuasm|vusim|vugen`, docs/vu-framework.md):
```bash
tyrax-editor --vu-list <file.vclpp>   # expand the vclpp layer + disassemble
tyrax-editor --vu-check               # parse ALL, simulate, diff, budget
```
- Every engine `.vclpp` parses and runs (the skill used to say "25"; tree now has 32 incl. 7 `*_fold`/`*_envn` experiment variants), incl. clip family and VU0 kernel. `--vu-list` shows post-macro-expansion code — first thing to check when a program does something you didn't write.
- Simulator catches what `vcl`/`dvp-as` won't: **Q clobber** (div/rsqrt result overwritten unread — the `CalculateTyraEnvStq` gotcha) and **quadword address outside VU1 data memory** (hardware silently wraps).
- Not modelled: cycle timing, dual-issue, branch delay slots (vcl's job), MAC/STATUS flags (`fsand`/`fmand` yield 0 and warn — flag-branching programs aren't authoritatively simulated).
- **Rounds toward zero like VU1** (x86 = nearest-even; shows as 1-2 ULP in 24-bit Z, scale 8388607.5; found by replaying a capture). New `vusim` arithmetic stays inside `run()`'s rounding scope.
- **All fifteen StaPip programs** (5 `as_is` + 5 `cull` + 5 `clip`) have C++ descriptions in `vugen.cpp`; proven bit-identical to the handwritten files on randomized input, same instruction COUNT. Hand-edit one → `--vu-check` fails until the description is updated (or decide which is right). `as_is` five and the two clip IMAGES (`clip_c`, `clip_tc`) are *adopted* (files ARE generated output + a few hand-kept comment blocks; re-adopt by patching the emitter's diff, not overwriting). `cull` family and unlinked clip refs `clip_d`/`clip_td`/`clip_tce` are handwritten.
- **Proving an edit output-preserving**: change the description first, run `--vu-check` against the OLD file (`git checkout` it back): IDENTICAL, then IDENTICAL after the hand edit = old == new. Caught: cull loops built a single-colour corner as `vf00 + singleColor` (adds vf00.w = 1.0 to ALPHA) — replicated copies keep `+ vf00`.
- **`--vu-replay <projectDir>`** re-runs a real console capture and diffs (`examples/vu-lab`: 36/36 GS vertices bit-identical). Only the LAST mesh of a chain replays (use the Debugger's flush picker for a single-mesh flush); a project without flow-graph nodes compiles the devkit away (capture button does nothing).
- `--vu-emit` writes to a directory you name; nothing in `vendor/tyra` is auto-generated. Adopting generated microcode needs the full build + hardware pass.
- `--vu-check` limits: **budget line is pessimistic** (can't know VCL pairing; read `1102..2201 of 2042` when the real build cleared by 180) — measure with `nm`. Its **pressure line is a linear scan** that can UNDER-state (preamble constant last read in the per-buffer header looks dead inside the loop that branches back). It once **could not see the spot light** (`stageInput` left lights-direction W = 0, and `invRange2`/`cosCut2`/`invSoft` are Ws); fixed + falsified: flipping one `ibgez` to `ibltz` in `cull_c` now fails within three trials. **Adding a gate → prove the harness fails when the gate is inverted.** It has a peer-path section (description parity only exercises variant zero) and an `-- EE wrappers --` section checking each wrapper's `extern u32 ..._CodeStart` names the image the description says.

## vclpp traps (silent!)
- **Nothing between a `#macro` line and its first instruction — not even a `;` comment.** vclpp expands the macro to NOTHING: no error, exit 0, callers compile green without it. `93a7657` (2026-07-14) put a note inside `PerformClipCheck` (`vcl_sml.i`) and silently removed the frustum clip check from `mcpip_cull` until 2026-08-04. Notes go ABOVE `#macro` in the existing `;//` block. Check after touching any `.i` macro:
  ```bash
  docker run --rm -v "<repo>/vendor/tyra:/e:ro" tyrax-toolchain:local sh -c \
    'cd /e/engine && vclpp src/renderer/3d/pipeline/minecraft/programs/cull/mcpip_cull_vu1.vclpp /tmp/o.vcl && grep -c clipw /tmp/o.vcl'
  ```
  Tell: a program suddenly ~27 instructions shorter in `.vsm` lost a macro body.
- No nested macro expansion: a macro calling a macro leaves mangled text → vcl "can't find instruction". Inline the callee.
- `#define`s expand ONE level: `#define A B` (B a define) reaches dvp-as unresolved ("unresolved expression"). VU-side defines must be literals.
- `[..]` is vclpp's register-array index (`t_lightMatrix[0]`) — a `[w]` field suffix on a macro parameter breaks expansion silently.
- No `#if` in vclpp (hence experiment = whole separate `.vclpp`).
- See the expansion: Docker `docker exec <proj>-compiler-1 cat /tyra/engine/obj/.../<prog>.o.vcl`; check before suspecting math.

## VCL-proper traps (paid for by the VU0 rt kernel)
- Symbolic register names colliding case-insensitively with VU special registers or mnemonics are rejected (`r`, `q`, `i`, `p`, `acc`, `mAx`, `sub` …: "can't use r as a name" / "invalid name for register").
- Broadcast selector only on the SECOND source: `max.x m, vf00, v[y]` works; `max.x m, v[y], vf00[x]` fails with misleading "used before set".
- `ERROR: no opt table .. something failed making table .. for <label>` = register allocator ran out (31 VF ceiling), not syntax. Shrink the live set: reload fixed-address params at use sites (`lq` is cheap), lane-pack scalar fold state (`add.x/y/z reg, vf00, src[x]`, fold once as a vector).

## Microcode rules
- `fcand` sets VI01 to 0/1 (any-bit), NOT the masked pattern.
- A vertex clipped to exactly |x| = w scales to GS 4096.0 and wraps the 12.4 XYZ2 field → clip sides at 0.9w (`VU1_CLIP_XY_BAND` 0.9), let the scissor finish.
- Every VU1 vertex-loop count must be a multiple of 3 or the loop runs off into memory.
- **`begin:` is a LOOP** (`b begin`, one iteration per batch): anything loaded above it must stay live through the body. openvcl reused those registers per vertex → every batch after the first stored garbage GIF tags (50 FPS of missing terrain, no assert). **Load per-batch values next to the store that reads them, inside the loop** (the nine `*_c`/`*_tc`/`*_tce`/`*_td` programs do; also cuts pressure). Detection (per register component: written in preamble, read in body before body writes it, written in body) + per-input-file `vcl` dispatch wrapper: docs/toolchain-image.md "A miscompile this uncovered".
- **The CLIP flag is a 24-bit shift window** (six bits pushed per `clipw`); a mask names a POSITION. Reading too soon gives the PREVIOUS vertex's answer (data-dependent: right in one scene, blank frame in another). SCE keeps positional tests 3-4 rows behind their `clipw` and issues the NEXT `clipw` before the read. Measure window depth per read (how many `clipw`s precede each `fcand`; `clipflags.py` lived in the working notes) in EMITTED ROWS — interlocked waits cost cycles, not instruction words.
- **Widening the cull programs' ADC test is retired**: three guard-band attempts in `PerformClipCheck` corrupted ADC bits (documented in `vcl_sml.i`). Real VU1 clipping = the `clip` family (default `"clipping": "vu1"`), which never derives ADC from clip flags.
- **Clamp vertex colours BEFORE the clipper interpolates them.** Cull programs `FixColor` per vertex after the spot light; clip programs only clamped in the emitter (after the lerp) → lit surfaces BRIGHTEN when touching a screen edge. `stapip_clip_{c,tc}_vu1.vclpp` clamp before storing to the scratch polygon (float clamp only; `ftoi0` stays in the emitter). Only the ceiling fit (`loi 255` + `mini.xyz`, 6 instructions); adding the `max.xyz … vf00[x]` floor (9/program) tripped `VU1 pipeline programs overflow into the draw-finish program` (path1.cpp) on the boot logo. Also in `buildClipBody` (vugen.cpp) — edit both or `--vu-check` fails (that's the point: the ceiling used to land on `clip_c` and be forgotten on `clip_tc`).
- Spot cone term: magnitude scales with distance², only its SIGN is the exact angular cutoff. Size `invSoft` in `buildSpotForBag` off a FRACTION of the range (`softness / (objRange2 * (1 - cosCut2))` made the torch black up close, bright only at the far end).

## Micro memory (2042-instruction ceiling below draw-finish)
- Overflow is SILENT in release builds (assert compiled out). **Count with `nm`, not `.vsm` rows**: uploader sizes from `<name>_CodeEnd - <name>_CodeStart` (`VU1Program::calculateProgramSize`, rounded to even — MPG uploads 64-bit pairs); rows run 2-6 high per program (~7 words on the resident ten).
  ```bash
  docker run --rm -v "tyra-engine-<hash>:/tyra:ro" tyrax-toolchain:local sh -c 'mips64r5900el-ps2-elf-nm /tyra/engine/obj/renderer/3d/pipeline/static/core/programs/clip/stapip_clip_c_vu1.o | grep -i Code'
  ```
  (volume name in the project's `docker-compose.yml`), or `mips64r5900el-ps2-elf-size obj/.../clip/*.o` (bytes / 8 = instructions).
- Historical measurements (always re-measure): resident ten SCE 2028 / openvcl 1996 → 2026; clip set 1676; 1684 before 1.94 (8 distinct images: 5 cull + 3 clip) → **1862** after the 1.94 spot gate (two duplicated cull loops); 1944 → **1698** after the 2026-09-26 VU1 audit (VU1-clipping set; EE-clipper set 1602 → 1508); 2026-08-11 shared images left 366 slots of headroom; "~1950 since 1.134.3's fog gate" (relation to the audit figure unverified). docs/toolchain-image.md's ~1988 predates shared images. Per-program table: docs/vu1-clipping.md "The resident set after the VU1 audit".
- **Words are not the perf metric**: openvcl is smaller than Sony's on all three corpora; the ~20 % gap once reported on a VU1-bound scene was a miscompile of ours — fixed, parity on every scene that can register a difference. The hidden cost is FMAC read-after-write stalls (docs/toolchain-image.md "Measured on the console").
- Booting an oversized program: shrink the SET — `StaPipQBufferRenderer::setProgramsCache` uploads programs from address 0; the packet tap names the program each mesh runs (`program @<addr>`); `vclab`'s twelve meshes all ran `programs[1]`, so passing 2 instead of `count` freed ~1600 words. Prove the harness by running SCE's build under it first. Or pay with smaller programs: put the candidate on openvcl with programs where openvcl beats SCE (`cull_d` −4, `cull_td` −8) — how `stapip_clip_c` was booted and its miscompile found.

## Clip images: five logical programs, TWO resident images
- `C/D` share C's image (two-stream input, stride-2 scratch); `TC/TCE/TD` share TC's (three-stream/stride-3; TD's normals sit where TC's colours do — merging saved 230 words).
- `VU1_OPTIONS_ADDR.y > 0` selects D in C's image and TCE in TC's; TD = `.y > 0` AND `.x < 0` (`sendObjectData` writes -1 to the single-colour lane for resident TD; every reader of that lane tests `> 0` only; TD is never single-colour). TD test sits inside the env branch: TC colour path pays 0, TD/TCE pay 2 instructions. TD reuses env-basis/spot registers the preamble loaded, loads only light colours + ambient per triangle.
- That left 33 VF live in `sharedDirMode`: Sony vcl refused ("no opt table"), only openvcl's load sinking (`--sink-loads*`) fitted it — 51396a98 broke `--docker` five days. Fix that works on both: TC image loads its five GIF-tag-block constants once per buffer at the tag block (`tagsPerBuffer` in vugen), output-identical because the EE writes those addresses only behind `sendObjectData`'s FLUSHE.
- Wrappers point at the same CodeStart/CodeEnd; `Path1::createProgramsCache` aliases that range to one destination. A generated override replacing one peer gets its own image; the built-in stays for the other.
- `stapip_clip_d_vu1.vclpp`, `_tce_vu1.vclpp`, `_td_vu1.vclpp` are compiled but never LINKED (reference for the peer-path check; TC/TD runs with `runtimeColorLane = -1`). Don't point a wrapper back at them.
- Clip-only count word: bits 0..9 vertex count, bits 10..15 active-plane mask (static-pipeline.md).

## `VU1_OPTIONS_ADDR` lanes
- `.y` is THREE-STATE: `> 0` shared clip image peer path, `0` base, `< 0` base + live dynamic light. Free only because every reader tests `> 0` vs `<= 0`; keep that; a fourth fact needs wider tests first.
- `.z` = fog SCALE; since 1.134.3 `cull_tc` has a third fog-free unlit loop entered when its low 16 bits read 0 (EE keeps a real scale's low half non-zero; docs/vu1-and-dma-cache-cost.md "The fog gate").
- `.x` single-colour lane (−1 = resident TD).

## Spot-light gate (1.94.0) — cost facts
`CalculateTyraSpotLight` = 21 upper-pipe ops/vertex (63/triangle), ran on every mesh. Gated on the SIGN of `VU1_OPTIONS_ADDR.y` from `StaPipClipperSpot::enabled` (same predicate as `addSpotToColor`). Cycles/triangle unlit → lit: `cull_c` 130 → **72**/130, `cull_tc` 133 → **73**/133, `clip_c` 230 → **173**/232, `clip_tc` 241 → **184**/243 (colour path). docs/flashlight.md "The cone costs nothing when nothing is lit".
- **A branch is a scheduling barrier; price the path that does NOT take it.** First shape (gate inside the loop): garage day −1.059 ms, outer day −0.620, outer night −0.701, **garage night +1.264 ms** (lamps on → nearly every mesh LIT; branch cost 11 cycles/triangle of lost pairing, `cull_tc` 133 → 144; 25 090 tri × 11 predicts 0.93 vs 1.26 measured). Shapes for `cull_tc` lit/unlit: per-corner gate 154/91, one gate before first `MatrixMultiplyVertex` 144/81, **two whole loops picked once per batch 133/73** (shipped for cull; lit loop byte-for-byte the original). Clip pair keeps a branch but DUPLICATES the colour clamp (+2 instead of +7). Programs with a project stage list keep one loop + per-corner gate.
- Three output-preserving trims (2026-09-26): (1) `CalculateTyraFog` is ONE `mul.x` — `LoadTyraFogParams` copies fog scale into the register's x lane (single-colour flag there is only read via `ilw`; dynpip lerp reads only y) → new callers must use `LoadTyraFogParams`. (2) Lit colour not clamped twice: light macro clamps 0..255, `FixColor` reduced to `ftoi0` (cull_d/cull_td, as_is_d/as_is_td); clip_c's emitter drops its ceiling (every path into C's scratch polygon already capped); TC keeps it (env path carries raw stream colours). (3) Cull colour loops don't branch on single colour: header writes three copies (`+ vf00`) to `VU1_SINGLE_COLOR_COPIES_ADDR` = 1016 (above polygon B), loop walks a stride register 0 or 3. Stage lists/scripts keep old shapes.

## Experiments = whole images (`stapip_vu1_experiments.hpp`)
Alternative program = its own `.vclpp` (`*_fold`, `*_envn`, derived from the shipping file — **re-derive after editing it**), always assembled; wrapper picks symbols under `#if TYRA_VU1_EXP_...`/`#else`. Folded lights and normalized env normals ship enabled after the 2026-09-27 physical A/B (saved 0.23..0.25 and 0.14..0.16 ms in augmented garage poses). The k255/ADC-table image was bit-identical but slower in all four poses (+0.11..+0.20 ms) — removed. `--vu-check`'s wrapper check reads the original branch; its experiment section bounds colour/ST differences. docs/vu1-clipping.md "Measured VU1 follow-ups".

## VU1 cost (2026-09-15, docs/vu1-and-dma-cache-cost.md)
- **0.0768 ms of frame per cycle per triangle** in the Motor District garage; 97 % surfaces as VIF1 DMA wait (+12 cycles/tri: submission 40.830 → 41.752 ms, wait 6.794 → 7.688). No slack: removed arithmetic comes straight off the frame.
- Cycles/triangle = lines between loop label and loop branch in the native cache's `.o.vsm` (every static loop does 3 vertices/iteration). openvcl: `cull_tc` 133, `cull_c` 130, `cull_tce` 109, `cull_td` 107, `clip_tc` 269 (pre spot-gate figures).
- **Aim experiments at the program the scene runs**: instrumenting `cull_td` measured zero — generated games attach a lighting bag only to dynamically lit objects; static geometry has `lighting == nullptr` → COLOUR programs. `getCullProgramByParams(isLightingEnabled, isTextureEnabled)` decides.
- GIF/REGLIST experiments: gs-state.md.

## Is VU1 clipping even running?
`"clipping": "vu1"` → `CLIP_VU1S[...] = {true}` in the generated `inc/scene_data.hpp` → `StaPipQBufferRenderer` uploads the `clip_*` programs; `"precise"`/`"fast"` use the EE clipper's `as_is_*`. A `vclab` copy on `"precise"` gave a pixel-identical openvcl frame read as "VU1 clipper works" (with `"vu1"` the build differed in 506 784 of 514 600 pixels). `grep -n "CLIP_VU1S" <project>/inc/scene_data.hpp`.


## VU0 split-field upper/lower write hazard

The SAT/CCD assembled oracle rejected openvcl pairing `addx.x VF03` with
`mfir.y VF03`: the overlap flag retained old float bits. Disjoint destination
fields did not make the pair safe. Keep the explicit --barrier before the
MFIR flag pack in vu0_sat_kernel.vclpp. Source-level vusim passes without it;
only the assembled PCSX2/console probe sees this scheduling fault.

Shared-body physics prototypes use OPMULA/OPMSUB for all nine edge-cross axes.
Both instructions use the same cyclic source swizzle; swap sources for OPMSUB.
The IR simulator now supports these fixed-XYZ operations, including aliased
destinations and W preservation. tools/verify-vu0-shared-separation.cpp checks
known vectors, canonical contacts and raw cross-only rejections; deliberately
zeroing cross axes must fail before hardware qualification. Packet bounds alone
are not semantic evidence, and short VU finish waits are not utilization proof.
