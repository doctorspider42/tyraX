# troubleshoot/tyra-testing-e2e.md

Layer 3 — full e2e: native build + PCSX2 boot, host prep, gyms and the launcher. Symptom first. Driving the pad lives in `tyra-testing-pad.md`.

## Editor Play boots the saved startup scene, not the one I picked

**Cause:** Build-only and CLI launches still use the saved startup scene.
**Fix:** for editor Play, select a scene other than the saved startup scene and launch from the toolbar or F5. Check the Runner log's `Play scene:` line and the game's loaded scene, then check that `bin/launch.scene` was consumed. The marker must never enter an exported ISO.

## `--build examples/<name> --run` boots to an untextured scene / black screen

**Cause:** a RELATIVE project path used to reach the native build script and PCSX2: the script's `cd` failed outright, and PCSX2 derived the `host:` asset root from the relative ELF, so the game booted untextured with no baked shadows and nothing in any log saying why. (A relative `-elf` is a separate black-screen failure: PCSX2 may rebase it below the ELF directory, giving `Denying access` / `Failed to read ELF` and entry point `0xFFFFFFFF`.)
**Fix:** both are absolutised at the source now (`Project::elfPath`, and the Runner's native-build arguments); the ~145-character ELF limit is measured on the path PCSX2 really sees. Reproduce launcher regressions with `--build ./examples/foo --run`, deliberately keeping the CLI project path relative, then inspect the running process's `-elf` argument. Keep the project path relative in one native-backend regression run — the Runner must resolve it before invoking `native-build`, which changes into the project (forwarding `./examples/foo` verbatim became `examples/foo/examples/foo`). A relative `-elf` can also be rebased by PCSX2 into `examples/foo/bin/examples/foo/bin/foo.elf`.

## Host preparation is missing

**Cause:** the game half of the toolchain is separate from the editor half.
**Fix:** check with `bash tools/toolchain/prepare-host.sh`; install missing Debian/Ubuntu packages with `--install`. On Linux `./setup.sh --deps` already covers this (its four distro lists carry `curl`, `tar` and `rsync`). A manual `tools/toolchain/setup.sh` installs into `configDir()/toolchain/ps2dev` — the same directory the editor uses. On Windows, `prepare-host.ps1 -Install` reaches the same script inside the default WSL distribution. A normal build never runs `apt`.

## Testing a change to the toolchain image itself

**Cause:** `docker/Dockerfile` (the inherited A/B reference) is **not built by CI at all** — local is the only check it gets.
**Fix:** build with `docker\build.ps1` / `docker/build.sh` (add `-FromSource` / `--from-source` for `docker/Dockerfile.fromsource`, the one CI publishes). Both scripts run the same checks CI does. Point a scratch project at it with one line, `TYRAX_IMAGE=tyrax-toolchain:local` in the project's `.env`, and take it to a PCSX2 boot. `docker compose config` prints which image will be used; `docker ps -a --filter name=<project>` confirms which one the container was created from. Nothing in the editor needs rebuilding — `docker-compose.yml` is regenerated per build and reads that variable (docs/toolchain-image.md).

## What happens on `--build <projectDir> --run`

**Cause:** the Runner pipeline (`src/runner.cpp`).
**Fix:** generated files refresh → verified PS2DEV and the vendored VU tools are provisioned if needed → engine sources checksum-synced into the native cache, `libtyra` rebuilt if changed (VU1 microprograms only when a VU source changed) → project rsynced → `make -j` → WAV sfx converted with `adpenc` → `bin/` synced back → existing PCSX2 processes killed → `HostFs = true` forced in `PCSX2.ini` → PCSX2 launched on the ELF. For every `*-loop.wav`, verify byte 6 of the generated `.adpcm` is `1` (`od -An -tu1 -j6 -N1`); a newer output with byte `0` must be re-encoded with `adpenc -L`.

## An image swap rebuilds nothing

**Cause:** the incremental logic keys off source timestamps, and an image swap touches no source, so the previous image's objects were relinked and the new toolchain appeared to change nothing (three consecutive `VCL_FLAGS` probes each booted the previous probe's VU microcode).
**Fix:** since 2026-08-05 the Runner stamps the VU assembler itself (`/tyra/.vcl-stamp`) and prints `VU assembler changed - rebuilding the microprograms`; the stamp hashes the resolved `vcl`, `vclpp` AND the `openvcl` binary behind the wrapper. Look for that line after a swap, and verify from the log that the work happened — `grep -cE '(^| )vcl '` (one line per microprogram, 25 of them) and `grep -c 'elf-g++ .* -c -o'`. A full microcode rebuild is ~2 min under `vcl` but **seconds** under openvcl.

## Measuring a build by the clock

**Cause:** the whole pipeline is incremental.
**Fix:** measure by what it RECOMPILED — `grep -c 'elf-g++ .* -c -o'` over the build log. On `examples/showcase` (18 TUs, 6 cores): nothing changed is **0 compiles / ~5 s**, one edited game source is **1 / ~9 s**, a moved scene object is **13 / ~47 s**, an engine `.cpp` is **~7 s**, an engine `.vclpp` is **~2 min**, and `--rebuild` is **~5 min** from nothing. A "no changes" build that still compiles things is a regression; find it by comparing mtimes in the container against `/src/obj/*.d`. `--rebuild` is the escape hatch; `Clean` also wipes the host's `bin/`.

## PCSX2.ini is not found / settings land in the user's real directory

