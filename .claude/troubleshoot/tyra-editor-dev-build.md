# tyra-editor-dev — building, packaging, VU aliases

From `## Building the editor`, `### Packaging and releasing`, `### VU clip-image
aliases`, and `### 4b`/`### 4c` (paths, platform parity). See
`docs/updates.md`, `docs/vu-framework.md`, `docs/native-toolchain.md`.

## Building the editor
```powershell
./build.ps1          # configure (if needed) + build → build/tyrax-editor.exe
./build.ps1 -Run     # build and launch
./build.ps1 -Clean   # nuke build/ first
./build.ps1 -Dev     # -O1 iteration build into its OWN build-dev/
```
```bash
./setup.sh --deps    # one-time: toolchain + dev headers (apt/dnf/pacman/zypper)
./build.sh           # configure (if needed) + build → build/tyrax-editor
./build.sh --run     # build and launch
./build.sh --clean   # nuke build/ first
./build.sh --dev     # -O1 iteration build into its OWN build-dev/
```
`build.cmd` / `setup.cmd` are **thin wrappers** over the `.ps1` (so a bare `build`
in PowerShell runs `build.cmd`; PATHEXT puts `.CMD` before `.PS1`).

## A single huge source file becomes the whole build's critical path
**Cause:** `-O3` is about two thirds of the project's compile time and scales
worse than linearly with TU size — app.cpp at 26k lines took ~48 s on its own,
and any UI edit paid that.
**Fix:** when a window or subsystem grows past roughly two thousand lines, give it
its own `*_ui.cpp` — App:: members declared in app.hpp, definitions in the new
file, the assetbrowser.cpp/save_assets.cpp precedent. It is now the shell plus
seven subsystem TUs (~9 s). `templates.cpp` (27k lines) is the one left. The `Dev`
build type (`-O1`, own directory) is for iterating; its host bakes (gibake,
matbake, aobake, pngquant) are genuinely slow, so never benchmark or ship one.
CMake picks up **ccache/sccache** off `PATH` automatically
(`-DTYRAX_COMPILER_CACHE=OFF` opts out).

## A generated header slows every clean build
**Cause:** a generated header listed straight in `tyrax-editor`'s sources makes
CMake attach its `add_custom_command` to that target, which inherits order-only
deps on everything the target links — two 0.1 s generators waited for
`libimgui.a`, and with them every editor `.obj`.
**Fix:** keep generators in their own `tyrax-generated` custom target. Single
CMake target `tyrax-editor`; Windows statically linked (MinGW `-static`), console
subsystem, links shell32/ole32/uuid/ws2_32; elsewhere `OpenGL::GL` + Threads +
`${CMAKE_DL_LIBS}`. **A new source file must be added to `CMakeLists.txt`'s single
source list** — there is no glob.

## A new dependency is missing on one platform
**Cause:** the dependency list lives only in `deps.ps1` / `deps.sh`, which the
setup and build scripts read. Missing `vendor/` deps are fetched by `setup.ps1` /
`setup.sh` (imgui docking, glfw 3.4, imguizmo, imnodes, stb, ufbx, miniaudio — all
git-ignored; `vendor/tyra` is versioned).
**Fix:** add a new third-party library **to both** and nowhere else, or one
platform's build guard won't know about it. Every entry pins an exact `Commit` SHA
and names a `Mirror` (our `doctorspider42/tyrax-vendor-*` fork); `Ref` is
documentation only. **Never put a branch name where a SHA belongs** — setup skips
a vendor directory whose probe already exists, so a branch pin freezes silently. To
bump: new SHA in both lists (`gh repo sync` the mirror first), delete the vendor
directory, re-run setup, build. A new dependency also needs its license notice in
`THIRD-PARTY-LICENSES.md`. Toolchain: Windows `scoop install mingw cmake ninja`;
Linux `./setup.sh --deps` reads the per-family lists in deps.sh
(`SYSTEM_PACKAGES_apt`/`_dnf`/`_pacman`/`_zypper`). **A new system dependency has
to be added to all four lists** (zenity is in them because the file dialogs shell
out to it).

## Packaging and releasing
**Cause/rule:** `installer/tyrax.iss` (Inno Setup 7) +
`installer/build-installer.ps1` package a built editor on Windows,
`installer/build-package.sh` does the Linux half (one staged tree → `.tar.gz` +
`.deb` + `.rpm`), and `.github/workflows/release.yml` runs both on every push to
main (docs/updates.md). Four things bind them to the code:
**Fix:**
- **The installed layout is the repo layout.** The editor resolves the engine, the
  PS2 tools, the `.vsix` and the VU framework sources from `<exe>/../`. **A new
  exe-relative lookup is a new `[Files]` entry in tyrax.iss AND a line in
  build-package.sh's `stage_tree`** in the same commit. The two scripts are a
  platform pair in PURPOSE — what must not drift is the CONTENT of the tree.
