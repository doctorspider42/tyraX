# Building the editor, platform layer and parity, dependencies, packaging, updates

Read when you touch build/setup/deps scripts, `CMakeLists.txt`, `platform.cpp`, path or shell-command handling, the installer/packages/release workflow, or the update check (`update.cpp`, `update_ui.cpp`). For TESTING what you built, read tyra-testing.

## Building

```powershell
./build.ps1          # configure (if needed) + build -> build/tyrax-editor.exe
./build.ps1 -Run     # build and launch
./build.ps1 -Clean   # nuke build/ first
./build.ps1 -Dev     # -O1 iteration build into its OWN build-dev/
```
```bash
./setup.sh --deps    # one-time: toolchain + dev headers (apt/dnf/pacman/zypper)
./build.sh           # -> build/tyrax-editor   (--run, --clean, --dev as above)
```
- `build.cmd`/`setup.cmd` are thin wrappers calling the `.ps1` (and a bare `build` in PowerShell runs `build.cmd`: PATHEXT puts `.CMD` before `.PS1`).
- `Dev` build type (`-O1`, own directory) is for iterating on UI/model code; its host bakes (gibake, matbake, aobake, pngquant) are genuinely slow - never benchmark or ship one. CMake picks up **ccache/sccache** off `PATH` (`-DTYRAX_COMPILER_CACHE=OFF` opts out) - useful with several worktrees.
- Toolchain: Windows `scoop install mingw cmake ninja` (build.ps1 finds scoop's mingw off-PATH); Linux `./setup.sh --deps` reads per-family lists in deps.sh (`SYSTEM_PACKAGES_apt`/`_dnf`/`_pacman`/`_zypper`) and installs via sudo (pkexec without a tty). build.sh only DIAGNOSES (checks tools and pkg-config headers, prints that command). **A new system dependency goes into all four lists** (zenity is there because file dialogs shell out to it).
- Native PS2 toolchain pair: `tools/toolchain/prepare-host.ps1` is only the Windows-to-WSL bridge; `prepare-host.sh` owns the prerequisite check and the apt list. Setup and normal builds call check mode; only explicit `-Install`/`--install` may mutate a distribution. Keep installer integration opt-in and `dontinheritcheck` (or an editor update runs apt again).

## CMake

- Single target `tyrax-editor`. **A new source file must be added to `CMakeLists.txt`'s source list - there is no glob** (only `docs/*.md` and `ai-support/*` are globbed for embedding). Windows: static MinGW (`-static`), console subsystem on purpose (logs visible), links shell32/ole32/uuid/ws2_32. Elsewhere: `OpenGL::GL` + Threads + `${CMAKE_DL_LIBS}`, app icon `.rc` skipped.
- Generated headers (`docs_gen.hpp`, `ai_support_gen.hpp`, embedded binaries) live in their own `tyrax-generated` custom target: a generated header in the executable's source list attaches its `add_custom_command` to the target and inherits order-only deps on everything it LINKS (two 0.1 s generators waited for `libimgui.a`, and every editor `.obj` with them: 17 s of every clean build). Keep new generators there.
- Keep TUs splittable (editor-ui-and-conventions.md).

## Dependencies

- Missing `vendor/` deps are fetched by `setup.ps1`/`setup.sh` (imgui docking, glfw 3.4, imguizmo, imnodes, stb, ufbx, miniaudio - git-ignored; `vendor/tyra` is versioned, see tyra-engine-dev); the build script runs setup when something is absent. **The dependency list lives ONLY in `deps.ps1`/`deps.sh`** - add a library to BOTH and nowhere else (one-sided: cmake says `Cannot find source file: vendor/<x>` on the other platform).
- Every entry pins an exact `Commit` SHA and names a `Mirror` (our `doctorspider42/tyrax-vendor-*` fork, used when upstream fails); `Ref` is documentation only. **Never put a branch name where a SHA belongs** (setup skips a directory whose probe exists, so a branch pin freezes at whatever HEAD that machine fetched). Bump: new SHA in both lists (`gh repo sync` the mirror first), delete the vendor dir, re-run setup, build. A new dependency needs its notice in `THIRD-PARTY-LICENSES.md`; if small and redistributable, prefer vendoring in-tree.

## Platform parity: files that exist twice

**Editing one member of a pair without its twin, in the same commit, is a bug.**

| Windows | Linux/macOS | Must stay in step on |
|---|---|---|
| `deps.ps1` | `deps.sh` | every third-party dependency - the ONLY place one is listed |
| `setup.ps1` | `setup.sh` | how the lists are fetched |
| `build.ps1` | `build.sh` | flags (`-Run`/`--run`, `-Clean`/`--clean`, `-Dev`/`--dev`), the dep guard, the toolchain check |
| `build.cmd`, `setup.cmd` | - | nothing: thin wrappers over the `.ps1`; keep them that way |
| `tools/toolchain/prepare-host.ps1`, `native-build.ps1` | `prepare-host.sh`, `native-build.sh` | the `.ps1` side is a WSL bridge over the `.sh` |
| `installer/build-installer.ps1` + `tyrax.iss` | `installer/build-package.sh` | WHAT IS IN THE PACKAGE (binary, engine, tools, VU sources, examples, licences in one exe-relative shape); the scripts share no lines |
| `CMakeLists.txt` `if(WIN32)` | its `else()` | link libraries, compile options |
| `platform.cpp` `#ifdef _WIN32` | its `#else` | every function in `platform.hpp` |

- `build.cmd`/`setup.cmd` once were full cmd translations with a hardcoded four-entry dependency list that froze while `deps.ps1` grew to seven - `fatal error: miniaudio.h: No such file or directory` on Windows only (PROGRESS 214). Don't put logic back into wrappers. A new per-platform file: add its twin in the same commit or make it a wrapper over the existing one.
- Game templates have their own pair: `TPL_GAME_HPP_ORBIT`/`TPL_GAME_HPP_FPP` and the two main heads (codegen-and-generated-files.md).

## The platform layer (`platform.cpp/.hpp` ~1.34k+325)

**The ONE place OS differences live**; a feature needing to know the OS grows an entry HERE and the call site stays platform-blind (~40 sites free of `#ifdef`). Covers: `exePath`/`configDir` (machine-global config root: `%LOCALAPPDATA%\tyra-editor` vs `$XDG_CONFIG_HOME/tyra-editor` - editor.ini, session remote-cache, exported PS2SDK headers, chats, bone aliases)/`homeDir`/`userName`/`exeSuffix`/`processId`, `sleepMs`/`logTimeStamp`, shell fragments (`quiet`, `envPrefix`, `commandExists`, `shellArg`, `containerFileOwner`), **`Process`**, process inventory (`processesNamed`/`commandLineNamesPath`/`killProcess`), pickers (`pickFile`/`pickFolder`/`errorBox`/`setDialogOwner`), `revealInFileManager` (walks up to the nearest existing ancestor; normalizes at the OS boundary), `copyImageToClipboard` (`CF_DIBV5` on Windows; `wl-copy`/`xclip` with the saved PNG on Linux), **`openUrl`** (ShellExecute vs xdg-open; REFUSES anything not http/https - URLs come off the network), `openInVSCode`, `installDesktopEntry`, font lookup (`systemFontPath`/`systemFonts`/`fallbackFontFiles`/`defaultFontLabel` for game bakes; **`uiFontFiles`** for the editor's own interface font).
- **`Process`**: one shell command line (`cmd.exe /S /C` vs `/bin/sh -c`), optional stdout capture (`readLine`/`readAll`), optional stderr-to-file, `running()`/`wait()`/`startDetached()`, and **`kill()` takes down the whole TREE** (Job Object vs `setsid()` process group). Never "improve" it to kill only the child: the shell wrapper is never the worker (docker, make, node, curl, ps2client) and killing it alone orphans a token-burning backend or a port-holding file server. `kill()` is safe from another thread (the update check's `cancel()` relies on it).
- **There is no `killByName`**: `taskkill /F /IM <x>` was wrong twice (a deploy killed another project's live session; a launch interrupted other worktrees' PCSX2 measurements). Callers use `processesNamed` + `killProcess` and decide ownership from `RunningProcess::commandLine`; empty = "the OS would not say" = NOT MINE. The discriminator is the command line rather than cwd because Windows has no supported way to read another process's cwd (remote PEB) - a key only one platform can compute would make the two behave differently.
- Deliberately outside: socket shims in `wire.cpp` (Winsock2 mapped in place) and PCSX2/`PCSX2.ini` discovery in `runner.cpp`/`pcsx2_config.cpp`.
- Linux specifics: file dialogs shell out to **zenity** (kdialog fallback; neither = no Open/Import); system fonts resolve through a lazily built filename->path index over freedesktop roots; SIGPIPE is ignored process-wide from a static here (a dying child's pipe or vanished peer would kill the editor); `installDesktopEntry` writes the `.desktop` + hicolor icon - under **Wayland the only icon mechanism** (compositor matches the surface app id to a desktop file; `glfwSetWindowIcon` fails with `GLFW_FEATURE_UNAVAILABLE`), so the app id hinted in `App::run`, the desktop file basename and the icon name are one `kAppId` constant.

## Paths and shell command lines

- **Never hand-join a path with `"\\"`** (outside Windows a backslash is a filename character; the asset just fails to load). Use **`Project::filePath(rel)`** for project-relative asset paths and **`templates::nativePath(rel)`** for `templates::File::relativePath` (those stay `\`-separated because hundreds of literals compare against them; only the four FS meeting points convert). The first Linux run produced ~30 files literally named `src\gen\flow_graph.gen.cpp` and handed PCSX2 `.../name\bin\name.elf`.
- **Never hand-roll the join or assume normalization**: `fs::path(dir) / rel` concatenates, so a forward-slashed `rel` on Windows gives `C:\proj\bin/proj.elf`, which C++/CRT accept (every `fs::exists` passes) while external programs reject it (PCSX2 v2.6.3 `Requested boot ELF ... does not exist`; `explorer.exe /select,"<mixed>"` opens the default folder). `filePath()` ends in `make_preferred()`; `App::assetAbs` delegates to it; normalize anything built by hand before it leaves the process (PROGRESS 193).
- **Anything nested in a command line goes through `platform::shellArg()`**: `cmd.exe` expands nothing in double quotes, but `/bin/sh` expands `$(...)`/`${...}` on the HOST (it emptied the in-container sfx loop's variables and ran `$(nproc)` on the wrong machine). Same for paths that may contain `$` or a backtick.
- **The build container runs as root**: on plain Linux Docker its writes into the bind-mounted project are root-owned; the copy-back rsync passes `--chown=` + `platform::containerFileOwner()` (empty on Windows). Any new container->host copy needs the same.
- PCSX2 path limits and absolute `-elf`: devkit-runner-and-network.md.

## Packaging and releasing (docs/updates.md)

`installer/tyrax.iss` (Inno Setup 7) + `installer/build-installer.ps1` (Windows), `installer/build-package.sh` (Linux: one staged tree -> `.tar.gz` + `.deb` + `.rpm`), `.github/workflows/release.yml` runs both on every push to main.
- **The installed layout is the repo layout**: the editor resolves the engine, PS2 tools, the `.vsix` and VU framework sources from `<exe>/../` (binary in `bin/`). **A new exe-relative lookup = a `[Files]` entry in tyrax.iss AND a line in build-package.sh's `stage_tree`**, same commit.
- **Exclude by DIRECTORY, never file type; if git keeps it, the package ships it.** Excluding `*.o`/`*.a`/`*.elf` from `vendor/tyra` dropped the COMMITTED `vendor/tyra/audsrv/bin/libaudsrv.a` (overlaid by runner.cpp onto the container's PS2SDK); the overlay's `cp a && cp b && cp c` died before copying the header, so installed copies failed with "'audsrv_adpcm_set_volume_and_pan' was not declared" for two releases (fixed 1.55.3). The exclusion list = what `.gitignore` drops under that directory; `runner.cpp` checks the three overlay files by name.
- **`src/version.hpp` authors the version; TAGS say which patches are spent.** Packaging scripts and the workflow read the three `TYRAX_VERSION_*` macros with the same regex; CI releases them as-is when `v<that>` is untagged, else one PATCH past the highest `v<MAJOR>.<MINOR>.*` tag, stamped into a WORKSPACE copy of the header before compiling (never committed - the branch ruleset requires PRs; the commit-back design died with GH013; tag pushes are exempt). The file's PATCH is a floor between releases. Bump MINOR by hand for a feature. **The Linux job must stamp too** (else tarball installs offer themselves an update forever).
- **One function decides whether an install can update itself**: the Windows installer installs PER USER (no UAC), `.deb`/`.rpm` are root's, a checkout is the developer's. build-package.sh stages a one-word `.tyrax-package` marker (`tarball`/`deb`/`rpm`; absent = checkout); `update::installKind` reads it; `update::selfInstallBlocked` returns "" or the ONE sentence of what to do instead (shown in place of the button). A new channel = a marker word + a case there.
- **The Linux runner image is a compatibility floor**: `runs-on: ubuntu-22.04` in `build-linux` means "Ubuntu 22.04+, Debian 12+, Fedora 36+" (glibc is not forward compatible); bumping it drops distributions silently.
- Asset NAMES are not a contract, SUFFIXES are: `update::platformAssetSuffix` matches `.exe` and `-linux-x86_64.tar.gz` by tail.
- Other workflows: `toolchain-image.yml` (Docker toolchain image to GHCR from `docker/Dockerfile`), `blss-default-net.yml`.

## Update check (`update.cpp/.hpp` ~410+115, `update_ui.cpp` ~260)

Host-only `update.cpp` (`compareVersions`, `parseRelease` are pure; settled from a harness). (1) **Transport is curl** (wire.cpp has no TLS); no curl = no update check, nothing else lost, so every failure is a message, not a dialog. (2) **No `-f`; HTTP status via `-w`** (`-f` collapses "no releases yet", rate limits, refusals into one exit code). (3) **A failed STARTUP check is silent**; only an asked-for one reports. `Skip this version` (editor.ini `updateSkipVersion`) silences only the automatic check. (4) **`cancel()` kills the curl in flight** (raw `Process*` under a mutex) - anything shelling out on a worker owes the same. Install: hands the installer `/SILENT /RELAUNCH=1`, exits via `requestExit()` (asks about unsaved work); the `[Code]` block in `installer/tyrax.iss` relaunches. `updateTick()` runs EVERY frame from `drawUI`.
