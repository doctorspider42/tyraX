# Devkit

![The live tools (devkit channels) in Project Preferences > Build & debug](img/project-preferences-build.png)

TyraX debug builds can inspect and control a running game. Release builds remove
the devkit completely.

Enable individual channels in **Project > Preferences > Build & debug**. Most need a
game launched with **Build & Run** (`F5`) or **Run on PS2** (`F6`).

## Choosing the work and frequency

![Devkit frequency settings](img/devkit-frequency.png)

The channel switches select what the game actually includes. Disable Live
Link when not editing the scene/textures, Live Logic when not patching graphs,
Time Machine when not rewinding, and Remote Pad when using a physical pad.
Keep Live Debugger for frame statistics, watches, breakpoints and captures.
The input recorder remains a separate opt-in channel.

The **Devkit channels** section is one table: each channel's row has its
switch, a line saying what it does, and an **Every** column with its interval
override (greyed out while the channel is off; Live Debugger has two, commands
and reports; Remote Pad and the input recorder keep their own cadence, shown as
`-`). The five fields keep these names:

| Setting | Work scheduled |
|---|---|
| Live Link every | Scene-edit and painted-texture command reads |
| Live Logic every | Graph patch reads; graph execution still runs every update |
| Debugger commands every | Breakpoints, stop/step and capture request reads |
| Debugger reports every | Snapshot writes with statistics and watched history |
| Time machine every | Restore request reads and state capture writes |

Values are game updates: **0 = existing platform defaults** (shown as *auto*), 1 = every update,
up to 120. Missing fields in older projects mean 0. Changes require rebuilding
the game. At 50 FPS, 50 updates take one second; at 16 FPS, about three seconds.
Increasing an interval trades response latency or history density for fewer
host filesystem operations. It does not disable per-node counters, per-update
watch sampling or graph execution; disable the corresponding channel to remove
that work. Longer report gaps can overwrite older entries in bounded history
rings before the editor receives them.

Automatic intervals remain 6 updates locally and 25 over ps2link for Link,
Logic and debugger I/O; Time Machine retains its platform defaults. The initial
poll phases stay staggered. Paused debugger command polling is capped at two
loop ticks and forced reports still bypass the report interval. The editor's
missing-heartbeat tolerance scales with the configured report interval and
reported FPS. Remote Pad polling and input recording/playback keep their
existing timing; these controls must not make a held button lag or lose frames.

The Live Debugger writes one initial snapshot as the runner's game-is-up marker,
then sends periodic reports only after it has received a valid editor command.
A debug game launched from the CLI therefore stays inspectable but does not pay
for an unconsumed synchronous `host:` snapshot every 25 updates. Command polling
continues, so opening the Debugger attaches normally and requests an immediate
answer. On a physical PAL PS2 this removed a repeatable 7.2 ms update spike that
turned one frame in every 25 into a 40 ms frame; ordinary work stayed unchanged.

For a lighter hardware session, try Link/Logic/Time Machine at 100, debugger
commands at 25 and reports at 50, then disable the unused channels. This is a
starting configuration, not a measured FPS guarantee. No extra polling channel
is introduced, and release builds still omit all these runtimes.

Verified for 1.90.0 on Windows: settings save/reopen and range clamping,
Preferences editing, automatic/custom/disabled/release code generation, native
game build, and PCSX2 plus physical PS2 runtime checks. With reports set to 50,
successive snapshot frame ids differed by exactly 50 on both. Physical PS2
halt/one-step/resume succeeded (step and resume about 0.15 s), and frame capture
worked. This is functional validation, not a measured performance improvement.

## What the devkit costs at runtime

