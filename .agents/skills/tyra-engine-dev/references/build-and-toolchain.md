# Build plumbing, Makefile.base, toolchains

Read when: an engine edit "does nothing", a link/compile error mentions the engine, you touch `vendor/tyra/Makefile.base`, `src/runner.cpp`'s engine sync, the toolchain image, or compiler flags.

## How an engine change reaches the game

The Runner (`src/runner.cpp`) rebuilds the engine on every game build (F5 / `tyrax-editor --build <projectDir>`). No manual engine build, no container restart.

**Native (default)**: `tools/toolchain/native-build.sh` (Windows: `native-build.ps1`, via WSL) syncs `vendor/tyra/engine` into `<configDir>/native-build/<engineVolumeName>` (configDir = `$XDG_CONFIG_HOME` or `~/.config/tyra-editor`, `%LOCALAPPDATA%` on Windows) and rebuilds with pinned PS2DEV (`<configDir>/toolchain/ps2dev`) + openvcl. A toolchain stamp invalidates stale objects.
- Incremental builds track `bin/libtyra.a` and `bin/<name>.elf`, not missing root-level files. Engine make always runs (retries included); unchanged Makefile timestamps are preserved; Makefiles are object prerequisites. Games depend on the archive, including rebuilds by other projects.
- The shared rule archives `.a` targets — do not add an overriding engine recipe.
- Embedded IRX data is assembled with `-call_nonpic` to match PS2DEV v2 EE CPIC objects. Payloads unchanged; ABI-mismatch warnings deliberately not silenced.

**Docker fallback** (`--docker`):
1. `vendor/tyra` bind-mounted **read-only** at `/engine-src` (service `compiler`, container `<name>-compiler-1`).
2. Runner checksum-rsyncs it into volume `tyra-engine-<hash of engine source path>` mounted at `/tyra`, shared by all projects of one checkout (parallel worktrees get their own, else they rsync diverging engines over each other). Declared `external: true` in the generated `docker-compose.yml` and created by the Runner (`docker volume create`, idempotent) — a compose-created volume is labelled with the first project and every other project warns "already exists but was created for project X".
3. Anything changed → `libtyra` rebuilt once, game ELF dropped so it relinks. Unchanged → rsync no-op, seconds.
- Weird volume state: `git checkout` inside `/tyra` (it is a git checkout of the fork). **Build > Rebuild** (`--build --rebuild`) throws away the whole compiled engine incl. VU1 objects.
- **Compiler-flag changes need `--rebuild` on Docker** (it rebuilds only when SOURCES changed) — otherwise objects compiled both ways. Volumes are per checkout, so another checkout needs its own rebuild.
- Fixed 1.81.1: a failed engine `make` used to leave the PREVIOUS `libtyra.a`; next rsync saw nothing, skipped make, linked stale, printed `Build OK` (read as "the fix changed nothing" four runs in a row). Runner now deletes `libtyra.a` on failure. Symptom if it recurs: `Build OK` without an `ar rcs bin/libtyra.a` line after engine-source changes.

**VU1 microprograms are the expensive special case.** Outside make's dependency tracking (a `.vclpp` `#include`s `.i`/`.h` nothing declares), so the Docker Runner force-rebuilds them only when the rsync reports a changed `.vclpp`/`.vcl`/`.vsm`/`.i`/`.h` (`grep -qE '[.](vclpp|vcl|vsm|i|h)$'` in runner.cpp). Full rebuild = **109 s** (`-j6`; `vcl`'s optimizer times out on some programs and says so). A `.cpp`-only edit is ~7 s. If VU sources may include a new extension, widen that list — a miss = stale microprograms that look like a renderer bug.

**Swapping the toolchain image rebuilds NO microcode** (make keys off `.vclpp` timestamps): previous VU objects relink, the change "does nothing" (three identical bisection screenshots). Build with `--rebuild` and count the `vcl` invocations in the log (was 25; the tree now has 32 `.vclpp` incl. 7 `*_fold`/`*_envn` experiment variants — expected count unverified).

## `vendor/tyra/Makefile.base`

