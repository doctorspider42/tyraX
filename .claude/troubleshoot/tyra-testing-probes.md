# troubleshoot/tyra-testing-probes.md

Scripted probes that replace the editor — flow-graph, VU1, crash, release audit, log panels, audio, multiplayer. Symptom first.

## The `livedbg.bin` flush map (snapshot v4)

**Cause:** "my model shows 2 meshes" was traced to a bag flush full of terrain, one flush at a time.
**Fix:** since PROGRESS 205 the **flush map** makes that one read: `livedbg.bin` (snapshot **v4**) ends with a 64-byte stats block (FPS, flushes/quadwords/vertices to VU1, GS VRAM free/low-water/largest/resident, object counts, free EE RAM) and one 8-byte row per bag flush (qw, unpacks, verts, program). A ~60-line Python probe reads it. **v3 snapshots still parse** (a console running an older build keeps working, minus stats), so when stats are missing the first question is "was the GAME rebuilt?". Free EE RAM is measured only when asked (command flags **bit 5**).

## Flow-graph logic, without a pad or a screenshot

**Cause:** a debug build with the *Live Debugger* preference on writes `bin/livedbg.bin` every 6 frames (per-node hit counters, a ring of recent fires, the flow variables and save values, the halted flag) and reads `bin/livedbg.cmd` (breakpoint list, halt/step, force-fire).
**Fix:** both are small fixed-layout binaries (`src/livedbg.hpp`), so a ~40-line Python probe replaces the whole editor: read the snapshot to assert which nodes ran how often and what the variables hold, and write a command to prove Pause freezes the counters, Step advances exactly one frame, or a force-fire runs a trigger's branch. The key -> object/node map is `src/gen/livedbg.sym`. The editor and the probe must not both drive `livedbg.cmd` at once.

## Prefer a C++ probe and link the editor's own decoder

