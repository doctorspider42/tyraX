# The devkit — and the promise that a shipped game pays nothing for it

TyraX has grown a development kit for the PlayStation 2: the editor talks to a
game running on the console, and the game talks back.

| Layer | Direction | What it does |
|---|---|---|
| [Live Link](live-link.md) | editor → game | object transforms/colors, adds/deletes, texture hot reload |
| [Live Debugger](live-debugger.md) | game → editor **+** commands back | what the graphs run, breakpoints, pause/step, watches, timers |
| [Live Logic](live-logic.md) | editor → game | the flow-graph **program** itself, no rebuild |

All three ride one channel: the host filesystem the game already loads assets
from (PCSX2's *Host Filesystem*, the ps2link file server on real hardware). No
extra transport, no engine debug stub, no devkit hardware.

## The promise: release builds carry nothing

The devkit exists only in **debug** builds, and "only" means *literally* — not
"disabled at runtime", not "a few dead branches", not "some tables nobody
reads":

- each generated runtime (`live_link.gen.cpp`, `live_debug.gen.cpp`,
  `live_logic.gen.cpp`, `live_tex.gen.cpp`) becomes an **empty translation unit**;
- the generated headers keep the API but every entry point is an `inline` no-op,
  and the predicates (`livedbg::halted()`, `livelogic::patched()`) become
  compile-time `false`, so the calls in the game loop and in every compiled flow
  graph **fold away** rather than branch;
- the instrumentation inside `flow_graph.gen.cpp` (`livedbg::hit`,
  `livedbg::timer`, the force-fire duplicate of each trigger branch) is not
  emitted at all;
- nothing polls, nothing writes, and the static arrays the debugger and the
  interpreter need (~145 KiB of `.bss`) do not exist.

### And the promise is checked, not asserted

The PS2 toolchain strips the symbol table, so each devkit runtime plants a
deliberate marker string (`TXDEVKIT-<layer>`) and the audit looks for that plus
the channel file names — signals that cannot survive if the code was not
compiled in.

```bash
tyrax-editor --audit-release <projectDir>
```

Exit code 0 = clean, 1 = something leaked (so a script can gate a release on it).
**Every release build runs the same audit automatically** and prints the verdict
into the build log:

```
[editor] Release audit: clean - ELF 2641 KiB (text 1830 KiB, data 205 KiB, bss 139 KiB), stripped (marker + string scan)
```

Measured on the same project, same assets, same code — only the profile changed:

| | text | data | bss | verdict |
|---|---|---|---|---|
| debug (devkit on) | 1848 KiB | 205 KiB | **284 KiB** | 4 devkit findings |
| release | 1830 KiB | 205 KiB | **139 KiB** | clean |

So the devkit costs ~18 KiB of code and ~145 KiB of RAM **while you are working**,
and exactly nothing in what you ship. The reader behind this
([`src/elfsym.hpp`](../src/elfsym.hpp)) is a small ELF32 parser; it is also the
foundation for reading named memory off a running game later.

## Turning it off while still in debug

Each layer is its own project preference (*Project > Preferences > Build*, or the
*Build* menu): **Live Link**, **Live Debugger**, **Live Logic**. Turning one off
compiles it out of the debug build too — the same empty-TU path — which is the
honest way to measure "what does my game do without the devkit" without
switching profiles.

## What the debugger gives you today

Beyond breakpoints and stepping (see [live-debugger.md](live-debugger.md)):

- **Armed timers.** Every `Delay` counting down reports itself, so the panel
  shows *"⏱ 1 armed timer, next in 1.2 s"* instead of leaving you wondering why
  the branch after it never came. A `Delay` only advances on frames that **run** —
  which is exactly why a single-frame *Fire* looks like it did nothing.
- **Fire and continue.** *Fire now* runs a trigger's branch for one frame;
  *Fire and continue* resumes the game afterwards, so whatever the branch armed
  (a `Delay`, a `Move Object To` glide) actually gets frames to finish.
  Shift-click the Fire button for the same thing.
- **Object watch.** Name up to 8 runtime objects and the game samples them
  **every frame** (position, rotation, scale, color, visible/active/dirty) into a
  ring it flushes whole — so the editor draws a real 50 Hz curve per axis, not
  one point per flush. The path is also drawn **in the viewport** as a trail with
  the head marking where the object is right now. This is the "where is it
  actually, and what did it do a second ago" tool.


## When it crashes

Three different things can go wrong in a running game, and they used to have
very different visibility:

| | before | now |
|---|---|---|
| `TYRA_ASSERT` / recovered asset error | delimited block in the log, editor dialog | same, and the banner is **TYRAX** |
| a real EE exception (bad pointer, address error, reserved instruction) | **nothing** - the game just stops | crash report + symbolized backtrace (experimental, below) |
| a hang | **nothing** | the editor notices the heartbeat stopped and shows the post-mortem |

### The heartbeat, and the post-mortem

The devkit already knows whether the game is alive: it flushes a snapshot every
few frames. When those stop arriving with no crash report and no assertion, the
Debugger says so - **"the game stopped reporting at frame N"** - and shows what
it still holds from the seconds before: the last flow-graph nodes that ran, the
watched objects' last positions, the armed timers. That is the difference between
"it froze" and "it froze right after `Chase Player` fired on frame 1487".

This part needs nothing from the game and works on hardware and in PCSX2 alike.

### The EE crash handler (experimental, opt-in)

*Project > Preferences > Build > "EE crash handler"*, **off by default**.

With it on, a debug build installs the engine's handler
([`vendor/tyra/engine/src/debug/crash_handler.cpp`](../vendor/tyra/engine/src/debug/crash_handler.cpp)),
built on ps2sdk's `libeedebug`: the handler copies the register frame, harvests
plausible return addresses off the stack for a backtrace, then **redirects the
frame's EPC at a trampoline and returns** - so the report is written from
ordinary context, where stdio and the host: filesystem work again. Doing file
I/O inside the exception context instead is the classic way to turn a crash into
a hang. The game then halts quietly with the last frame on screen, exactly like
a failed assertion.

The report is `bin/crash.txt`: decoded cause (`Cause.ExcCode` -> "Address error
on store"), EPC, BadVAddr, all 32 GPRs, and the backtrace candidates. The editor
parses it, pops the Debugger and offers **Resolve names** - which runs the PS2
toolchain's `addr2line` in the build container against the **unstripped copy** a
debug build keeps (`bin/<name>.elf.sym`, written by `Makefile.base` when the
generated Makefile sets `KEEPSYM=1`; the shipped ELF is stripped). So a crash
address becomes `TerrainGame::renderOnePortalView(int)` at
`src/terrain_game.cpp:7913`. The same lookup is available headlessly:

```bash
tyrax-editor --symbolize <projectDir> 0x00120000 0x00180000
```

**Why it is opt-in.** Measured under PCSX2: `ee_dbg_install()` wedges the game
the moment the handlers go in - the loop stops, nothing more reaches the log and
the devkit heartbeat dies on the first frame. Narrowing the hooked causes did not
help, so it is the install itself, in the emulator. Two traps were found and
fixed along the way and they are worth knowing before touching this code:

- **never hook cause 0 (Interrupt)** - that hijacks vblank/timer/DMA dispatch, so
  no thread ever runs again;
- **never hook cause 8 (Syscall)**, nor TLB refill (2, 3) / TLB modified (1) -
  those are how ordinary memory traffic and every kernel service are *serviced*,
  not faults to report.

The handler now hooks only genuine faults (4, 5, 6, 7, 9, 10, 11, 12, 13, 15).
It still needs a pass on real hardware before it can be the default; until then
the preference exists so nobody's debug build changes behaviour by surprise.
Since installation is also what LINKS the handler out of `libtyra.a`, a project
that leaves it off carries none of it - and neither does any release build.

## Seeing what VU1 was fed

VU1 debugging is blind by nature: you cannot print from a microprogram and its
output goes straight to the GS. But its **input** is ours - every vertex the
static pipeline draws leaves the EE as one DMA chain built by
`StaPipQBufferRenderer::sendPacket()`. The devkit taps exactly that.

*Debugger > VU > "Capture VU1 packet"* asks the game for one chain; it lands as
`bin/vucap.bin` and the editor decodes it:

- the **chain itself**, tag by tag and VIF code by VIF code - `STCYCL`, the
  `UNPACK`s with their VU1 destination addresses, `FLUSH`, and the `MSCAL` that
  names the microprogram entry point;
- every **UNPACKed block**, read back as floats (the scales, the GIF tag, the
  matrices, the vertex arrays);
- and a **wireframe** of the vertex stream - drag to orbit, wheel to zoom -
  drawn from the exact numbers that went to VU1, in model space, as packed.

### Which draw am I looking at?

A frame sends **one chain per bag flush**, always in the same order, so "the next
packet" forever means the same picture forever. The capture therefore carries an
index, and the button walks it: click, click, click and you step through the
frame's draws (`flush 3 of 9` in the header). Tick **pin flush** to hold one
index instead - that is the mode for watching a single draw change over time.
The index is exact, not "the first one after arming": arming happens mid-frame,
so the game waits for the real flush number, which costs at most a frame. If the
index runs past what a frame sends (the count moves with streaming) it wraps to
0, and the file always reports the index actually taken.

The request rides in spare bits of `livedbg.cmd`'s flags word (bit 4 = "an index
follows in bits 8-23"), so a game built before this simply keeps grabbing the
first flush.

**One flush is a whole bag, not a mesh.** The chain carries a dozen position
streams - one per mesh the bag flushed - and each is in its OWN model space, so
they cannot be drawn together. The panel lists them (`N mesh(es)`) with vertex
and triangle counts, model-space size, degenerate-triangle count and the
microprogram each runs under; the slider picks which one the wireframe shows and
`--dump-vucap` prints the same list. Positions are told apart from the
same-sized ST/Q array that follows them by their w component: the pipeline packs
`(x, y, z, 1.0)` and nothing else in the chain has a constant 1.0 there. A mesh
can read `program carried over (MSCNT)`: that chain never says `MSCAL`, because
the program was already loaded by an earlier one.

**Two captures of the same draw look identical, and that is not a bug** - the
model-space geometry of a mesh does not depend on the camera. What moves is the
frame number, the MVP, and the staged GS vertices. (It *was* a bug for a while
on the editor side: the panel re-read `vucap.bin` only when its SIZE changed, and
a second capture of the same draw is the same length down to the byte - so the
panel showed the first capture forever while the game happily overwrote the file.
It keys on the timestamp now, and says "waiting for the game..." until the answer
to your click actually lands.)

A real capture from a terrain chunk reads:

```
frame 1669, 73 quadwords, 36 unpacks, 7 triangles
bag flush 1 of 9 this frame; rendering at 512x448
[   0] DMAtag cnt  qwc=2 (inline)          <- the mesh's constants
[   0]   VIF UNPACK V4_32   num=2  -> VU1 addr 0 (+TOPS)
[   3] DMAtag ref  qwc=21 (data by reference)   <- POSITIONS
[   3]   VIF UNPACK V4_32   num=21 -> VU1 addr 2 (+TOPS)
[   4] DMAtag ref  qwc=21 (data by reference)   <- ST/Q, right after them
[   4]   VIF UNPACK V4_32   num=21 -> VU1 addr 23 (+TOPS)
[   5]   VIF MSCAL      num=0 imm=176       <- load and run; later meshes MSCNT
meshes in this flush: 12
  mesh 0: 21 verts (7 tris), 116.7 units across, program @176, unpack 1 -> VU1 2
  ...
```

Reading the chain is mostly reading its **repeat**: the same three-block pattern
per mesh, ending in a program call. Three things are worth knowing.

- **`ref` vs `cnt`** - `ref` is a single-quadword tag pointing at data elsewhere;
  `cnt` carries it inline. (See the bottom of this section - getting that wrong
  is how a decoder starts reading vertex data as DMA tags.)
- **`+TOPS`** - the address is relative to the buffer VU1 is currently filling
  (double buffering), which is why every mesh writes to "addr 2" and nothing
  collides. The second array's address is `2 + vertex count`, so `addr 23` for a
  21-vertex mesh - a free sanity check.
- **`MSCAL` once, then `MSCNT`** - the program is loaded once and re-run. Several
  DIFFERENT `MSCAL` addresses in one chain means the bag switched microprograms
  (a textured and an untextured variant, say), which is worth noticing.

The wireframe answers four questions and no more, but answers them exactly: did
this mesh reach VU1 at all, is it the geometry you think it is, is its scale
sane (hundreds of units where you expect single digits usually means a wrong
model unit), and are its triangles degenerate.

### ...and what VU1 left behind

Arming a capture also takes a snapshot of **VU1 data memory** (all 1024
quadwords) right after that chain ran - the engine waits for VIF1 and for the
microprogram to finish, hands the memory over, and uninstalls itself, so the
stall happens for the one frame you asked about and never again. In it:

- the **MVP matrix** the mesh was given (quadword 0), printed as uploaded;
- the vertex arrays as VU1 read them;
- and the **GIF packets the program staged for XGKICK** - decoded to GS vertices:
  screen-space X/Y in 12.4 fixed point, 24-bit Z, RGBAQ, ST, with the primitive
  and register list spelled out (`TRIANGLE +ABE, [RGBAQ, XYZF2], EOP`).

That is the answer to "what did VU1 actually produce": the exact numbers the GS
was about to rasterize, per vertex, before anything reached a pixel.

A real capture (terrain, PCSX2):

```
VU1 data memory: captured (1024 qw)
MVP (as uploaded, column per quadword):
     -0.189     0.000     0.000     0.000
      0.000    -0.189     0.000     0.000
      0.000     0.000    -1.000     0.200
      0.000     0.000     1.000     0.000
gif 1 @VU1 23: TRIANGLE +ABE nloop=21 nreg=2 [RGBAQ, XYZF2] EOP
   out v0  x=1242.3 y=1205.9 z=2655319  rgba 132,60,149,0
```

How to read one of those vertices:

- **x / y** are already divided by 16 (the GS packs 12.4 fixed point), in the
  4096-unit **primitive** plane. The visible window is
  `[2048 - width/2, 2048 + width/2]` on each axis, because `RendererCoreGS` sets
  `XYOFFSET = 2048 - size/2` - for 512x448 that is x 1792..2304, y 1824..2272.
  The capture carries the live resolution (it is decided at runtime, from the
  display mode and the console region), so the panel prints those bounds for
  you. **Vertices outside them are ordinary** - a triangle crossing the screen
  edge has two, and the GS scissors it. What is not ordinary is the whole packet
  missing the window, which is the finding the panel actually raises.
- **z** is 24-bit and the test is `GREATER_EQUAL`: bigger is closer. Read it
  comparatively.
- **rgba**: `a = 128` is 1.0. `a = 0` in a packet without `+ABE` draws nothing.
- **PRIM** flags are spelled out (`+TEX`, `+FOG`, `+ABE`, `+AA1`): no `+TEX` on
  something you textured is a diagnosis in itself.

### The findings block

Above the chain the panel prints what the decode can say for certain, in amber
when it is a finding: a packet that misses the drawing window entirely, triangles
spanning nearly the whole 4096-unit plane (the classic sign of a vertex at or
behind `w = 0` - the threshold is deliberately huge, because near-camera terrain
legitimately covers several screens), input triangles with no area, vertices that
are fully transparent in a packet that does not blend, and - only on a
single-mesh flush, see below - input vertices behind the camera.

Two scoping rules keep those numbers honest, and both were learned by watching
the first version lie:

- **VU1 data memory is never cleared**, so a scan of all 1024 quadwords also
  finds packets earlier meshes and earlier frames left behind. The per-vertex
  checks read the BIGGEST geometry packet only - this run's output - and the
  panel says so rather than counting leftovers ("60 of 60 vertices are off
  screen" was a decoder artefact, not a bug in the game).
- **The `w <= 0` check needs a single-mesh flush.** The host reference
  transforms the largest mesh's vertices with the MVP left in VU1 memory, which
  belongs to the LAST mesh - on a multi-mesh flush that pairing is meaningless,
  so the finding is withheld instead of guessed.

### The host reference is a hint, not a verdict

The editor also runs the same transform itself (`clip = MVP * v`,
`ndc = clip / w`, `screen = scale * (ndc + 1)`, `ftoi4`) and diffs it against the
decoded output. **Read that number with the caveat it prints**: one flush can
carry SEVERAL meshes in one chain, and VU1 memory holds only the **last** MVP
uploaded - so input block and output packet are not reliably paired yet. In the
capture above X agreed to the LSB and Y did not, and the honest reading is "the
pairing is unproven", not "VU1 has a Y bug" (those vertices happen to share
almost the same X, so the agreement is weak evidence).

Making it a verdict needs one more step, and it is a small one: capture the
object-data chain (the MVP upload) together with the qbuffer chain, and capture a
single-bag flush, so exactly one mesh is in play.

Headless: `tyrax-editor --dump-vucap <projectDir>` prints the whole decode -
chain, memory, staged GIF packets, the findings, the reference and its caveat -
which is how all of this is tested without the GUI.

### Where to look first

| Symptom | Order to check |
|---|---|
| An object does not appear | is it in the mesh list at all? -> did a geometry packet come out? -> does that packet reach the drawing window? -> alpha / `+ABE` -> z |
| Giant smeared triangles | the "spans nearly the whole plane" finding, then the `w <= 0` count on a pinned single-mesh flush |
| Colours wrong | `rgba` in the staged packet, the `REGS` list, missing `+TEX` |
| Frame is slow | meshes per flush, flushes per frame, input triangles vs staged, how many distinct `MSCAL` addresses |

### What it cannot tell you

- **Only the static pipeline.** The tap sits in `StaPipQBufferRenderer`;
  animated models go through DynPip and never appear here.
- **No object names.** A mesh is `mesh 3` - the bag carries geometry, not
  identity. Mapping meshes back to scene objects would need the game's render
  loop to label each submission, which is not plumbed today.
- **One flush per capture**, and the VU1 memory snapshot costs a pipeline stall
  for that one frame (on purpose - the hook uninstalls itself immediately).
- **The MVP in memory is the last one uploaded**, which is what makes the host
  reference a hint rather than a verdict (above).

**The one thing that made this non-trivial**: the pipeline sends vertex arrays
**by reference** - a `ref`/`refs`/`refe` DMA tag whose `qwc` counts quadwords at
*another* address, with the tag itself being a single quadword. Two consequences,
both learned the hard way: a walker must advance by 1 for those tags (and by
`1 + qwc` only for the inline `cnt`/`next` kinds) or it starts reading data as
tags; and the capture has to **follow those references on the EE**, while the
addresses are still live, or the editor receives the structure with none of the
geometry. The tap copies each referenced block along and the file carries an index
of them.

Cost: the engine holds a **null function pointer** and the branch that tests it,
once per bag flush - the capture code lives in the generated devkit TU, so a
release build links none of it (and the audit says so).

## What is not here yet

- **Named memory.** The unstripped `.elf.sym` copy now exists, so a symbol-driven
  memory watch is mostly plumbing: a read request in the command file and a
  response block in the snapshot.
- **Live perf graphs.** The engine already computes per-phase EE times, free RAM
  and GS VRAM residency, but prints them as text (see
  [profiling.md](profiling.md), [gs-vram.md](gs-vram.md)); streaming them over
  this channel would make them curves next to the object watches.
- **VU1's OWN output.** The capture shows what VU1 was *fed*. Reading back what
  it produced means snapshotting VU1 data memory (a VIF1 reverse transfer with the
  pipeline stalled) - the next step, and a bigger one.
- **EE/VU instruction-level stepping.** Deliberately out of scope: PCSX2 already
  has a debugger, and on real hardware the sane path is a GDB stub over ps2link
  rather than a hand-rolled disassembler. What this devkit does instead is show
  the *game's own* state — objects, graphs, variables, timers, and now the packets
  — which is where the bugs of a project like this actually live.
