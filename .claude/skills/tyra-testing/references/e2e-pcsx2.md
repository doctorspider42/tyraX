# Layer 3 — native/Docker game build, PCSX2 boot, reading results

Read when: you build and boot a game, a boot fails or is black, you need the game's own screenshot (`--capture-frame`), judge rendering correctness, test the toolchain image, or capture PCSX2 savestates / GS dumps.

## Prerequisites and host prep

- WSL on Windows or the packages named by `tools/toolchain/setup.sh` on Linux; PCSX2 with a BIOS — auto-detected in `Program Files\PCSX2`, or on Linux from PATH / flatpak / an AppImage under `~/Applications` or `~/Downloads`; otherwise *Edit > Preferences*.
- `bash tools/toolchain/prepare-host.sh` checks; `--install` installs missing Debian/Ubuntu packages. On Linux `./setup.sh --deps` already covers curl/tar/rsync. Windows: `prepare-host.ps1 -Install` runs the same inside the default WSL distro. A normal build never runs `apt`; the Windows installer offers this as an unchecked task.
- Manual `tools/toolchain/setup.sh` installs into the editor's own dir (`configDir()/toolchain/ps2dev`), so it is a pre-install, not a second 943 MB copy.
- Docker on Linux: a `docker` group granted in the current login isn't active in already-running shells; start a fresh session.

## What `TYRAX --build <dir> --run` does (`src/runner.cpp`)

Generated files refresh → verified PS2DEV + vendored VU tools provisioned if needed → engine sources checksum-synced into the native cache, `libtyra` rebuilt if changed (VU1 microprograms only when a VU source changed) → project rsynced → `make -j` → WAV sfx converted with `adpenc` → `bin/` synced back → PCSX2 instances whose `-elf` names THIS project's ELF closed (since 1.22.0; not others) → `HostFs = true` forced in PCSX2.ini → PCSX2 launched.

