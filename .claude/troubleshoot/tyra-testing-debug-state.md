# troubleshoot/tyra-testing-debug-state.md

Finding the project being debugged, and the console/devkit channel. Symptom first.

## What is the OWNER debugging right now?

**Cause:** their projects live wherever they put them; a blind `Get-ChildItem -Recurse` over the disk finds nothing useful.
**Fix:** ask the machine:

```powershell
build\tyrax-editor.exe --debug-state
```

It prints, per project, the live devkit artifacts in `bin/` with **how old** each is, and decodes the interesting ones inline — `vucap.bin` as *"frame 661, flush 2/9, 16 mesh(es), 94 tris in, 512x448"*, `livedbg.bin` as *"frame 1111, scene 0, HALTED"*. The last line names the freshest artifact on the whole machine. Then go in with `--dump-vucap <thatDir>` (or read `bin/log.txt`).

Where the list comes from: **Running editors publish themselves** (`devsession.hpp`): one small file per process under `%LOCALAPPDATA%\tyra-editor\sessions\<pid>.ini` (`$XDG_STATE_HOME/tyra-editor/sessions/` off Windows), carrying the open project, build profile, live/halted state and frame, and **which transport** — `pcsx2` or `ps2link`. Liveness is the **heartbeat** (refreshed every ~4 s); a session that stopped beating shows as `stale`. Also: **`editor.ini`'s recent-project list** (`%LOCALAPPDATA%\tyra-editor\editor.ini`, machine-global), rewritten the moment a project is **opened**; **the default projects folder** (`~/TyraProjects` unless Preferences moved it); and `--debug-state <projectDir>` reports one project directly.

## A running game beats all of it

**Cause:** it is a process query, not a file one.
**Fix:**

```powershell
Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'" | Select-Object CommandLine
```

The `-elf` path is `<projectDir>\bin\<name>.elf`. Same trick for `tyrax-editor.exe` (its `MainWindowTitle` reads `TyraX - <project>`). **Never kill either process by name** — the owner may be sitting in front of one (a link failing with *"cannot open output file tyrax-editor.exe: Permission denied"* means exactly that).

## On a real PS2 the channel dies with the editor

**Cause:** a ps2link deploy is served by a `ps2client.exe` that the RUNNER spawns (`src/runner.cpp`), so closing the editor takes the file server with it: the console keeps running but every devkit file freezes at its last write, and commands written to `livedbg.cmd` are never seen. Symptom: `livedbg.bin` stops advancing while the console still answers `ping`.
**Fix:** a redeploy (*Run on PS2*, F6), not a retry — and that is why `--debug-state` reports the transport. A missing first `[ps2]` log line does **not** prove `execee` failed (the UDP command may have reached the console while its tty reply was lost); the Runner waits 15 seconds, reports an unconfirmed launch, and keeps `ps2client` alive. Never "clean up" that timeout by killing the process — doing so removes `host:` from a possibly running game (checkerboard text, missing models, silent audio).

## A deploy killed somebody else's file server

**Cause:** `deployToPs2`, `stopPs2` and `clean` used to run `platform::killByName({"ps2client"})` (`taskkill /F /IM ps2client.exe`, machine-wide by name). The `[ps2]` log is the trap — it is UDP straight from the console to whichever `ps2client` is listening, so it keeps arriving after the file channel is dead. Two transports, one dead, and only the live one is visible.
**Fix:** since 1.22.0 the Runner reaps only the servers it owns (its own by handle, a stale one of the SAME project by its command line, an orphan no running editor claims) and **refuses**, naming the project that holds the channel, for anything else (docs/ps2link-setup.md, "One file server at a time"). `--debug-state` lists every `ps2client` with the console (`-h`) and the game (`execee host:<name>.elf`) its command line names.

## `platform::killByName` is gone from the tree

**Cause:** ownership must be decided from the command line.
**Fix:** use `platform::processesNamed(name)` + `platform::killProcess(pid)`; a process whose command line cannot be read is never a target. The discriminator is the command line, not the working directory (Linux answers cwd with one readlink of `/proc/<pid>/cwd`; Windows has no supported way to ask).