**Cause:** PCSX2.ini is found portable-first (an `inis/` next to the executable), then per OS. On Windows **Documents may be OneDrive-redirected** (e.g. `...\OneDrive - <org>\Dokumenty\PCSX2\`); on Linux `$XDG_CONFIG_HOME/PCSX2/inis` or the flatpak sandbox (`~/.var/app/net.pcsx2.PCSX2/config/PCSX2/inis`). `logs/emulog.txt` and `snaps/` sit beside it. PCSX2 itself is auto-detected in `Program Files\PCSX2`, or on Linux from PATH / flatpak / an AppImage under `~/Applications` or `~/Downloads`, else *Edit > Preferences*.
**Fix:** normalize Windows PCSX2 CLI paths with `[IO.Path]::GetFullPath()` (in 2.9.84 a forward-slash `C:/...` to `-elf` reported an existing ELF did not exist; a `C:\...` booted; normalize `-logfile` too). `-datapath` needs a build from Apr 2026 on (stable 2.8.2+, nightlies ≥ 2.9.84); probe `pcsx2-qt -help` first — PCSX2 is a GUI-subsystem exe so usage arrives on **stderr**. For an isolated `-datapath <parent>`, settings were created under `<parent>/PCSX2/inis`, NOT `<parent>/inis` — inspect before diagnosing a first-run wizard. Rewrite every PCSX2 `[Folders]` key to an absolute private path, matching keys case-insensitively (the canonical key is `SaveStates`, not `Savestates`); after writing the private ini, re-read `[Folders]` and assert every entry is inside the run directory (`prepare_datapath`). `--capture-frame` reads the GS raster without touching `ScreenshotSize` in PCSX2.ini.

## Gym runs

**Cause:** a project on the gym harness ships one gym per feature; "no feature lands without a gym".
**Fix:** `pcsx2-capture.py gym` exits non-zero on any `[GYM] FAIL` and drops a fail savestate per failure; the verdict source is the GAME's `log.txt` next to the staged ELF, not PCSX2's emulog. While building a feature run ONLY its gym (`scripts\gym.cmd --only <gym-name>`, comma-separated); when finished run the full suite; and a gym must pass ALONE (`scripts\gym.cmd --isolated`).

## An emulator window pops up / a suite throws windows over the machine

**Cause:** `-nogui` hides only PCSX2's *main* window; the GS display surface is its own top-level window.
**Fix:** `pcsx2-capture.py gym` hides the GS window at the OS level by default (pass `--show-window` to watch). Hiding does NOT stop rendering or `--capture-frame`. A project's `*-gui` wrapper (`scripts\gym-gui.cmd`) is the ONE thing that shows a window.

## The full suite renders wrong / is slower than Null

**Cause:** the full suite must run the VALID renderer, not Null.
**Fix:** default to **GS Software (13)** — real rasterization, so a rendering regression cannot hide and `--capture-frame` still works. With the frame limiter off and `VsyncEnable` forced off, Software GS is also *faster* than Null on a modern host (measured: 20.4 s vs 22.1 s for Gunner's 42 gyms). Reach for Null GS only for a deliberate logic-only pass, and never make it the default.

## Reading the results

**Cause:** the game's own log and PCSX2's log are different files.
**Fix:** `bin/log.txt` (host: fs) holds `TYRA_LOG` output plus the assertion dump, bracketed between `======= TYRA =======` and `================` — grep the banner rather than screenshotting for text, and remember a failed assert halts quietly with the last frame up. `logs/emulog.txt` (beside PCSX2.ini) is the emulator's own log: success reads `ELF <path> is executing`; `TYRA_LOG`/EE printf never lands there. `--symbolize <dir> 0x...` runs the container's `mips64r5900el-ps2-elf-addr2line` over `bin/<name>.elf.sym`; DEBUG builds keep that `.sym` because `vendor/tyra/Makefile.base` writes it when the generated Makefile sets `KEEPSYM=1` (a release ELF is `strip --strip-all`ed), and it needs the build container up.

## A game asserts on asset loading ("Failed to load ...png")

**Cause:** missing `HostFs` = assert on the first fopen; the editor enforces it, but PCSX2 rewrites its ini on exit.
**Fix:** check `HostFs` first. The launcher also configures PCSX2's emulated USB ports (USB1 = HID keyboard, USB2 = HID mouse) whenever the project's *Keyboard & mouse controls* preference is on; `bin/log.txt` prints `KbdMouse: keyboard driver ready` / `mouse driver ready` when the game saw the devices.

## ISO / cdrom0 testing

**Cause:** host: boots skip the path-conversion code.
**Fix:** use `Project > Export PS2 ISO`, then boot the ISO in PCSX2.

## A boot produces nothing but `TLB Miss` spam / a black window

**Cause:** PCSX2's host: loader gives up on an ELF path over ~145 characters: emulog stops after `ELF Loading: ...`, the EE never reaches `is executing`, `bin/log.txt` is never written.
**Fix:** keep the fixture project's path short — put e2e fixtures in `~/tyra-projects/<name>` (or the Windows equivalent). If a boot produces only `TLB Miss` spam, measure the path before debugging the game.

## Docker on Linux says permission denied

**Cause:** the container runs as root, so a `docker` group granted in the current login session is not yet active in an already-running shell.
**Fix:** start a fresh session or accept that `docker` needs privilege there; the build itself needs no root once the socket is reachable.

## An EE exception cannot be raised in PCSX2

**Cause:** PCSX2's main RAM starts at address 0, so a NULL dereference, a signed-overflow `add`, an illegal opcode and a misaligned load all pass through — catching an EE exception is a **hardware-only** test. (Installing the crash handler no longer wedges anything; that was `ee_dbg_install(2)`.)
**Fix:** trigger it from a user-owned script: `volatile int a = 0x7FFFFFFF, b = 1; asm volatile("add %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));` → cause 12, a real `bin/crash.txt`, and `--symbolize` naming the exact source line. Emulator-testable parts: the report format (synthetic `bin/crash.txt`), the `TYRAX` soft-error block, and the heartbeat post-mortem.