- Before generation: procedural bake, vehicle bake, and (when enabled) GI/pre-lit auto-bake (cli-and-codegen.md). texbake then `wavconvert::bakeSounds` run before either build backend.
- First-ever build downloads PS2DEV v2.0.0, tests OpenVCL, compiles the engine (minutes).
- Every `*-loop.wav`: byte 6 of the generated `.adpcm` must be `1` (`od -An -tu1 -j6 -N1`); a newer output with `0` must be re-encoded with `adpenc -L`, not skipped (native/Docker parity).
- Relative project paths work now (`Project::elfPath` and the Runner's native-build args are absolutised). Keep ONE regression run relative (`--build ./examples/foo --run`): native-build `cd`s into the project, so forwarding `./examples/foo` verbatim becomes `examples/foo/examples/foo`; before the fix PCSX2 also derived `host:` from a relative ELF → untextured scene, no baked shadows, no log.
- Editor Play: select a non-startup scene, launch from toolbar/F5; Runner log shows `Play scene:`, game loads that scene, `bin/launch.scene` consumed. Build-only and CLI launches use the saved startup scene; the marker must never enter an exported ISO.
- ISO/cdrom0 path: *Project > Export PS2 ISO*, mount on host, boot in PCSX2 (covers path conversion host: boots skip).

## Incremental build: measure what it RECOMPILED

- `grep -c 'elf-g++ .* -c -o'` over the log. `examples/showcase` (18 TUs, 6 cores): no change **0 / ~5 s**; one game source **1 / ~9 s**; moved scene object **13 / ~47 s**; engine `.cpp` **~7 s**; engine `.vclpp` **~2 min**; `--rebuild` **~5 min** from nothing. A no-change build that compiles = regression (some step wrote a file the compiler reads; compare mtimes in the container with `/src/obj/*.d`; `runner.cpp` row in tyra-editor-dev).
- VU assembler stamp (since 2026-08-05): `/tyra/.vcl-stamp` hashes resolved `vcl`, `vclpp` AND the `openvcl` binary; a change prints `VU assembler changed - rebuilding the microprograms`. Before, an image swap rebuilt nothing (three `VCL_FLAGS` probes booted the previous probe's microcode). Verify from the log: `grep -cE '(^| )vcl '` (one per microprogram) and `grep -c 'elf-g++ .* -c -o'` before believing a picture. openvcl rebuilds microcode in seconds (Sony `vcl` ~2 min), so a fast build isn't proof nothing rebuilt.
- Engine-header features: the ELF only changes if `libtyra` was re-archived — look for `Engine sources changed - rebuilding libtyra...` + `elf-ar rcs bin/libtyra.a`, and `sha256sum` both ELFs (must differ).
- `--rebuild` recreates container + rebuilds engine and game from source (control for suspected incremental bugs). *Clean* also wipes host `bin/`.
- `Copy-Item` / `cp -p` / `git checkout <file>` keep the SOURCE timestamp → make may relink a previous object. Touch the file (`(Get-Item $f).LastWriteTime = Get-Date`) or clean the objects.

## Toolchain image (docs/toolchain-image.md)

`docker\build.ps1` / `docker/build.sh` (`-FromSource`/`--from-source` → `docker/Dockerfile.fromsource`, the one CI publishes; default `docker/Dockerfile` is the A/B reference CI does NOT build). Both run CI's checks. Point a scratch project at it: `TYRAX_IMAGE=tyrax-toolchain:local` in its `.env`; boot in PCSX2. `docker compose config` shows the image used; `docker ps -a --filter name=<project>` shows what the container came from. No editor rebuild needed.

## PCSX2 configuration traps

- PCSX2.ini: portable-first (`inis/` beside exe), then Windows Documents known folder (**may be OneDrive-redirected**, e.g. `...\OneDrive - <org>\Dokumenty\PCSX2\`), Linux `$XDG_CONFIG_HOME/PCSX2/inis` or flatpak `~/.var/app/net.pcsx2.PCSX2/config/PCSX2/inis`. `logs/emulog.txt`, `snaps/` beside it.
- Missing `HostFs` → "Failed to load ...png" assert on first fopen. PCSX2 rewrites its ini on exit — check HostFs first. Launcher also sets USB1 = HID keyboard, USB2 = HID mouse when *Keyboard & mouse controls* is on (`bin/log.txt`: `KbdMouse: keyboard driver ready` / `mouse driver ready`, docs/keyboard-mouse.md).
- Driving PCSX2 by hand inherits none of this: run through the editor once first; set `Renderer = 13` (software) with PCSX2 **closed**.
- Windows CLI paths: normalise with `[IO.Path]::GetFullPath()` (2.9.84: `C:/...` to `-elf` reported an existing ELF missing; `C:\...` booted); `-logfile` too. Isolated `-datapath <parent>` put settings in `<parent>/PCSX2/inis`, not `<parent>/inis` — inspect before diagnosing a first-run wizard.
- **ELF path > ~145 chars**: emulog stops after `ELF Loading: ...`, EE never `is executing`, no `bin/log.txt`, black window. `TLB Miss` spam only → measure the path first.
- **Relative `-elf`**: PCSX2 may rebase it (`examples/foo/bin/examples/foo/bin/foo.elf`): `Denying access` / `Failed to read ELF`, entry `0xFFFFFFFF`. Pass absolute paths by hand.
- Parallel sessions: when another agent's session is live, build with plain `--build` and launch `pcsx2-qt -elf <abs>/bin/<name>.elf -logfile <yours>` yourself (build container per project name, engine volume per checkout are shared ground). Select windows/processes by the `-elf` on the command line (`Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'"`; Linux `/proc/<pid>/cmdline | tr '\0' ' '`). `pkill -f <pattern>` matches the shell running it. A second PCSX2 on the same ELF halves both frame rates.

## Reading the results

- **emulog.txt**: success = `ELF <path> is executing`; failure = `Assertion` lines / early exit. **`TYRA_LOG`/EE printf does NOT land in emulog** even with EnableEEConsole.
- **`bin/log.txt`** (host: fs) = TYRA_LOG + assertion dump bracketed `======= TYRA =======` … `================`. A failed assert halts quietly leaving the last frame up — grep the banner, don't screenshot for text (the running editor also pops the dump in a dialog).
- **ps2link deploy: no `bin/log.txt`.** The game logs to EE console → ps2client → `[ps2] …` lines (Output panel; stdout of `--build <dir> --run-ps2 <ip>`; Debug window). Games built before 2026-08-06 log NOTHING over ps2link (unflushed stdout) — rebuild before believing silence.
- **`TYRAX --capture-frame <dir> [-o shot.png] [--alpha a.png] [--timeout s]`** — the game photographs itself (docs/devkit.md "The game's own screenshot"): debug build + *Live Debugger* on; `livedbg.cmd` flags **bit 6** or *Debugger > Screen > Capture frame*; reads the last finished frame from GS VRAM → `bin/frame.tga`; GUI keeps `screenshots/frame-<date>-<time>.png`. No window/focus; the ONLY capture on real hardware; never grabs the wrong window. Console A/B: `--build <dir> --run-ps2 <ip>` (that process IS the host: server), `--pad` to move, `--capture-frame` per vantage.
  - Works on console since 1.55.1 (ps2sdk's `ps2_screenshot_file()` `open(O_CREAT|O_WRONLY)` arrived at host: as a mkdir → a DIRECTORY `frame.tga`; the runtime writes the file itself now). Over ps2link one capture freezes the game ~3 s; in PCSX2 it lands between frames.
  - TGA: uncompressed 32-bit **BGRA, bottom row first**, exactly `18 + w*h*4` bytes (PIL opens it). Written a scan line at a time → decide "finished" by the size reaching that and no longer GROWING (seconds over ps2link). Alpha forced 255. Script: `Command c; c.seq = n; c.captureFrame = true; livedbg::writeCommand(bin + "livedbg.cmd", c);`.
  - It is the **GS raster** (no aspect correction/letterbox) — cheapest way to read real buffer geometry without touching `ScreenshotSize` in PCSX2.ini. Cross-check vs a PrintWindow grab cropped to non-black columns and resized: **0.91/255** mean abs diff on fpp = correct.
  - Command sequence is wall-clock seconds: space repeated calls > 1 s apart or the second is seen as the same command. `--capture-frame` resumes a halted game. Delete `bin/frame.tga` (and `bin/livedbg.cmd`) before boots and between captures or you read the previous file. Host: writes can tear (`wrote N of M bytes - the host: write did not complete`) — retry.
- `--symbolize <dir> 0x...`: container's `mips64r5900el-ps2-elf-addr2line` on `bin/<name>.elf.sym` (DEBUG builds keep it: `vendor/tyra/Makefile.base` writes it when the generated Makefile sets `KEEPSYM=1`; release ELF is `strip --strip-all`ed). Needs the build container up.

## Rendering correctness

- Judge visuals in the **software renderer** (HW masks GS raster-window wrap bugs). Let the game settle, screenshot, compare to known-good.
- **Frozen-camera fixture** (every rendering A/B): Player `walkSpeed: 0`, `lookSpeed: 0`, `position`, `rotation: [pitchDownDeg, headingDeg, 0]` (X = pitch, positive = down; docs/player-start.md), `"keyboardMouse": false`; `displayMode: progressive` (interlaced alternates fields); `--build --run`, wait ~12 s, capture. Or a global script (`TYRA_SCRIPT`, no attachment) driving `ctx.cameraOverride`/`cameraEye`/`cameraAt` from its frame index (e.g. `examples/large-terrain/authoring/engine-generality-2026-09-16/zz_vantage.cpp`).
- A strict pixel A/B between two RUNS usually fails (orbit/auto-spin phase differs, interlaced fields) — a "1.7M pixels differ" result is almost always that. Freeze the pose, or **compare DATA on the console**: log an FNV hash (+ first raw bit patterns) of what the change produces and diff the logs; for a format change load BOTH formats in one run (PROGRESS 163).
- **Axis-aligned walk over the flat checkerboard is invisible to a diff** (pattern maps onto itself): 2.5 s forward from default FPP changed an 11-px band. Turn first (right stick), then walk: ~150k px.
- **Prove which object is which** in a one-frame A/B: screen X runs opposite to world X (yaw-0 Player looks along +Z), so "left" = positive X. Force one object's colour absurd for one run.
- **Do the pixel arithmetic for subtle effects** (retired PROGRESS 231): Camera Shake amp 0.05 → 0 px over four captures 250 ms apart (ease-out → 0.03; 3 cm sub-pixel at 512x448, nearest object 5.6 m); amp 2.0 → horizon swept 215 px, 545k px. Turn the effect up past visibility first. A byte-identical idle fixture is what makes small shifts meaningful.
- **Measure captures** (PROGRESS 132): read pixel rows (plateau widths, step sizes told flat from Gouraud); byte-identical A/B is stronger than "looks the same".
- **Temporal effects need a SETTLE test, not a frame diff.** A heavy trail over a repeating pattern averages flat; a long trail looks like a freeze while moving. Drive, `neutral`, wait seconds, capture twice: settled + SHARP = trail; smeared (or still showing the boot splash) = freeze. Inspect static high-contrast HUD text after idle clear: a diagonal down-right tail = 1:1 history sprite sampling texel boundaries instead of centres (even with point filtering). Motion blur's idle clear = ONE sharp frame ~0.12 s below its meaningful-motion rate threshold, not a permanent zero — with the camera parked, move an object across: it must still trail. Test option-off separately (its 16-bit fixed-point residue is expected). Test at forced low FPS or hardware: a per-frame displacement threshold passed at 60 FPS in PCSX2, failed at ~30 FPS on PS2 (pad drift doubled per frame).
- Still-picture (period-2) testing and moving-picture testing: motion-and-ab-rigs.md.

## PCSX2 savestates and GS dumps (docs/emulator-captures.md)

`scripts/pcsx2-capture.py run <elf> --stage --states 3 --gsdump [--slot N] [--interval s] [--boot-wait s] [--wait-log] [--out dir] [--keep]` starts a **private** PCSX2 (`-datapath` under `%TEMP%\tyra-editor-test\pcsx2-capture-<slot>`, own ini, PINE on), takes savestates over PINE + one single-frame GS dump, writes `report.txt`, stops only its own PID. `gs <dump>` / `state <p2s>` analyse hand-taken captures of any title. Needs `pip install zstandard numpy`.
- GS dump = PCSX2 hotkey rebound to F12 in the private ini, `PostMessage`d to that PID's window (no focus, no global input). Windows only; Linux skips the dump.
- One `--slot` per parallel run; the user's global PCSX2.ini is never touched.
- A savestate lands at VSync → VIF1 idle → shows at most the last ping-pong packet. Use `arm-vucap.py` for our chains; the savestate analyser is for games that build one frame chain.
- GS pixel counts are scissored coverage, not fill — compare with them, don't price the GS.

## Crashes

- **PCSX2 cannot raise an EE exception** (main RAM starts at 0; signed-overflow `add`, illegal opcode, write to 0, misaligned load all pass) — catching one is HARDWARE-ONLY (entry 246). Console trigger in a user-owned script: `volatile int a = 0x7FFFFFFF, b = 1; asm volatile("add %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));` → cause 12, real `bin/crash.txt`, `--symbolize` names the line. (Installing the handler no longer wedges anything — that was `ee_dbg_install(2)`, fixed in 246.)
- Emulator-testable: the report format (write a synthetic `bin/crash.txt`, let the editor parse + symbolize), the TYRAX error block (a `.flownode` calling `TYRA_SOFT_ERROR`), the heartbeat post-mortem (kill the game, watch the Debugger notice).
- Zero-length `BagArray` / null `data()`: PCSX2 hides writes through null (RAM at 0); hardware reports cause 3 `BadAddr 0` right after loading.