- **A package carries the repo's CONTENT, so exclude by DIRECTORY — never by file
  type.** Both packagers once dropped `*.o`, `*.a` and `*.elf` from `vendor/tyra`
  and took the COMMITTED `vendor/tyra/audsrv/bin/libaudsrv.a` with them; the
  overlay's `cp a && cp b && cp c` died before copying the header, and every game
  build ended in "'audsrv_adpcm_set_volume_and_pan' was not declared" on INSTALLED
  copies only. The rule: **if git keeps it, the package ships it.**
- **`src/version.hpp` is where the version is AUTHORED, and the TAGS say which
  patches are spent.** CI releases them as they stand when `v<that>` is untagged,
  else goes one PATCH past the highest `v<MAJOR>.<MINOR>.*` tag, and stamps that
  number into a WORKSPACE copy of the header before compiling, never a commit.
  Bump MINOR by hand for a feature. **The Linux job must stamp it too**, or every
  tarball install offers itself an update for ever.
- **Not every install can update itself, and ONE function decides.**
  `installer/build-package.sh` stages a one-word `.tyrax-package` marker
  (`tarball`/`deb`/`rpm`; absent = source checkout), `update::installKind` reads it
  and `update::selfInstallBlocked` returns either "" or the ONE sentence naming
  what to do instead. A new distribution channel is a marker word and a case there.
