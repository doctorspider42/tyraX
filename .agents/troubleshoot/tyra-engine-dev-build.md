# troubleshoot/tyra-engine-dev-build.md

Fork policy, the build/rebuild path, `Makefile.base`, the toolchain image and its
two VU1 assemblers, and the in-tree audsrv source fork. The `-G0` link error and
the stale `libtyra.a` symptom are in the index (`tyra-engine-dev.md`).

## Where the native build keeps its cache

**Cause:** the native default builds out of a per-user cache, not the repo.
**Fix:** `tools/toolchain/native-build.sh` (Windows bridges through
`native-build.ps1`) syncs the engine into `<configDir>/native-build/<engineVolumeName>`
and builds with pinned PS2DEV at `<configDir>/toolchain/ps2dev` plus openvcl.
`configDir` is `$XDG_CONFIG_HOME` or `~/.config/tyra-editor`, and `%LOCALAPPDATA%`
on Windows; a toolchain stamp invalidates stale objects.

## The `vclpp` preprocessor fails on a file I just edited

**Cause:** the file has CRLF line endings. `vendor/tyra/**` is **LF-only**,
enforced by `.gitattributes`; `vclpp` chokes on CRLF. It fails only on files that
contain a `#macro`, and the message names a line that is fine
(`Preprocessor directive inside macro block: '#endmacro'`), so it reads like a
source error rather than an encoding one. An editing tool respects this; a
**script** may not — Python's `Path.write_text()` turns every `\n` back into CRLF
on Windows.
**Fix:** write bytes, or check `grep -c $'\r'` afterwards. Do not fight the
`.gitattributes` rule.

## A one-line comment in a `.cpp` paid the whole VU1 rebuild

**Cause:** the VU1 microprograms sit outside make's dependency tracking (a
`.vclpp` `#include`s `.i`/`.h` files nothing declares), so the Runner used to
force-rebuild them on *any* engine change. The set costs **109 s** (measured,
`-j6`; `vcl` is slow enough that its optimizer times out on some programs and says
so).
**Fix:** the Runner now force-rebuilds only when the rsync reports a changed
`.vclpp`/`.vcl`/`.vsm`/`.i`/`.h`, so that same edit is a ~7 s build. **If you
change what a VU source may include, widen that extension list in `runner.cpp`** —
a missed extension means stale microprograms, which is the kind of bug that looks
like a renderer bug.

## The `-G0` change was verified byte-identical

**Cause:** the rule change touched every object.
**Fix:** verified byte-identical: the same project built with the old and new rules
produced the same `md5` for its stripped ELF.

## The shared engine volume warns it "already exists but was created for project X"

**Cause:** the volume `tyra-engine-<hash of the engine source path>` is shared by
every project built from the same checkout, so parallel worktrees would rsync
diverging engines over each other forever. A volume compose creates is labelled
with whichever project got there first.
**Fix:** declare it **`external: true`** in the generated `docker-compose.yml` and
create it in the Runner (`docker volume create`, idempotent). The Docker backend
bind-mounts `vendor/tyra` read-only at `/engine-src` in the project's container
(service `compiler`, container `<name>-compiler-1`). If the shared volume
gets into a weird state, `git checkout` inside `/tyra` restores originals (it is a
git checkout of the fork).

## Native incremental builds miss changes / relink against a stale archive

**Cause:** the native backend tracks `bin/libtyra.a` and `bin/<name>.elf`, not
missing root-level files. An overriding engine recipe, or a game that does not
depend on the archive, breaks this.
**Fix:** the native backend always runs engine make (retries included), preserves
unchanged Makefile timestamps, tracks Makefiles as object prerequisites, and games
depend on the archive including rebuilds by other projects. The shared rule
archives `.a` targets — do not add an overriding engine recipe. Embedded IRX data
is assembled with `-call_nonpic` to match PS2DEV v2 EE CPIC objects; binary
payloads stay unchanged and ABI mismatch warnings are not silenced.

## A new host-code directory breaks every build that has one

**Cause:** `src/vu/` and `src/vu0/` are HOST C++ (a project's own VU1 programs and
VU0 kernels, docs/vu-authoring.md), compiled and run at build time by the
container's g++; handing them to the PS2 compiler fails on the very first include.
They are excluded from `SOURCES` by a `find` in `Makefile.base`.
**Fix:** add any new host-code directory to that `find` exclusion. `Makefile.base`
is shared by the engine build and every generated game, so an edit there moves
both. TyraX changes in it: **`-G0`**, single-pass dependency generation
(`-MMD -MP`; it used to run the compiler a second time per file just to write the
`.d`), `| directories` order-only prerequisites so `-j` cannot reach an absent
`bin/`, and `rsync -ru` for the resource copy **minus `RESDIR_SKIP`**.

## A GI/shadow cache got burned onto the disc / wasted disk on every build

**Cause:** `bin/` IS the game's filesystem. `.res-baked/gi/` is the
global-illumination solve and `.res-baked/shadow/` the baked shadow decals; what
ships is the PIXELS they produce, already beside them in `aomap/`, `aoatlas/` and
`shadowatlas/`. `isoexport` walks all of `bin/`.
**Fix:** `RESDIR_SKIP` is the resource copy's exclusion list; a new host-only cache
under `.res-baked` joins that list **AND** the matching skip in
`src/isoexport.cpp` (`rel.rfind("gi/",0)…` — the second covers a `bin/` an older
build already polluted).

## An audsrv source change does not reach a game build