**Cause:** a hand-rolled Python reader can disagree with the format; the shipped decoder cannot.
**Fix:** `src/livedbg.cpp` has no GL/ImGui/project.hpp dependency, so a ~60-line `main()` compiled with just that one file (`g++ -std=c++20 -static probe.cpp <repo>/src/livedbg.cpp -I<repo>/src` — `-static`, or the exe dies with `0xC0000139` looking for MinGW's DLLs) calls `loadSymbols` + `readSnapshot` and prints per-node hit counts with node ids and types, every watch variable BY NAME, and the armed timers. `hashMatch` in that dump is the check that the ELF's symbol table is the one the editor thinks it is. Two things it makes clear: a trigger whose exec output is **unwired** has a hit count of 0 (codegen does not instrument an empty chain); an event's payload trailing the live counter is the **one-frame bus latency** as a number.

## Inspect what went to VU1

**Cause:** you need the decoded chain and vertices without the GUI.
**Fix:** arm a capture (Debugger > VU, or command flag bit 3 in `livedbg.cmd`) and the game writes `bin/vucap.bin`; `tyrax-editor --dump-vucap <projectDir>` prints the decoded chain (DMA tags, VIF codes, UNPACK destinations, the MSCAL entry point) and the first vertices. A healthy capture shows `UNPACK V4_32 -> VU1 addr 0` for the scales, referenced `num=21` blocks for the double-buffered vertex data, and model-space positions with w=1.0. It lists the meshes in the flush, which **bag flush** of the frame, the live render resolution, and the amber findings. A project with no runnable flow-graph node generates no devkit layer at all (`live_debug.gen.cpp` being a 220-byte comment is the tell) — a scratch fixture needs `--apply-graph`. Consecutive captures of the same draw are byte-identical in LENGTH; tell them apart by mtime or the frame in the header. Flags bit 3 arms a capture, bit 4 means "index in bits 8-23"; without bit 4 each capture walks to the next flush. **Delete `livedbg.cmd` before booting a fixture** — a leftover command eats the first capture. `refe qwc=32789` or float garbage means the walker lost sync.

## A hand-rolled arming script arms NOTHING, silently

**Cause:** a stale `CMD_VERSION`: the game drops a command whose version it does not know (`if (magic != CMD_MAGIC || version != CMD_VERSION) return;`), so the script prints "armed" and `--dump-vucap` answers "no capture yet" — on the console AND in the emulator, which makes it look like the transport.
**Fix:** check the constant in `src/livedbg.cpp` before suspecting anything else. One arming that DOES land sets `vuCapPending` and the tap then refuses every later one until the write finishes.

## Symbolize an address from a running/crashed game

**Cause:** you have an address and no name.
**Fix:** `tyrax-editor --symbolize <projectDir> 0x...` runs the container's `mips64r5900el-ps2-elf-addr2line` against `bin/<name>.elf.sym`, the unstripped copy a DEBUG build keeps (`Makefile.base` writes it when the generated Makefile sets `KEEPSYM=1`; the shipped ELF is `strip --strip-all`ed). Needs the build container up.

## Testing a crash is harder than it looks

**Cause:** **PCSX2 cannot produce an EE exception at all**, so catching one is a HARDWARE-ONLY test.
**Fix:** on a real console the reliable trigger is one line in a user-owned script — `volatile int a = 0x7FFFFFFF, b = 1; asm volatile("add %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));` — which raises cause 12 and lands a real `bin/crash.txt`; `--symbolize` then names the exact source line. What IS testable in the emulator: the report format (write a synthetic `bin/crash.txt` and let the editor parse + symbolize it), the TYRAX error block (a `.flownode` calling `TYRA_SOFT_ERROR`), and the heartbeat post-mortem (kill the game and watch the Debugger notice).

## Prove a release build is devkit-free

**Cause:** a broken check reads exactly like a clean build.
**Fix:** `tyrax-editor --audit-release <projectDir>` reads the built ELF and exits 0 (clean) / 1 (something leaked), printing text/data/bss. Every release build also runs it and logs the verdict. **Negative-test it too** — run it against a DEBUG ELF and confirm it fails and names the findings. It also flags a MEASUREMENT build (1.123.2): it scans `.rodata` for `FTCLIP`, `FTPKT`, `STAPIPRET`, `STAPIPMISS`, `STAPIPBAKE`, `VRAMRES`, `VRAMEVICT`, `ROADINDEXVERIFY`, `WHEELBAKE` and prints `measurement build - .rodata`. Negative-test that half with `TYRA_WHEEL_REBUILD_REPORT=1` (generated game) or `TYRA_FRAME_PROFILE=1` (engine). **Add the tag when you add a gate.**

## Live Logic patches without the GUI

**Cause:** `livelogic.cpp` has no GUI dependency.
**Fix:** a ~100-line host harness (link it against the editor's `build/CMakeFiles/tyrax-editor.dir/src/*.obj` minus `app`/`viewport`/`gl_loader`/`main`, plus `vendor/ufbx/ufbx.c.obj` and your own `STB_IMAGE_IMPLEMENTATION` TU) can `project::load`, edit graph params in memory, `livelogic::compile` + `encode` and write `bin/livelogic.bin`. Combined with the Live Debugger's telemetry that is a fully scripted end-to-end check of hot-patched logic. **When a patch half-works**, suspect the wire STRIDE first, and check that `build/tyrax-editor.exe` was rebuilt before the `--build` that generated the game's parser.

## The log panels' severity split (docs/log-panels.md)

**Cause:** `logview.cpp` is a pure function of text — no ImGui, no `Project`.
**Fix:** check it from a 40-line harness (`g++ -std=c++20 -Isrc harness.cpp src/logview.cpp`) fed a realistic build log. Assert TWO things: that the counts are per ENTRY (a gcc error plus its snippet is one error, not four), and that parsing the log **one line at a time** through `parse(log, from, state, out)` + `appendPartial` gives byte-identical results to one-shot parsing. The panel is then a `--ui-script` job (`click 'Output/3 errors'`, `expect-unchecked 'Output/Select text'`); the **Debug** window is a dock TAB behind Output in every built-in layout, so no script can select it — verify it by temporarily pointing that layout's `pendingFocusWindow_` at `"Debug"`, screenshotting, and reverting.

## Audio

**Cause:** EE-side logs are invisible for the GAME's audio.
**Fix:** the *editor's* own audio (`src/audiopreview.cpp`) is testable directly — a host harness opens the device and prints peak levels; the Drone Generator's DSP needs no device at all (link `dronegen.cpp` alone, render a preset and measure the samples). For the GAME's audio, meter the PCSX2 process — on Windows the WASAPI session peak meter (`AudioMeterInformation`), on Linux `pactl list sink-inputs` (or a short `parec` capture of the monitor source). A by-ear speaker check stays with the human.

## Two-player modes (docs/multiplayer.md)

**Cause:** the split/shared toggle needs a controller.
**Fix:** testable with no controller through the Remote Pad — give the scene two Player objects and a pause menu with the "Player count" option block, then `--pad <dir> "press start; wait 0.5; press cross"` and screenshot: the frame flips between full-screen and the top/bottom split (or the pulled-back shared camera). **Pad-2 hot-join is scriptable too** (`pad 2; press start`) — the overlay is applied to the game's own second connector.

## Flow-graph / gameplay logic

**Cause:** an `On Button` / `On Action` trigger needed a human at the pad.
**Fix:** it is reachable unattended now (`--pad <dir> "press cross"`), so prefer that over rewiring the behavior to `On Start` / `Every N Seconds` just to test it. What still needs a human: mouse FEEL, analog ramps judged by eye, and the editor's own on-screen pad being CLICKED — say so in the commit message.
