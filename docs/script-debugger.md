# Script debugger (GDB / VS Code in PCSX2)

The script debugger stops the running game in PCSX2 on a line of one of your C++
scripts, so you can step through it, read its locals and fields, change them and
carry on, from VS Code (F5) or from any GDB. It is a debug-build feature, it works
only in PCSX2, and it needs no modified emulator: a stock PCSX2 2.x with its PINE
socket on is enough.

## Using it

### F5 in VS Code

Open the project folder in VS Code, set a breakpoint in a script under
`src/scripts/**` or `src/extensions/**`, and press **F5**. That is the whole
workflow. Every project's `.vscode/launch.json` already holds the configuration:

```json
{ "type": "tyrax", "request": "launch", "name": "TyraX: debug game in PCSX2" }
```

F5 saves your open files first. What it does next depends on what is already
running:

| Situation | F5 |
|---|---|
| The TyraX editor serves a game built from the current code | attaches at once |
| The game is running, but you changed code since it was built | rebuilds and restarts the game, then attaches (the editor's build, or a fresh headless one); *Output > TyraX* names the file that changed |
| The editor has the project open, but no game runs or the debugger is off | asks the editor, which switches the debugger on, builds and runs; VS Code shows the editor's progress and attaches when the game is up |
| No editor is open | runs the editor headless (`tyrax-editor --debug-run --enable`): build, PCSX2, serve, then attaches. If the debugger is off it asks first, and the switch is saved in the project |
| The project builds with the release profile | says so: switch it to debug in the editor (the profile pill left of PCSX2 \| PS2) |

The **run button in the editor's title bar** (top right, over a `.cpp` file)
does the same: in a TyraX project it reads *Debug Game in PCSX2*. The C/C++
extension's own one there - "build and debug the active file" with g++ or
cl.exe, which cannot run PS2 code - is hidden for the project (`C_Cpp.debugShortcut:
false` in `.vscode/settings.json`, written once by the TyraX extension and left
alone when you set it yourself).

**Changing code while debugging.** The game cannot pick up a change until it is
rebuilt, so as soon as you save a code file newer than the running build, VS Code
says so: a warning in the status bar (*TyraX: code changed - Restart to rebuild*)
and one notification with *Rebuild and Restart*. Breakpoints in edited lines may
not match until then. *Rebuild and Restart* - and the green **Restart** arrow
(Ctrl+Shift+F5) - rebuild and start the game again when the code changed and
attach to it, whether the game was running or parked on a breakpoint; with no
change they simply reattach.

Once stopped, Continue (F5), step over (F10), step into (F11), step out
(Shift+F11) and Pause work as usual, and Variables shows the locals and `this`
with the script's fields. You can edit a value there or with
`-exec set var x = ...` in the Debug Console. Build output goes to *Output > TyraX*.
Add `"build": false` to the configuration to run the last build instead of
building first when no editor is open.

The extension is **TyraX** (`tyrax.tyrax`) on the Visual Studio Marketplace and
Open VSX. It needs Microsoft's **C/C++** extension (`ms-vscode.cpptools`), which
drives GDB, and offers to install it if it is missing. See
[the VS Code extension](vscode-extension.md).

### From the editor

**Build > Debug Scripts in VS Code** does the same from the editor's side: it
switches the debugger on, builds, runs and opens the project in VS Code. Press
F5 there once the game is up.

The toolbar's **GDB** chip shows where the session stands:

| Chip | Meaning |
|---|---|
| `GDB` (dim) | The debugger is on, but no game is running, or the running one has not drawn its first frame yet. |
| `GDB` (accent) | Ready: the game runs and the editor serves GDB. Press F5 in VS Code. |
| `GDB attached` (green) | VS Code / GDB is attached and the game runs. |
| `GDB stopped @ <pc>` (orange) | The game is parked on a breakpoint, a step or a Pause. |
| `GDB failed` (red) | It cannot serve. The tooltip says why, usually that the ELF was built without the debugger and needs a rebuild. |

Clicking the chip opens the project in VS Code.

### The switch

F5 turns it on by itself. It also sits with the other live tools in *Project >
Preferences > Build & debug > Live tools in debug builds > Script debugger*, and
in *Build > Script Debugger*. It only applies with the **debug** build profile.
While it is on:

- the game carries a small breakpoint handler (`src/gen/script_debug.gen.cpp`);
- the project's own scripts (`src/scripts/**`, `src/extensions/**`) compile at
  **-O0**, so every line has code, every local has a home and stepping follows
  the source. The engine and generated code keep `-O3`;
- PCSX2 starts with PINE enabled (the editor sets `EnablePINE = true` in its
  ini, on the slot already configured there; the default is 28011);
- the editor serves GDB while the game runs, on a port derived from the project
  path (23400-24399; see `.vscode/tyrax.json`).

Turning it on or off changes the Makefile, so the next build recompiles the game
once. A release build carries none of it: the handler is not generated and
`--audit-release` stays clean.

### From any GDB

The editor speaks GDB's remote protocol, so a plain GDB works too:

```bash
mips64r5900el-ps2-elf-gdb bin/<name>.elf.sym
(gdb) target remote 127.0.0.1:<port>
(gdb) break motion_math.cpp:31
(gdb) continue
```

The GDB to use is the toolchain's own:
`<config>/toolchain/ps2dev/ee/bin/mips64r5900el-ps2-elf-gdb`, where `<config>` is
`%LOCALAPPDATA%\tyra-editor` or `~/.config/tyra-editor`. On Windows that is a
Linux binary: run it inside WSL. Use the ELF with symbols,
`bin/<name>.elf.sym`, not the stripped `bin/<name>.elf`. In batch mode
(`gdb -batch -x script`) add `maint set target-async off` first, or `continue`
returns before the game stops.

`tyrax-editor --debug-serve <projectDir> [--pine-slot N]` serves a game you
launched yourself, for example with your own PCSX2 setup.
`tyrax-editor --debug-run <projectDir> [--no-build] [--rebuild] [--enable]` does the
whole thing headless; `--enable` switches the debugger on in the project first.

## How it works

```
VS Code ──(C/C++ extension)── GDB ──remote protocol──> editor ──PINE──> PCSX2
                                                                         │
                                         the game's BREAK handler <──RAM─┘
```

PINE, PCSX2's IPC socket, can read and write guest RAM and nothing else. It
cannot stop the CPU, read registers or set breakpoints. So the game does the
CPU-side work itself, and everything the editor does is move data through one
block of RAM, the mailbox `g_tyraxScriptDbg`:

1. **Breakpoints are `break` instructions.** When GDB inserts one, the editor
   queues "write `break` at this address" in the mailbox. It does not write the
   code itself (see the pitfalls below).
2. **A `break` lands in the game's handler.** The handler is routed through
   ps2sdk's `libeedebug` level-1 vector, which saves every register into a frame.
   It stores the FPU registers and a pointer to that frame in the mailbox, marks
   itself stopped and spins with interrupts off, so the whole EE stands still.
3. **The editor answers GDB from RAM.** Registers come from the frame, memory
   comes straight from PINE, and register writes go back into the frame.
4. **Continue** writes the queued code patches and a "go" flag. The handler
   applies the patches on the EE thread (writing each word back from the data
   cache and invalidating the instruction-cache line), restores the FPU and
   returns.
5. **Stepping** is done in software, because the EE has no single-step mode.
   GDB hands over a whole source line (*range stepping*), and the editor traps
   every way out of it: branch and jump targets outside the line, the
   instruction after its end, and each `jr`/`jalr`, which it then steps once its
   target register is known. A line of straight code or a loop costs one stop.
   A call costs one more at the callee, where GDB steps over it with a
   breakpoint at the return. A single instruction (`stepi`) is trapped at every
   place it can go next.
6. **Pause** sets a flag that the object-script driver polls once a frame. The
   poll executes `break 7`, which the handler steps over on the way out, so a
   pause stops in `tyraxScriptDebugPoll`. Step out to reach the frame loop.

On Windows the GDB that ships with the PS2 toolchain runs inside WSL, and WSL's
default networking cannot reach a socket listening on Windows' `127.0.0.1`. The
other direction works, so the editor starts a small relay in WSL
(`tools/toolchain/gdb-relay.c`, compiled on first use with WSL's `cc`). It
listens on the GDB port for GDB and on the next port for the editor, which
connects into it. The VS Code extension runs GDB through `wsl.exe` and maps the
`/mnt/<drive>/...` paths in the debug info to `C:\...`.

### What a step costs

| Measured (PCSX2 2.9, Windows, cpptools over DAP) | F10 (median) | 8 x F10 |
|---|---|---|
| First version: one trap per instruction, one PINE request per word | 1.8 s | 18.1 s |
| Range stepping + batched PINE + per-stop cache + no-ack | **0.1 s** | **0.9 s** |

The time that is left is mostly GDB and the C/C++ extension re-reading the frame
and variables (~30-60 ms). The game itself takes ~10 ms to reach the next trap
after it is released, because PCSX2 paces the emulated CPU. Everything a stop
reads is batched into a few PINE requests and kept until the game runs again.
A batch of 140 words costs about as much as a single word (~0.1 ms).
`TYRAX_GDB_TRACE=<file>` makes the editor log every packet and how long it took.

## Limits

- **PCSX2 only.** A console has no PINE. A ps2link deploy runs with the debugger
  built in but never gets a GDB server.
- **One debugged game at a time per PINE slot.** Every PCSX2 on the machine
  listens on the slot in its ini (28011 by default), so a second game cannot take
  it; the editor refuses to serve a game whose code is not the build it has.
- **One PINE client at a time.** PCSX2 serves the next PINE connection only after
  the previous one closes. If another tool is attached to the same slot, the chip
  stays at "waiting for the game".
- **Engine code is optimised.** Stepping into the engine or generated code
  works, but at `-O3` its lines jump around and its locals often read
  `<optimized out>`. Your scripts are `-O0`.
- **Pause needs scripts to tick.** A game sitting in a pause menu does not run
  the object-script driver, so Pause waits until gameplay runs a frame.
  Breakpoints in your own code are not affected.
- **No hardware watchpoints.** Data breakpoints (watch) are software-only in GDB
  and very slow; prefer breakpoints and conditions.
- **Stopping freezes the emulated machine.** Sound stops and the IOP waits. The
  Live Debugger's panel reads "stale snapshot" while you look around.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Chip says "no PINE on slot N" | PCSX2 is not running, or PINE is off. A launch from the editor turns it on. A PCSX2 you started yourself needs *Settings > Advanced > Enable PINE*, on the slot `--debug-serve --pine-slot` names. |
| Chip says "PCSX2 itself is paused" | The emulator paused (Esc, focus loss, or a gamepad disconnecting - PCSX2 pauses on that by default). Resume it. |
| VS Code: "the build failed" | The rebuild F5 asked for did not compile; the editor's Output panel (or *Output > TyraX* without an editor) has the first error. |
| Every F5 rebuilds | *Output > TyraX* names the file that is newer than the build each time; a tool that keeps touching it makes every F5 a rebuild. |
| "the game on PINE slot N is not this build" | Another PCSX2 owns the PINE slot (one slot serves every PCSX2 on the machine), or an old build of this project is still running. The editor compares 1024 words of the game's code with the ELF before it serves it; close the other game, or Build & Run. |
| Restart does not reattach | VS Code still runs an older TyraX extension: *Developer: Reload Window* after updating it. |
| Chip says "PINE slot N does not answer" | Another tool holds PCSX2's PINE connection; it serves one client at a time. Close it. |
| Chip says "waiting for the game's first frame" | The game has not run the object-script driver yet (loading), or it was built without the debugger. Rebuild. |
| `GDB failed: no script debugger in ...elf.sym` | The ELF predates switching the debugger on. Build & Run again. |
| Breakpoint lands on a strange line | The file is not one of your scripts (it is `-O3`), or the build predates the switch. Rebuild with the debugger on. |
| VS Code: "Unable to start debugging ... Parameter 'arch'" | An old extension. Update TyraX in VS Code (0.5.0+). |
| VS Code: "the script debugger is off for this project" | `.vscode/tyrax.json` is missing: switch the debugger on in the editor and build once. |
| Windows: "the WSL relay exited" | WSL has no C compiler. Run `tools/toolchain/prepare-host.ps1 -Install`, which the toolchain needs anyway. |

## Files

| File | What |
|---|---|
| `src/gen/script_debug.gen.cpp` | the game's handler + mailbox (generated, only while on) |
| `.vscode/tyrax.json` | what the VS Code extension needs: whether the debugger is on, the profile, editor, ELF, GDB, port, path map (generated in every project, machine-specific) |
| `.vscode/launch.json` | the `tyrax` configuration (written once in every project, then yours) |
| `bin/gdbserver.json` | written by the editor while the project is open, and by `--debug-run`: `state` (`off`, `release`, `building`, `idle`, `waiting for the game`, `waiting for GDB`, `attached, ...`, `failed`), `message`, `port`, `pid`, `owner` |
| `bin/gdbserver.request` | VS Code's F5 asking the open editor for a debug run: `{"id": ..., "rebuild": true\|false}`. The editor deletes it, acts once no build is running, and echoes the id in `gdbserver.json` (`request`) so VS Code never attaches to the game it asked to replace; a failed build comes back as `failed` |
| `src/scriptdbg.hpp` (editor) | the mailbox layout, the protocol both sides compile against |
| `src/gdbstub.cpp` (editor) | PINE client, GDB remote protocol, software single-step |
| `tools/toolchain/gdb-relay.*` | the WSL relay (Windows) |