**Since 2.15.0 most of what follows is history on a console**: a debug game
under ps2link moves its polled channels - Remote Pad, the Live Debugger, Live
Link, Live Logic, texture hot reload, the time machine - and its log lines
off `host:` and onto the IOP ([devlink.md](devlink.md)). Measured on a PS2
with the debugger attached: 0.17 ms of `pre` against 1.67 ms over `host:`, and
0 missed fields against 20 in 1250. What still costs round trips: the input
recorder (a chunk every 256 frames), the boot snapshot and the crash report. The numbers below are
the `host:` path, still what PCSX2-less tools and older games pay.

The devkit is free in a RELEASE build - that is what the audit proves. In a
debug build it is not, and over ps2link it is not small. Measured on a physical
PS2 on `examples/vehicle-playground`, 2026-09-23, parked vantage, day:

| | frame period | frames missing the 20 ms field |
|---|---:|---:|
| debug build, Remote Pad + Live Debugger on | 26.3 ms | 15 of 50 |
| the same build with both compiled out | **20.4 ms** | **0-1 of 50** |

Over ps2link every `host:` open is a network round trip, and the game makes
them from `loop()` before it starts rendering: Remote Pad reads
`livepad.bin` every 4th frame, the Live Debugger polls its command file and
writes its snapshot every 25th. A frame that was a millisecond inside its field
is pushed over it by any of them and waits a second field. Under PCSX2 the same
reads are host syscalls and cost almost nothing, which is why this never shows
in the emulator.

The HUD's `MEM` readout WAS the other one, until 1.123.10. It measured free
RAM by allocating every free block until `malloc` fails, every two seconds, and
that cost 30-75 ms depending on how fragmented the heap was - a visible hitch
every two seconds in any debug build that showed it. It now reads the
allocator's books instead (`mallinfo().fordblks` plus the space between
`sbrk(0)` and `EndOfHeap()`), agrees with the old probe to within 544-1806
bytes of ~19 MB, and the periodic spike is gone: 8 against 0 in paired
emulator runs with the readout on. `TYRA_MEM_VERIFY` in
`engine/inc/info/info.hpp` takes every reading both ways, as the gate.

