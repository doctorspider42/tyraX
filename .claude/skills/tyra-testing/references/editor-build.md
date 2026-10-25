# Layer 0 — building the editor, dependencies, build timing

Read when: the editor build fails, you touch build scripts / deps / CMake / platform code, you time a build, or you check native incremental-build behaviour.

## Commands

```powershell
./build.ps1          # → build/tyrax-editor.exe (fetches missing vendor deps itself)
./build.ps1 -Run     # build + launch the GUI
./build.ps1 -Clean   # full rebuild
./build.ps1 -Dev     # -O1 iteration build → build-dev/tyrax-editor.exe
```
```bash
./setup.sh --deps    # one-time on a bare box: editor toolchain + dev headers AND the game half (curl/tar/rsync)
./build.sh           # → build/tyrax-editor
./build.sh --run | --clean | --dev   # --dev → build-dev/tyrax-editor
```

- Clean Release ≈ 1–1.5 min on 16 cores; one UI file ≈ seconds. ccache/sccache on `PATH` is used automatically (cheap worktree switching).
- **Dev (`-O1`, own `build-dev/`)** is for the edit-compile-look loop (`-O3` is ~2/3 of compile time). Never benchmark/ship it, **never verify a bake with it** — gibake/matbake/aobake/pngquant at -O1 take minutes and look hung. Verify bakes with Release.
- Build must be warning-clean; this is the compile check for all of `src/`.
- `cmake --build build --target check-ui` adds the GUI ID/Assets regression gate (fresh fixture/settings, artifacts in `build/ui-check/`). Requires a desktop; headless Linux: `xvfb-run -a` with Xvfb/xauth installed. The Linux release job runs it before packaging; compilation alone cannot cover runtime widget IDs.
- Prereqs: Windows `scoop install mingw cmake ninja`; Linux `./setup.sh --deps` (apt/dnf/pacman/zypper lists in `deps.sh`). build.sh checks tools + pkg-config headers first, prints the distro's install command, refuses to configure otherwise.

## Traps

- **MSVC is not supported, no flag fixes it.** `src/templates.cpp` holds PS2 templates as raw literals beyond MSVC's 16380-byte cap → Visual Studio's default `x64-Debug` CMake preset dies with *C2026 string too big* (+ C2589/C2660 in `wire.cpp`, `windows.h` `min` macro). That report = wrong kit, not a code bug. Compiler is MinGW-w64 GCC.
- **On Windows bare `build` runs `build.cmd`** (PATHEXT: `.CMD` before `.PS1`). `build.cmd`/`setup.cmd` are wrappers only (map `run`/`clean`/`dev` → `-Run`/`-Clean`/`-Dev`, forward with `-ExecutionPolicy Bypass`). Never give them logic: a hand-copied dep list once drifted → "vendor\tyra is not an empty directory", "Cannot find source file: vendor/ufbx/ufbx.c", `fatal error: miniaudio.h` (PROGRESS 214). On a missing `vendor/` header, check WHICH script ran first.
- **Dependencies live in exactly one list per platform: `deps.ps1` and `deps.sh`.** setup fetches from them, build probes them. Add a dep to BOTH and nowhere else.
- Each dep is fetched at a **pinned commit** (`git init` + `git fetch --depth 1 <url> <sha>`, mirror-fork fallback). `git -C vendor/<dep> rev-parse HEAD` must equal the SHA in `deps.sh`; if not, the checkout is stale — delete the dir and re-run setup (don't pull).
- `Cannot find source file: vendor/<x>` (+ `No SOURCES given to target: tyrax-editor`) = path not on disk yet (e.g. after merging a branch that added a dep). Build script normally fixes it; `./setup.ps1`/`./setup.sh` fetches without building. If the guard still reports a probe missing after a fetch, delete the dir and re-run setup (probes are real source files, so half-clones show as missing).

## Platform parity

Only one platform's compiler runs at a time → a cross-platform change is half-checked until the other side builds. Anything touching `src/platform.*`, `wire.cpp`, the Runner, CMakeLists or a paired script needs both builds, or say so in the commit. Pairs: `deps.ps1`/`deps.sh`, `setup.ps1`/`setup.sh`, `build.ps1`/`build.sh`, CMake `if(WIN32)`/`else()`, `platform.cpp` `#ifdef _WIN32`/`#else` (tyra-editor-dev "Platform parity").

## Timing a build honestly

- Laptop thermals drift ~20% between back-to-back clean builds: alternate A, B, A, B and compare like rounds.
- Read `build/.ninja_log` (`start_ms end_ms mtime output`): per-target durations; sum(end−start) vs wall clock = real parallelism (found the 26k-line app.cpp critical path and the libimgui.a ordering stall).

## Native (game) incremental build regression

- Windows: compare `TYRAX_NATIVE_DIRECT=1` (bypasses the WSL filesystem mirror, `tools/toolchain/native-build.ps1`) against the default WSL cache, warming each separately. Verify `bin/` returns to the authored project, a Windows-written runtime channel survives, DWARF paths map to authored sources. Boot the ELF in a private PCSX2 capture slot.
- Scratch copy of `examples/vehicle-playground`, warm build, then repeat `--build`: **no compile/link/archive commands, unchanged ELF/archive mtimes**. Change one script source, then one scene value → inspect which objects rebuilt. Touch the engine archive → another project's engine rebuild relinks without recompiling. Change a Makefile → flags invalidate objects. A failed engine compile must fail again on retry until fixed.
- IRX ABI changes: compare objcopy binary payloads; link must have no abicalls-mismatch warnings. Toolchain identities must match for byte-identical trees at different absolute paths. Exclude setup/cold builds from timings.
- Generated-game split: build FPP + orbit fixtures native, boot in a private PCSX2, inspect scene/menu/vehicle state; syntax-check historical example subsystems with the PS2 compiler; mutable helper symbols/cache guards must have one final linked instance; check marker-owned upgrade, owned legacy preservation, owned split refresh. Time active ambience keys and a real project preference separately from object moves/additions and no-op builds. Jobs use host-visible logical CPU counts — never describe the measured `-j24` as a project setting.
- Scene object data (value-only move): after warming, move one object + refresh → `scene_objects.gen.cpp` should be the only compiled source (other bakes unchanged). Additions/removals keep the header stable; also test scene-count changes, colour edits, empty scenes, baked scroller clones, both camera templates, a custom script reading `SCENE_OBJECT_TABLES`. Old/new initializer rows must be exactly equivalent; boot the game. Culling on and off: static-box additions update proxy data without changing the declaration header. Feature/derived-table edits may rebuild more. Profile a large remaining compile with GCC `-ftime-report` before blaming parsing/I/O.