## A scripted hardware probe needs the file server alive

**Cause:** a probe needs the file server alive, but the editor that hosts it also drives `livedbg.cmd`, and two writers on that file is a known hazard.
**Fix:** host the server yourself. **A game that is still running does NOT need a redeploy** — it is blocked on `host:` and resumes the moment a server answers:

```bash
cd <projectDir>/bin && ../../tools/ps2client/bin/ps2client.exe -h <ps2-ip> listen
```

`listen` serves the console's file traffic and nothing else. Only when the game is actually gone do you need the runner's two commands — `ps2client -h <ip> -t 10 reset`, then `ps2client -h <ip> execee host:<name>.elf` with `cwd = <project>/bin`. Use the **main checkout's** copy of ps2client (a worktree path is a different binary to Windows Firewall).

## Do not send ps2client COMMANDS at a console with a game loaded

**Cause:** with a game running, any client that connects is immediately conscripted as its file server and the command never reaches ps2link (`reset` and `dumpmem` both returned -1 while the game's `host:` opens scrolled past); the one command that got through froze the game.
**Fix:** the reliable sequence is the runner's: nothing else attached, `reset` (exit 0 only when the link is free), then `execee`. ps2link's extra commands are not a free lunch: `dumpmem` answers `EE: pkoDumpMem() write failed` (destination created, zero bytes) and `scrdump` exits 0 having written nothing — vestigial pko-era plumbing. Anything wanted from them is a **ps2link patch** (`tools/ps2link/` clones a pinned upstream, applies `tyrax.patch`, builds it in Docker).

## ps2link in PCSX2 (no console)

**Cause:** ps2link was hardware-only.
**Fix:** a SECOND, portable copy of the emulator (`-portable`, its own `inis/bios/logs`) with **DEV9 bridged** onto the LAN boots `ps2link.elf` and answers the real `ps2client` — same subnet, same ports, `reset` and `execee` included. A wedge then costs `Stop-Process` instead of a walk to the console. The full recipe and four gotchas (`[DEV9/Eth]` is the ini section, `EthDevice` is the bare adapter GUID, `host:` is served by PCSX2's HostFs relative to the `-elf` directory and NOT by ps2client, ping proves nothing either way) are in docs/ps2link-setup.md, "Testing a change without a console". The limit that matters: **the emulator cannot produce an EE exception** — main RAM starts at address 0, so a NULL dereference is an ordinary load. Faults, `crash.txt` and the crash handler stay hardware tests.

## Testing ps2link itself

**Cause:** the `host:` protocol code is testable before reaching for hardware.
**Fix:** `tools/ps2link/test/run.ps1` (`run.sh`) compiles the REAL patched `build/iop/net_fio.c` against stub IOP/lwip headers in `test/shim/` and drives it with a scripted fake socket (framing, EOF, short reads, buffer clamps). What makes it evidence is `-Pristine`/`--pristine`: the same tests against the untouched upstream file, which must FAIL (it does, 7 of 18, three by spinning past a 2000-call `recv()` ceiling). The harness `#include`s the `.c` so it can place the static `pko_fileio_sock`. Threads, the SIF and the GS are still console-only. Rejected theory (do not retry): the EE passing newlib `O_*` where the wire wants `FIO_O_*` (`O_RDONLY` = 0, `FIO_O_RDONLY` = 1) — asset `fopen("rb")` works through the same path.

## "Stop on PS2" got faster / SPU2 drones

**Cause:** r3 adds `spu2Silence()`; r2 fixed a closed `ps2client` socket spinning the IOP `host:` thread, an unexpected reply desyncing the stream, and an inverted `SifIopSync()` in `pkoReset()`.
**Fix:** since r3, "Stop on PS2" no longer deploys `tools/silencer/silencer.elf` and no longer sleeps ~7 s. The tool stays as a manual fallback for a pre-r3 console. Check the boot banner reads `TyraX ps2link r3` and the log has `SPU2 silenced` before blaming anything else.