So: **a frame rate read off a debug build over ps2link describes the devkit as
much as the game.** For a number that means something, turn Remote Pad, Live
Debugger and the MEM readout off for the run (tyra-testing, "A debug build over
ps2link is NOT the player's frame"), and turn them back on when you need the
tools. Keep Remote Pad off whenever a physical pad is in use anyway - it is the
most frequent of the polls.

## Release builds stay clean

Devkit code is generated only for the debug profile. A release build removes
Live Link, the debugger, Remote Pad, Time Machine, the input recorder and
their file polling, buffers and symbols.

Every release build runs an ELF audit. You can repeat it with:

```text
tyrax-editor --audit-release <projectDir>
```

Exit code 0 means no devkit marker, symbol, command file or buffer reached the
ELF. Run the same command on a debug ELF when checking the auditor itself: it
must fail and list what it found.

**"Clean" means no devkit — not no logging.** A game build **never defines
`NDEBUG`** (the release profile only drops `-g` and sets `KEEPSYM=0`), so every
`TYRA_LOG` / `TYRA_WARN` in the engine ships: the calls run and the format
strings sit in `.rodata`. Harmless on a retail console, which has nowhere to
send the EE console, and not an audit failure since none of it is devkit code —
but do not write "compiles out" about a `TYRA_*` macro without checking.

**It also catches a MEASUREMENT build (1.123.2).** That is a different failure
from a devkit leak and a worse one to miss: an opt-in profiling macro somebody
turned on for one A/B and did not turn back off. Nothing about the ELF looks
unusual, the game runs, and the only symptom is host writes inside a sampling
window - which is how a VRAM census once shipped live at about 1 ms a frame and
1,016 host: lines per 1,440-frame run, and was briefly mis-attributed to the
change being measured beside it. The audit scans `.rodata` for the log tags
those macros own - `FTCLIP` and `FTPKT` (`TYRA_FRAME_PROFILE`), `STAPIPRET`,
`STAPIPMISS`, `STAPIPBAKE`, `VRAMRES` and `VRAMEVICT` (the census),
`ROADINDEXVERIFY` and `WHEELBAKE` (the generated game's own gates) - and
reports each as `measurement build - .rodata`. Each tag exists only while its
macro is 1, so a hit is proof rather than a hint.

Verified both ways on `examples/vehicle-playground`: the ordinary build reports
five devkit strings and no measurement finding; the same project built with
`TYRA_WHEEL_REBUILD_REPORT=1` adds `WHEELBAKE`, and with `TYRA_FRAME_PROFILE=1`
adds `FTCLIP` and `FTPKT`. Falsify it that way whenever the tag list changes -
a detector nobody has seen fire is not a detector.

## Tools

| Tool | What it does |
|---|---|
| [Live Link](live-link.md) | Streams object transforms and simple property edits |
| [Live Logic](live-logic.md) | Hot-patches compatible flow-graph changes |
| [Live Debugger](live-debugger.md) | Breakpoints, stepping, counters and watches |
| [Script debugger](script-debugger.md) | GDB / VS Code breakpoints, stepping and locals in C++ scripts (PCSX2 only) |
| [Time Machine](time-machine.md) | Captures and restores recent game state |
| [Remote Pad](remote-pad.md) | Drives pad 1 or 2 without window focus |
| [Input recorder](input-replay.md) | Records a play session and performs it again |
| Debug window | Shows game logs, frame statistics, crashes and VU1 captures |
| Debugger > Screen | Makes the game photograph its own frame buffer |

Turn off channels you are not using when measuring performance. Debug file
polling is real work and can distort small timings. On real hardware it is not
small: the same scene measured **30-40 fps with the devkit polling and 50 fps
without it**, because a poll cycle is ~21 network round trips and one of them
writes ~10 KB in 1446-byte chunks.

The channels start on **different frames** — phases 5, 7, 9, 13, 17 and 21,
with the livedbg flush and Remote Pad deliberately left at 1 (liveness signal,
and latency). They used to all start at 1 and re-arm to the same number, so
they stayed locked to one frame in 25 for ever: that frame did seven blocking
`host:` round trips and the other 24 did none. Same total work, spread out.

The input recorder is phase **7**, and it is the odd one out in period as well:
it writes once per **chunk** — 64 frames under PCSX2, 256 over ps2link — not
once per poll cycle. A per-frame write would be a `host:` round trip per frame
and would change the very frame times the recording exists to capture.

## Frame statistics

The running game reports a compact snapshot containing:

- frame rate and current scene;
- draw flushes, vertices and VU1 quadwords;
- GS VRAM free, low-water and largest block;
- resident objects by type;
- optional free EE memory - measured ON REQUEST (*Measure now*), because the
  only way to ask this allocator how much it has left is to take all of it and
  give it back: `Info::getFreeRAMSize` claims every free block until malloc
  refuses, sums them and frees the chain. Thousands of allocations, so it is a
  button and not a timer. **A measurement that comes back 0 MB is reported as a
  failure rather than as a reading** - on a running game that number is either
  an emergency or a broken probe, and the panel used to show it as "not
  measured yet", which read as the button doing nothing (1.85.1);
- one row per render-bag flush.

The Debug window decodes it. Use the flush map to find a heavy draw before
capturing it; a terrain bag may contain many meshes, while a suspicious model
may be only one row.

## Crashes and hangs

The game writes `bin/log.txt` through PCSX2 HostFs. TyraX watches the heartbeat
and opens the latest assertion or crash report when the game stops advancing.

Debug builds can also keep an unstripped `bin/<name>.elf.sym`. Resolve an address
with:

```text
tyrax-editor --symbolize <projectDir> 0xADDRESS
```

PCSX2 does not reproduce every EE exception, so a crash handler must ultimately
be checked on real hardware. Assertions and soft errors are testable in the
emulator.

### A null pointer is reported too

The handler hooks address errors, bus errors, reserved instructions,
coprocessor-unusable, overflow, traps **and the TLB causes** (1–3). A PS2 has no
demand paging — RAM, its uncached windows and the scratchpad are mapped by fixed
entries at boot — so a TLB refill always means an access to nothing: a null or
wild pointer, the most ordinary C++ bug there is. Before 2.15.0 those causes were
handed back to the kernel, so exactly that bug produced no `bin/crash.txt` and
no TYRAX banner, only ps2link's own register dump. Now the console log says

```text
====ERR: TYRAX EE CRASH: TLB refill on store (excCode 3) epc 0x0017981c badvaddr 0x00000004 ra 0x0017981c - writing bin/crash.txt
====ERR: TYRAX EE CRASH: report written
```

and the report follows. Read it like this:

- ExcCode **3** is a TLB refill on store, **2** the same on a load. Either one
  means *this address is not mapped*, and a small `badvaddr` means the pointer
  was null.
- `badvaddr` is the **offset of the field** that was touched: `0x00000004` is a
  store four bytes into a null object, which usually names the member outright.
- `epc` is the faulting instruction, so `--symbolize` finishes the job:

```text
tyrax-editor --symbolize <projectDir> 0x0017981c
```

If the report itself faults (stdio and the `host:` write go over SIF), the TV
screen adds an **"AND A SECOND FAULT WHILE REPORTING IT"** line with that
fault's code and address, instead of the EE looping on it silently.

**PCSX2 cannot show you any of this**: its main RAM starts at address 0, so a
null store is an ordinary write there and the game runs on happily. A
null-pointer bug on this platform is a hardware-only symptom.

### The SIF RPC completion guard

Heavy `host:` polling makes the devkit path an unusually busy SIF RPC client,
and ps2sdk's completion handler dereferences a null packet instead of ignoring
it — `BadAddr 0x00000010`, EPC in `rpc_packet_free`. The engine installs its own
handler that skips only that free and **always completes the client**, counting
what it skipped (`SifRpcGuard::rejected()`, 0 in a healthy session; a non-zero
count logs `==WARN: SIF RPC: dropped …`).

It is **not** gated on the debug profile, because a duplicate completion would
kill a release game too, and `--audit-release` stays clean either way (verified
in both directions). The mechanism, the measured teardown A/B, and the fact that
the fault has **never been reproduced** are in
[ps2link-setup.md](ps2link-setup.md#the-sif-rpc-completion-crash-and-the-guard-against-it).

## The game's own screenshot

![Debugger Screen capture with the Copy image action](img/debugger-copy-image.png)

In **Debugger > Screen**, press **Capture frame**. The game reads its last
finished frame straight out of GS VRAM and hands it over as `bin/frame.tga` -
through the IOP on a console ([devlink.md](devlink.md)), as a `host:` file in
PCSX2 - and the panel shows it.

Every capture is then **kept as a PNG in the project's `screenshots/` folder**,
named by the clock (`frame-20260817-164501.png`). **Show file** reveals that
copy, while **Copy image** puts the decoded bitmap itself on the desktop
clipboard, ready to paste into chat or an image editor. On Windows it is
published the way Paint and the browsers do it — the PNG's bytes as `PNG` plus
a classic bottom-up `CF_DIB` — because Electron apps (Claude, Slack, Discord)
read `PNG` first and failed the paste on the top-down `CF_DIBV5` alone. On
Linux this uses `wl-copy` or `xclip`; the button reports failure when neither
is installed.
`bin/frame.tga` is a *channel*, not an album — one file, overwritten by
the next capture and deleted at every launch — so the PNG is the one that lasts:
it sits outside `bin/`, survives a *Clean*, opens in anything, and is
git-ignored. Delete the ones you do not want; nothing reads them.

From a shell, `tyrax-editor --capture-frame <projectDir> [-o out.png]
[--timeout s]` does the same thing headlessly: it writes the one-shot command
into `bin/livedbg.cmd`, waits for a complete `bin/frame.tga` (progress
is what decides the wait, so the ~3 s per shot over ps2link is fine) and
decodes it to a PNG. On a console the process that owns the session (the
editor, or the `--run-ps2` that deployed the game) forwards `livedbg.cmd` to
the IOP and writes the `frame.tga` that comes back, so the command is the same
either way: about 2 s per picture on a PS2. It is how an unattended A/B on a console gets its
pictures - a debug build with *Live Debugger* on, deployed with `--run-ps2`.
`--alpha <png>` writes the frame's own alpha channel as a grey image as well
(the game stores it in `frame.tga` as is since 1.70.3; the picture readers
force it opaque) - the only way to see an alpha-shaped artifact that the
television shows and the RGB does not.

This is the only capture path that does not need a desktop. Every host-side one
— the emulator's F8 key, a GDI grab of the window, `PrintWindow` — needs the
window present and, in practice, unoccluded and on an unlocked session; none of
them exists at all on a real PlayStation 2. So this is how you get a picture
from hardware, from a locked or disconnected machine, and from an unattended
script that must not depend on which window happens to be in front.

What comes back is the **frame buffer as the GS holds it**, not the television
picture: the raster the game rendered at, with no aspect correction and no
letterbox. A 512x448 project answers 512x448.

It costs the game a hitch, which is why it is one shot per press rather than
anything continuous: around a megabyte is read back out of VRAM, 16 scan lines
per transfer, into one block of RAM. What happens to that block decides how
long the hitch is:

- **On a console with devlink up** the block is handed to
  `DevLink::writeFileOwned` and the game carries on. It leaves in ~30 slot-sized
  segments over the next frames (one at a time, so the debugger's reports and
  the log keep their slot), the IOP sends them as UDP datagrams, and the editor
  writes `bin/frame.tga` when the last one is in, with the game running
  throughout. The block is freed once it has left. **Measured on a PS2
  (2026-11-10, 512x512 at 32 bits, 1 MB): the readback holds the game for
  34 ms - two fields - and the picture is in the editor 1.4-1.7 s later**
  (the console log says `readback N ms`, the editor's `received frame.tga
  (... bytes in N ms)`). Those datagrams are not retransmitted, so the
  Debugger and `--capture-frame` ask again when no picture has arrived after
  8 s.
- **In PCSX2, or with devlink off**, the block is one `host:` write. In PCSX2
  that is over between two frames.
- **With no RAM for the block** (a game at the edge of its 32 MB) the picture is
  streamed to `host:` a line at a time, as it always was before 2.15.0: over
  ps2link that is a network round trip per 1.4 KB, **measured at about three
  seconds on a 512x512 buffer, with the game frozen for all of it**. The log
  line ends in `streamed - no RAM for the whole picture` when this happened.

The picture is the **previous real frame**: the last one the scene rendered, so
never a half-composed image and never a synthesised
[extrapolated](frame-extrapolation.md) one.

One thing that will look like a bug and is not: the capture is taken between
frames, so it can be one frame older than what you were looking at when you
pressed the button. The alpha channel needs no care — a frame buffer's alpha is
a working channel rather than coverage, so the game writes the file **opaque**
and both the panel and the PNG show a picture rather than a half-transparent
one.

### Live preview

![A live preview picture from a real PlayStation 2](img/live-preview-ps2.png)

**Debugger > Screen > Live preview** turns the one-shot capture into a small
picture stream: the game sends a quarter-resolution picture of its last
finished frame, and sends the next one as soon as that has left. On a console
it travels through the IOP ([devlink.md](devlink.md)); in PCSX2 it is a
`host:` file every third frame. The panel shows the picture, how many arrive a
second and what each one cost the game.

Measured on a PS2 (2026-11-10, 512x512 at 32 bits, the picture above):

| | |
|---|---:|
| Picture | 128x127, 15 bits a pixel, one devlink slot (32.5 KB) |
| Pictures a second at the editor | **about 20** |
| Work per picture on the EE | **1.3 ms** (GS blits 0.27, readback 0.24, conversion 0.74) |
| Game frame rate while streaming | 50 fps, unchanged (a light scene) |

How it stays that cheap: the frame buffer is never read back. The GS shrinks
it, a quadrant at a time, into the post-fx pass's eighth-resolution scratch
buffer - so the preview needs **no VRAM of its own** - and only that tile
comes back over the reverse FIFO: four tiles of ~16 KB instead of a megabyte.
The picture is cropped by a row or two to fit one slot, because one slot is
one pump on the EE and one burst on the IOP.

Two things to know before trusting it:

- **It is not free in a scene with no slack.** The readback needs VIF1 idle,
  so each picture waits for the frame in flight to finish (up to a field with
  the frame pipeline on). The game would sit that wait out at the flip anyway,
  which is why the test scene held 50 fps - but that frame loses its EE/GPU
  overlap. Leave the preview off when you measure frame times.
- **It shimmers on fine detail.** Each preview pixel stands for 4x4 source
  pixels and one bilinear tap covers 2x2 of them. It is a preview, not a
  capture: press *Capture frame* for the real thing.

The switch is one-shot in the command (it rides the step field as -9 / -10,
every flag bit being taken) and lasts until switched off or the game ends.

### Why the file is written by the game rather than by libdebug

Worth knowing before touching that code, because the failure is silent and it
made this an **emulator-only feature for its first release**. ps2sdk's
`ps2_screenshot_file()` creates its output with `open(name, O_CREAT|O_WRONLY)`,
and over ps2link that create arrives at the `host:` server as a **mkdir of the
target name**: the host ends up with a *directory* called `frame.tga`, the open
that follows returns -1, and the function reports nothing at all — it has no
failure path. On hardware the log reads

```text
remove file host:frame.tga
mkdir name host:frame.tga
mkdir wrong mode, using fallback value 493
open name host:frame.tga flag 202  ->  open fd = -1
```

while every other devkit channel, writing through `fopen(name, "wb")` (flags
`0x602` on the wire), succeeds in the same session over the same server. So the
runtime keeps the half of libdebug that carries the value — `ps2_screenshot()`,
the VRAM readback — and writes the file through the same stdio path everything
else here uses. Two hardware-only traps come with that: the readback lands in
RAM **behind the EE's data cache** (the cache is flushed after every transfer, or
rows repeat), and `ps2_screenshot()` refuses to run while VIF1's DMA channel is
busy and says so only through its return value, so refusals are counted and
reported instead of being written out as picture.

## Inspecting VU1 input

In **Debugger > VU memory**, arm a capture and choose a flush. The game writes
`bin/vucap.bin`; the editor shows DMA/VIF commands, mesh boundaries, vertices
and common packet problems.

The command-line decoder is useful when the UI is not:

```text
tyrax-editor --dump-vucap <projectDir>
```

A capture contains one bag flush, which can hold several meshes. Use the mesh
picker before assuming the first geometry belongs to the object you care about.
Only the last mesh in a multi-mesh chain can currently be replayed by the host
VU simulator.

Two limits are worth knowing before a capture is used as evidence rather than as
a debugging aid.

**The capture buffers clamp silently, and a heavy scene is already at the edge.**
The devkit holds 2 048 quadwords of chain, 64 referenced blocks and 2 048
quadwords of referenced data, and every one of those limits is applied by
truncation with no marker in the file. The Motor District's garage-day frame
submits about 1 050 packages over 120 flushes, i.e. 8.75 packages per flush,
which is roughly **1 986 referenced quadwords against the 2 048 limit — 97%
full**. A flush carrying the full 16 groups overflows it and nothing says so.

**The VU1 memory half is one package's residue, not a frame's output.** The
second hook waits for VU1 to idle and copies the whole of VU1 data memory, which
includes the GIF packet the microprogram staged for `XGKICK` — but that lives in
the double buffer and every package overwrites it, so what the snapshot holds is
whatever the **last** package of that one chain left. It is the right instrument
for "what did this draw hand the GS"; it is not, and cannot be made into, a
recording of the frame's GS stream. See
[baked-stream-acceptance-gate.md](baked-stream-acceptance-gate.md), which wanted
exactly that and had to build something else.

For VU source work, the faster checks are:

```text
tyrax-editor --vu-check
tyrax-editor --vu-replay <projectDir>
```

`--vu-check` compares generated and handwritten programs from the same editor
build. Rebuild first; mixing an old executable with a new engine directory
creates false failures.

## Find the live project

When several editors or emulators are open, do not guess paths:

```text
tyrax-editor --debug-state
```

It lists live sessions, transports and the freshest debug files, including the
frame and flush stored in a VU capture. Pass a project directory to inspect one
known project directly.

On hardware, the editor-owned `ps2client` is also the file server. Closing the
editor freezes devkit files even if the game keeps running. Redeploy, or start
`ps2client ... listen` from the project's `bin/` directory to restore the
channel.

### What the game already logs about itself

`bin/log.txt` is not only warnings and assertions - a few events that are hard
to reconstruct afterwards write a line each, so a report like *"I picked it up
and something threw me across the room"* can be read off the log instead of
guessed at. Grep for the prefix:

| Line | When |
| --- | --- |
| `Portal: player crossed <pi> to x y z` | a walker went through portal `pi`, with the arrival position |
| `Portal: object <oi> crossed <pi> to x y z` | a body did |
| `Pick: grabbed <oi> at x y z, player x y z` | an object was picked up, with both halves of that frame |
| `Pick: dropped <oi> …` / `Pick: threw <oi> … v vx vy vz` | it was put down or thrown |
| `Pick: lost <oi> mid-carry …` | it was despawned or hidden while carried |

Paired with an [input recording](input-replay.md) these turn a "it sometimes
does X" report into a fixture: replay the session, read the order of the lines.
That is how the carry and portal interactions are debugged here - the hop and
the grab are one frame apart or they are not, and the log says which.

Since 1.85.0 the same events are also **written into the recording itself**
(`--replay-dump` prints them with no emulator at all, and a replay compares
them frame by frame). The log lines stay: they carry the positions and the
velocities, which the events deliberately do not.

## First places to look

1. **Output** for build and launch failures.
2. `bin/log.txt` for game warnings and assertions.
3. `--debug-state` to confirm the project, transport and heartbeat.
4. The Debug window's frame and flush tables.
5. A VU capture only after you know which flush is wrong.

The devkit reports evidence; it does not prove performance on its own. Use real
hardware for timings and the PCSX2 software renderer for visual correctness.

### Manual ps2link launch: require the resident-IOP marker

Prefer `--build PROJECT --run-ps2 IP`, which creates the marker automatically.
If running ps2client manually, first create and verify **an absolute path** to
`PROJECT/bin/ps2link.run` containing `ps2link`. Launch with `PROJECT/bin` as the
working directory. `-ps2link` on execee alone is insufficient with this crt0.
A missing marker resets IOP, removes the host filesystem/network service and
can require a physical reboot; a later UDP poweroff cannot repair that loss.
Do not chain a failed marker write into an execee command. In PowerShell use
`-ErrorAction Stop` and `Test-Path -LiteralPath` before launching. A working
directory already ending in `bin` must not receive another relative `bin/`.
Also stop an emulator serving the same project before hardware captures: its
fresh `livedbg.bin`/`frame.tga` can otherwise disguise a disconnected console.

## Native hardware timeline

**Debugger > Hardware timeline** reads bounded engine captures without extra
polling: arm the next boot, load the completed CSV, choose a frame and zoom.
See [hardware profiler](hardware-profiler.md) for limits and HTML/Perfetto export.