Shared by the engine build AND every generated game — an edit moves both. TyraX changes:
- **`-G0`** (see below).
- Single-pass deps `-MMD -MP` (used to run the compiler twice per file).
- `| directories` order-only prereqs so `-j` cannot reach an absent `bin/`.
- `rsync -ru` resource copy minus **`RESDIR_SKIP`** (`gi shadow`).
- **`src/vu/` and `src/vu0/` excluded from `SOURCES`**: HOST C++ (a project's own VU1 programs / VU0 kernels, docs/vu-authoring.md) built and run at build time by host g++; the PS2 compiler fails on the first include. A new host-code dir must join that `find` exclusion.
- `-lps2snd` (reverb, see audio.md).
- Verified byte-identical: same project, old vs new rules → same `md5` of the stripped ELF.

**`-G0` and the link error that names the wrong thing.** Without it engine + game data goes in `.sdata`/`.sbss`, GP-relative 16-bit (±32 KB), which the engine outgrew. Failure = dozens of `relocation truncated to fit: R_MIPS_GPREL16 against Tyra::Info::writeLogsToFile` — names a one-byte bool, reads as a missing symbol, not "too much small data". DEBUG hits it first (more inlined logging) — that is why enabling `showFps` stopped linking and broke the perf-benchmark recipe. ps2sdk builds its own libs with `-G0` (`Defs.make`).

**`RESDIR_SKIP`: `bin/` IS the game's filesystem.** `.res-baked/gi/` (GI solve) and `shadow/` (baked shadow decals) are host-only caches; the console reads their output in `aomap/`, `aoatlas/`, `shadowatlas/`. Copying them wasted disk and fed `isoexport` (walks all of `bin/`) a multi-MB GI cache for the disc. A new host-only cache under `.res-baked` joins `RESDIR_SKIP` AND the skip in `src/isoexport.cpp` (`rel.rfind("gi/",0)…`) — the latter covers a `bin/` an older build already polluted.

## Compiler environment
- Compiler `mips64r5900el-ps2-elf-g++` (native toolchain, or inside the image). No host compile of engine code; even a syntax check needs a game build (tyra-testing).
- PS2SDK `math3d.h` `#define`s names like `LIGHT_AMBIENT` — prefix constants (codebase uses `SCENE_*`).
- LF only under `vendor/tyra/**` (`.gitattributes`). vclpp fails on CRLF only in files with a `#macro`, with a misleading message (`Preprocessor directive inside macro block: '#endmacro'`). Python `Path.write_text()` writes CRLF on Windows — write bytes, check `grep -c $'\r'`.

## Two VU assemblers
- `vcl` in the stock image = Sony VCL 1.4beta7 (32-bit x86 prebuilt, no source/license). `openvcl` = from-source; compiles all programs since 2026-08-04 (one patch + moving GIF-tag loads into the batch loop in `stapip_clip_d`/`clip_td`/`cull_td`); since 2026-08-05 games built with it run **pixel-identical** to Sony's, EE and VU1 clipper.
- Native default = openvcl; Docker fallback = Sony `vcl`. `VCL_IMPL=legacy|openvcl` picks at image build; recorded in `/usr/local/share/tyrax/vcl-impl`. Patches: `docker/openvcl-tyrax.patch`, `docker/openvcl-clipw-upstream.patch`; docs/toolchain-image.md, docs/upstream-openvcl.md.
- openvcl schedules differently: **any VU1 timing belongs to one assembler** — name it in the commit, A/B with the same one. Size/perf details: vu1-microprograms.md.
- **A VU edit is not done until BOTH assemblers take it** (Sony's is exercised by nobody; 51396a98 broke `--docker` for five days). Check: `vclpp`+`vcl`+`dvp-as` over every engine `.vclpp` in `tyrax-toolchain:local` (cwd = engine dir), or the native `vcl` wrapper, or one small project built with and without `--docker`.

## audsrv is built separately
See audio.md: `vendor/tyra/audsrv/build.sh`/`build.ps1` produce the committed `bin/` artifacts; nothing in a game build compiles audsrv (except `docker/Dockerfile.fromsource`).
