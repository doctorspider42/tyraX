# troubleshoot/tyra-testing-build.md

Building the editor (Layer 0) and the native incremental-build regression. Symptom first.

## The editor build dies with `C2026 string too big` (plus C2589/C2660)

**Cause:** you configured with MSVC. The Windows compiler is MinGW-w64 GCC; MSVC is not a supported target and no flag makes it one. `src/templates.cpp` holds the PS2 templates as raw string literals far past MSVC's hard 16380-byte cap per literal, and `wire.cpp` hits `windows.h`'s `min` macro eating `std::min`.
**Fix:** build with `./build.ps1` (MinGW). Report of that error means "you configured with the wrong kit", not a code bug — Visual Studio's default `x64-Debug` CMake preset dies on it, plus C2589/C2660 in `wire.cpp`.

## A Windows build fails on a missing `vendor/` header (`fatal error: miniaudio.h: No such file or directory`)

**Cause:** PATHEXT resolves `.CMD` before `.PS1`, so on Windows `build` runs `build.cmd`. It is a wrapper only; the guard that fetches missing dependencies lives in `build.ps1`, and the old cmd translation carried its OWN four-entry dependency list that drifted (PROGRESS 214).
**Fix:** check WHICH script ran before suspecting the code. Never give `build.cmd`/`setup.cmd` logic of their own.

## A build dies with `Cannot find source file: vendor/<something>` (`No SOURCES given to target: tyrax-editor`)

**Cause:** the dependency's checkout is not on disk yet — not a corrupt checkout.
**Fix:** run `./setup.ps1` / `./setup.sh` directly for the fetch without a build; the build script normally fixes it itself. Same after merging a branch that added a dependency (the merge brings the CMake reference, not the clone). If the guard still reports a probe absent after a fetch, delete the directory and re-run setup. Verify the pin: `git -C vendor/<dep> rev-parse HEAD` must equal the SHA in `deps.sh`; a checkout that predates pinning is stale.

## Adding a third-party dependency

**Cause:** third-party dependencies live in exactly one list per platform: `deps.ps1` and `deps.sh`.
**Fix:** add it to BOTH. `setup.ps1`/`setup.sh` fetch from them and `build.ps1`/`build.sh` probe them before configuring, so the build guard picks it up for free. Adding one anywhere else, or to only one of the two, recreates the drift bug.

## A bake reads as a hang under `-Dev`/`--dev`

**Cause:** `-Dev` is `-O1`. gibake/matbake/aobake/pngquant are raytracers and quantizers; at `-O1` a bake that takes seconds takes minutes, which reads as a hang rather than as the flag you chose.
**Fix:** never benchmark or ship a Dev build, and never use one to verify anything that bakes. Verify bakes with the Release build. `-Dev`/`--dev` is only for the edit-compile-look loop (`-O3` is roughly two thirds of the compile time of the Release build).

## A build-timing comparison proves nothing

**Cause:** back-to-back clean builds on a laptop drift ~20% from thermals alone, so a single before/after pair is noise.
**Fix:** alternate the two variants (A, B, A, B, …) and compare like rounds. Read `build/.ninja_log` (`start_ms end_ms mtime output`) rather than guessing; summing `end-start` against the wall clock gives the real parallelism (that is how the 26k-line `app.cpp` critical path and the `libimgui.a` ordering stall were found).

## A cross-platform change is only half-checked

**Cause:** only one platform's compiler runs at a time.
**Fix:** anything touching `src/platform.*`, `wire.cpp`, the Runner, CMakeLists **or any of the paired build scripts** needs a build on both, or say so in the commit. Pairs that must move together: `deps.ps1`/`deps.sh`, `setup.ps1`/`setup.sh`, `build.ps1`/`build.sh`, the `if(WIN32)`/`else()` halves of CMakeLists, the `#ifdef _WIN32`/`#else` halves of `platform.cpp`.

## The native incremental build regression

**Cause:** the native backend's incrementality has specific seams that a cold build does not exercise.
**Fix:** On Windows compare `TYRAX_NATIVE_DIRECT=1` with the default WSL filesystem cache, warming each separately. Verify `bin/` returns to the authored project, a Windows-written runtime channel survives, and DWARF paths map to authored sources. Use a private PCSX2 capture slot to boot the resulting ELF.

Use a scratch copy of `examples/vehicle-playground`. Warm the native build, then repeat `--build`: no compile/link/archive commands and unchanged ELF/archive mtimes. Jobs use the host-visible logical CPU counts — never describe the measured `-j24` as a project setting. Change one script source, then one scene value, and inspect which objects rebuilt. Touch the engine archive to verify another project's engine rebuild relinks the game without recompiling it. Change a Makefile to verify flags invalidate objects. A failed engine compile must fail again on retry until fixed. For IRX ABI changes compare `objcopy` binary payloads and check the link has no `abicalls` mismatch warnings. Toolchain identities must match for byte-identical source trees at different absolute paths. Exclude setup/cold builds from timings.

## Build commands

```powershell
./build.ps1          # → build/tyrax-editor.exe (fetches missing vendor deps itself)
./build.ps1 -Run     # build + launch the GUI
./build.ps1 -Clean   # full rebuild
./build.ps1 -Dev     # -O1 iteration build → build-dev/tyrax-editor.exe
```

```bash
./setup.sh --deps    # one-time on a bare box: EDITOR toolchain + dev headers,
                     # AND the game half (curl/tar/rsync) - one command, both
./build.sh           # → build/tyrax-editor
./build.sh --run     # build + launch the GUI
./build.sh --clean   # full rebuild
./build.sh --dev     # -O1 iteration build → build-dev/tyrax-editor
```

A clean Release build is ~1-1.5 min on 16 cores; a one-file edit to the UI is a few seconds. Needs `scoop install mingw cmake ninja` (Windows) or `./setup.sh --deps` (Linux — apt/dnf/pacman/zypper; the lists live in `deps.sh`). If ccache/sccache is on `PATH` CMake uses it automatically. `build.cmd` / `setup.cmd` exist for plain `cmd.exe` and double-clicks and map `run`/`clean`/`dev` onto `-Run`/`-Clean`/`-Dev`.