**Cause:** `vendor/tyra/audsrv/` is a **SOURCE fork** at ps2sdk `e78a9cb2`, not a
blob; nothing in the game build compiles audsrv. `vendor/tyra/audsrv/build.sh` /
`build.ps1` rebuild the three artifacts in `bin/` (`audsrv.irx`, `libaudsrv.a`,
`audsrv.h`) that `src/runner.cpp` overlays into the build container.
**Fix:** re-run that script and commit `bin/` in the same commit. **Except on the
from-source image** (`docker/Dockerfile.fromsource`), which compiles the EE half
itself because the committed `libaudsrv.a` carries GCC 11.3 LTO bytecode a newer
GCC refuses, and skips the Runner's overlay — so a source change reaches THAT image
only after the image is rebuilt. The crossing is `ee/src/sif-compat.h`: upstream
renamed ten EE-side SIF RPC entry points to `sce`-prefixed ones and the two SDKs
export disjoint sets, so the header aliases them under `TYRAX_PS2SDK_SCE_SIF`,
which only the image can set (the compile always sees the old pinned headers —
`__has_include` cannot tell them apart). It is included FIRST, before any ps2sdk
header. `./build.sh --check` diffs a fresh build against the committed artifacts;
`audsrv.irx` is byte-identical while `libaudsrv.a` never is (ar stamps its members,
gcc's LTO section names carry a random per-compilation id), so the member SIZES are
what that check compares.

## The ps2sdk build tree for audsrv cannot be fetched

**Cause:** the script fetches a build TREE (these Makefiles include
`$(PS2SDKSRC)/Defs.make`), which has a mirror fallback like every entry in
`deps.sh`.
**Fix:** `PS2SDK_MIRROR` in `build.sh` points at
`doctorspider42/tyrax-vendor-ps2sdk`. Losing upstream costs the ability
to REBUILD the module, not the module (its sources are in-tree) and not game builds
(those use the SDK installed in the `h4570/tyra` image).

## Swapping the toolchain image changed nothing

**Cause:** the engine's make keys off `.vclpp` timestamps, and an image swap
touches neither them nor their checksums, so the previous build's VU objects are
relinked (this produced three identical "bisection" screenshots before anyone
noticed).
**Fix:** build with `--rebuild`, and confirm the log shows 25 `vcl` invocations.

## Which VU1 assembler built my microcode?

**Cause:** two assemblers exist. `vcl` in the stock image is Sony's prebuilt VCL
1.4beta7 (32-bit x86, no source, no license). The from-source `openvcl` compiles
all 25 programs since 2026-08-04 and, since 2026-08-05, a game built with it runs
**pixel-identical to Sony's output**. openvcl schedules *differently*, so **any VU1
timing you measure belongs to one assembler, not to the engine**.
**Fix:** say which one in the commit message, and A/B with the same one. Since
2026-08-05 a game built with openvcl **runs, pixel-identical to Sony's output** — on
the EE clipper, and on the VU1 clipper for all ten resident programs (`stapip_clip_c`,
the last one to blank `blocks-terrain` and `raytraced-mirror`, was fixed by the
register-liveness work). `VCL_IMPL=legacy|openvcl` picks it when building the image;
the image records its choice in `/usr/local/share/tyrax/vcl-impl`. The patches
are `docker/openvcl-tyrax.patch` and `docker/openvcl-clipw-upstream.patch`;
numbers, repro and `docs/upstream-openvcl.md`: `docs/toolchain-image.md`.

## Sony's `vcl` broke while the native build stayed green

**Cause:** the default build is native openvcl, so the Docker fallback (Sony's
`vcl`) is exercised by nobody. `51396a98` broke it for five days: TC's clip image
with the TD path on it had 33 VF live in `sharedDirMode`; openvcl's
`--sink-loads*` silently moved five preamble loads into the buffer header and
fitted, Sony's allocator said `no opt table .. for sharedDirMode` and every
`--docker` build died in the engine. `--vu-check`'s pressure line read 30 and did
not warn (it is a linear scan and can UNDER-state).
**Fix:** **a VU edit is not done until BOTH assemblers take it.** Load per-buffer
constants where they are used (the tag block), not in the preamble; it costs a few
lower-pipe `lq`s a BUFFER and is output-identical. Check after any `.vclpp` edit:
`vclpp`+`vcl`+`dvp-as` over every engine `.vclpp` in `tyrax-toolchain:local`
(cwd = engine dir), or one small project built with and without `--docker`.

## A microprogram that does not fit the resident set can still be tested

**Cause:** the ceiling is on the SET (2042 instructions), not on the program.
**Fix:** put the oversized candidate on openvcl together with the programs where
openvcl beats SCE (`cull_d` -4, `cull_td` -8) and the set fits again. That is how
`stapip_clip_c` was booted at all. The whole set fits on either assembler now, so
this trick is for a *new* oversized candidate.

## PS2SDK's `math3d.h` collides with my constants

**Cause:** it `#define`s names like `LIGHT_AMBIENT`.
**Fix:** prefix your constants (the codebase uses `SCENE_*`). The engine's umbrella
headers are `engine.hpp` / `game.hpp`, beside the mirrored subsystem directories
(`audio`, `debug`, `file`, `info`, `irx`, `loaders`, `math`, `pad`, `physics`,
`renderer`, `thread`, `time`).

## I cannot compile engine code on the host

**Cause:** the compiler is `mips64r5900el-ps2-elf-g++` inside the `h4570/tyra`
image; there is no way to compile engine code on the host.
**Fix:** even a syntax check requires a game build (see `tyra-testing`).
