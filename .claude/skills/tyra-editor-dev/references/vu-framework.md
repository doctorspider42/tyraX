# The VU framework (host side)

Read when you touch `vuir`/`vuasm`/`vusim`/`vugen`/`vucap`/`vushader`/`vumain`/`vu_ui.cpp`, the `--vu-*` CLI, project VU scripts, or the built-in clip programs' resident images. Docs: docs/vu-framework.md, docs/vu-authoring.md. Engine-side VU1 work: tyra-engine-dev.

## Modules (host-only, no GL, no project.hpp; exercised via `--vu-check` / `--vu-emit` / `--vu-list` / `--vu-replay` / `--dump-vucap`)

- `vuir.{hpp,cpp}` (~115+320): shared instruction model = **VCL-level assembly with unlimited virtual registers** (pre-schedule: what a program computes, not when).
- `vuasm.{hpp,cpp}` (~55+830): parses the engine's handwritten `.vclpp` into vuir (`#include`, one-level `#define`, non-nesting `#macro`/`Name{ }`).
- `vusim.{hpp,cpp}` (~150+675): EXECUTES vuir on either unit (`Target::VU1`/`VU0`: 1024 vs 256 qwords data memory, 2048 vs 512 micro slots; `xgkick`/`xtop` warned on VU0; `runKernel` for the `vcallms` contract - restart at entry, data memory persists); masked fields, ACC, Q/I, clip flags, 16-bit VI wrapping.
- `vugen.{hpp,cpp}` (~850+4.8k): the C++ DSL + `.vclpp` and EE-side emitters. One `Desc` yields microprogram, EE program class, tag-block size and GS register list together (removing `.vclpp` vs `*_vu1_program.cpp` drift).
- `vucap.{hpp,cpp}` (~165+665): decodes the engine's VU1 packet capture (`bin/vucap.bin`): DMA tags, VIF commands, UNPACKed blocks, which microprogram started (docs/devkit.md). `vucap::scanGifPackets` is THE GIF decoder.
- `vushader.{hpp,cpp}` (~440+40): the layer a PROJECT writes VU programs against (value types whose operators append to the same program; registry behind `VU_PROGRAM` is a function-local static - static-init order). `vushader_sample.cpp`, `vushader_palette_sample.cpp`, `vushader_snap_sample.cpp`, `vushader_wobble_sample.cpp`, `vukernel_sample.cpp`: canonical samples compiled into the editor so `--vu-check` can simulate them; examples/vu-lab ships the same files.
- `vumain.cpp` (~760): the generator a project's `src/vu/*.cpp` scripts are compiled into INSIDE the build container (host compiler) and executed to produce ordinary generated source; never runs on the PS2 or in the editor. Generated VU outputs are refreshed and stale ones swept by `refreshGenerated` (`isVuGenerated`).
- `vu_ui.cpp` (~1.3k): Tools > VU Programs - authoring a VU1 microprogram or VU0 kernel out of stages, no assembly; prices engine crossings via `vugen::engineDesc`.

## Load-bearing properties

- `vusim` ends a run with the same memory image `vucap::Capture::vuMem` carries, decoded by the SAME `vucap::scanGifPackets` - simulated and console runs are directly comparable. A second GIF decoder would destroy that.
- The generator emits **VCL, not microcode**: `vcl` keeps register allocation and dual-issue scheduling (as fast as handwritten); the framework must never schedule.
- `equivalence()` is the proof obligation: change a described program on either side and `--vu-check` fails until both agree.
- **`vusim` must not use host float semantics**: the VU FPU has no inf/NaN, overflow saturates to `0x7F7FFFFF`, denormals are zero - every float the machine writes goes through `vuFloat()`, EXCEPT the MOVE FAMILY (`move`, `mr32`, `mfir`, `ftoi*`, `lq`/`sq`) which carries integer bit patterns. `vusim::run` also switches the host ROUNDING MODE (VU truncates toward zero; x86 rounds to nearest-even; showed as 1-2 ULP in 24-bit Z). Range and rounding are separate fixes; both needed.
- **`--vu-check` proves the microprogram, NOT the EE side** (it stages VU1 memory itself, so the emitted `addProgramQBufferDataToPacket` never runs): the emitted `c` put colours a block too far and `td` unpacked normals onto the ST block while the check said "bit-identical". The per-vertex block layout therefore lives ONLY in `attrBlocks()` (vugen.cpp); stream count and emitter derive from it - never restate it.
- `--vu-replay` reconstructs input from a REAL capture, re-runs it and diffs the staged GIF packet against the hardware's; `examples/vu-lab` matches 36/36 GS vertices.

## VU clip-image aliases

- The five built-in clip classes occupy three resident code images: `C/D` share the generated C image, `TC/TCE` share TC, `TD` stays specialised; `VU1_OPTIONS_ADDR.y` selects the peer path.
- **The pairing is declared in ONE place**: `Desc::residentImageAsmName` / `codeOwner`, next to the `sharedClip*` flags. The flags say "my body carries a peer's path"; the two fields say "somebody else's body carries mine", and every consumer reads the latter: `vugen::residentImage` (what the emitted EE wrapper links, what `--vu-check`'s budget charges) and `engineCrossingInstructions` in `vu_ui.cpp` (via `vugen::engineDesc`, not a hardcoded copy). The engine's part is `Path1::createProgramsCache` (uploads an identical source range once).
- Two `--vu-check` sections are mandatory when touching any of it (the per-description equivalence run only selects variant zero): `-- shared clip images, peer paths --` (each shared image's variant one vs the specialised D/TCE source) and `-- EE wrappers ... --` (emitted wrapper AND the one in `vendor/tyra` vs the description's image and its two `StaPipVU1Program` ABI numbers). Undo sharing in a wrapper by hand and every microcode section still says IDENTICAL - only that section fails.
- `elementsPerVertex` (last ctor arg) is `attrStreams + 1` for a LIT program, `attrStreams` otherwise: `getMaxVertCount` subtracts one element for single-colour meshes and a lit program never unpacks colours, so the phantom block makes the subtraction land right. Only the CULL program's numbers are consulted (`getMaxVertCountByBag` asks `getCullProgramByBag`).
- The VU language support lives in `tools/vscode-tyrax`; a change there is not done until the `.vsix` is repackaged (it has been forgotten for the VU language once; codegen-and-generated-files.md).