- **The Linux job's runner image is a compatibility floor** — `runs-on:
  ubuntu-22.04` says "Ubuntu 22.04+, Debian 12+, Fedora 36+"; bumping it drops
  distributions silently.
The release's asset NAMES are not a contract but their SUFFIXES are:
`update::platformAssetSuffix` matches `.exe` and `-linux-x86_64.tar.gz` by tail.

## VU clip-image aliases
**Cause/rule:** the five built-in clip classes occupy three resident code images:
`C/D` share the generated C image, `TC/TCE` share TC, and `TD` stays specialised.
`VU1_OPTIONS_ADDR.y` selects the peer path. The pairing is declared in ONE place —
`Desc::residentImageAsmName` / `codeOwner`, next to the `sharedClip*` flags that
build the two-path body. The flags say "my body carries a peer's path"; those two
fields say "somebody else's body carries mine", and every consumer reads the
second: `vugen::residentImage` is what the emitted EE wrapper links, what
`--vu-check`'s budget charges, and what `engineCrossingInstructions` in vu_ui.cpp
prices (via `vugen::engineDesc`, NOT a second copy of the rule). The one piece that
lives in the engine is `Path1::createProgramsCache`.
**Fix:** two `--vu-check` sections are mandatory when touching any of it, and
neither is implied by the per-description equivalence run: `-- shared clip images,
peer paths --` compares each shared image's variant one against the specialised
D/TCE source, and `-- EE wrappers ... --` holds both the emitted wrapper and the
one in `vendor/tyra` to the description's image and its two `StaPipVU1Program` ABI
numbers. Undo the sharing in a wrapper by hand and every microcode section still
says IDENTICAL — only that section fails. `elementsPerVertex` (the last ctor
argument) is `attrStreams + 1` for a LIT program and `attrStreams` otherwise
(`getMaxVertCount` subtracts one element when the mesh draws in a single colour,
and a lit program never unpacks colours). Only the CULL program's numbers are ever
consulted (`getMaxVertCountByBag` asks `getCullProgramByBag`).

## Reference facts (upstream `build-platform-release.md`)
- CMake single target `tyrax-editor`; a new source goes in `CMakeLists.txt` (no glob; only `docs/*.md` and `ai-support/*` globbed). Windows static MinGW (`-static`), console subsystem, links shell32/ole32/uuid/ws2_32; elsewhere `OpenGL::GL` + Threads + `${CMAKE_DL_LIBS}`. Generated headers live in their own `tyrax-generated` target. `Dev` build type (`-O1`, own dir); ccache/sccache off `PATH` (`-DTYRAX_COMPILER_CACHE=OFF`). Toolchain: Windows `scoop install mingw cmake ninja`; Linux `./setup.sh --deps` reads `SYSTEM_PACKAGES_apt`/`_dnf`/`_pacman`/`_zypper`. Native PS2 pair `tools/toolchain/prepare-host.ps1` (only the WSL bridge) / `prepare-host.sh` (prereqs + apt list); `dontinheritcheck`.
- Dependencies: `deps.ps1`/`deps.sh` ONLY; each entry pins an exact `Commit` SHA + a `Mirror` (our `doctorspider42/tyrax-vendor-*`); `Ref` is documentation only; never a branch name where a SHA belongs. A new dependency needs its notice in `THIRD-PARTY-LICENSES.md`.
- Platform layer `platform.cpp/.hpp` (~1.34k+325): the ONE place OS differences live (`exePath`/`configDir` - `%LOCALAPPDATA%\tyra-editor` vs `$XDG_CONFIG_HOME/tyra-editor` - /`homeDir`/`userName`/`exeSuffix`/`processId`, `sleepMs`/`logTimeStamp`, shell fragments (`quiet`, `envPrefix`, `commandExists`, `shellArg`, `containerFileOwner`), `Process`, `processesNamed`/`commandLineNamesPath`/`killProcess`, pickers (`pickFile`/`pickFolder`/`errorBox`/`setDialogOwner`), `revealInFileManager`, `copyImageToClipboard` (`CF_DIBV5` on Windows; `wl-copy`/`xclip` on Linux), `openUrl` (http/https only), `openInVSCode`, `installDesktopEntry`, font lookup (`systemFontPath`/`systemFonts`/`fallbackFontFiles`/`defaultFontLabel`, and `uiFontFiles` for the editor UI font)).
- `Process`: one shell command line (`cmd.exe /S /C` vs `/bin/sh -c`), optional stdout capture (`readLine`/`readAll`), stderr-to-file, `running()`/`wait()`/`startDetached()`; `kill()` takes down the whole TREE (Job Object vs `setsid()`). There is no `killByName`; callers use `processesNamed` + `killProcess` + `RunningProcess::commandLine`. Linux dialogs shell out to zenity (kdialog fallback); SIGPIPE ignored process-wide; `glfwSetWindowIcon` fails with `GLFW_FEATURE_UNAVAILABLE` under Wayland (desktop file + hicolor icon, one `kAppId`).
- Paths and quoting: `Project::filePath(rel)` / `templates::nativePath(rel)`; never hand-join (`fs::path(dir)/rel` concatenates); `App::assetAbs`; nested command-line args via `platform::shellArg()`; the container runs as root so copy-back rsync passes `--chown=` + `platform::containerFileOwner()`.
- Packaging: `installer/tyrax.iss` (Inno Setup 7) + `installer/build-installer.ps1`; `installer/build-package.sh` -> `.tar.gz` + `.deb` + `.rpm`; `.github/workflows/release.yml`. Installed layout = repo layout; exclude by DIRECTORY, never file type; `src/version.hpp` authors the version, TAGS say which patches are spent; `.tyrax-package` marker word (`tarball`/`deb`/`rpm`); `update::installKind`/`selfInstallBlocked`; `runs-on: ubuntu-22.04` is the compatibility floor; `update::platformAssetSuffix` matches `.exe`/`-linux-x86_64.tar.gz`. Other workflows: `toolchain-image.yml` (from `docker/Dockerfile`), `blss-default-net.yml`.
- Update check `update.cpp/.hpp` (~410+115), `update_ui.cpp` (~260): `compareVersions`/`parseRelease` pure; transport is curl (no `-f`, status via `-w`); a failed STARTUP check is silent; `Skip this version` (`updateSkipVersion`); `cancel()` kills the curl in flight; install hands the installer `/SILENT /RELAUNCH=1` and exits via `requestExit()`; `updateTick()` runs every frame from `drawUI`.

## VU framework reference facts (upstream `vu-framework.md`)
- `vusim.{hpp,cpp}` EXECUTES vuir on `Target::VU1`/`VU0` (1024 vs 256 qwords data memory, 2048 vs 512 micro slots; `xgkick`/`xtop` warned on VU0; `runKernel` for the `vcallms` contract). `vucap.{hpp,cpp}` decodes `bin/vucap.bin`; `vucap::scanGifPackets` is THE GIF decoder; `vucap::Capture::vuMem` is the same memory image `vusim` ends with. The emitted EE wrapper is `addProgramQBufferDataToPacket`. The MOVE FAMILY (`move`, `mr32`, `mfir`, `ftoi*`, `lq`/`sq`) carries integer bit patterns; VCL not microcode. `vushader` registry behind `VU_PROGRAM`; `vumain.cpp` is the build-container generator; samples `vushader_sample.cpp`/`vushader_palette_sample.cpp`/`vushader_snap_sample.cpp`/`vushader_wobble_sample.cpp`/`vukernel_sample.cpp`.

## VU float identifiers (upstream)
- The VU FPU has no inf/NaN; overflow saturates to `0x7F7FFFFF` and denormals are zero, so every float goes through `vuFloat()` except the MOVE FAMILY.
