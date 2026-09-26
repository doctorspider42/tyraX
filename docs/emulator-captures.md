# Emulator captures: savestates and GS dumps without a human

`pcsx2-capture.py` (in `.claude/skills/tyra-testing/scripts/`) launches a
private PCSX2 instance, takes PCSX2 savestates and a single-frame GS dump of a
running game, and turns them into a text report. It needs no window focus and
sends no global input. It also reads captures that someone took by hand, and it
works on any PS2 title, not only on games this editor generates. That is how the
reference numbers at the end of this page were measured.

Use it to answer questions a frame profiler cannot:

- what the GS actually receives: buffer formats, dithering, fill per render
  target, state changes, texture uploads;
- how a commercial game shapes its DMA chains;
- what VIF1 was handed at the end of a frame.

## Usage

```
pip install zstandard numpy        # once

# launch, wait for the game, 3 savestates + 1 GS dump, analyse, stop PCSX2
python .claude/skills/tyra-testing/scripts/pcsx2-capture.py run <game.elf> --stage --states 3 --gsdump

# analyse captures taken by hand (F1 savestate, Shift+F8 GS dump)
python .claude/skills/tyra-testing/scripts/pcsx2-capture.py gs    <dump.gs.zst> [--passes] [--frame 1]
python .claude/skills/tyra-testing/scripts/pcsx2-capture.py state <state.p2s>  [--span 0x200000]
```

`run` writes `state<N>.p2s`, `frame.gs.zst` and `report.txt` into
`%TEMP%\tyra-editor-test\pcsx2-capture-<slot>\out-<time>\`, or into `--out`.
It stops only the process it started, by PID.

**Useful flags:**

- `--slot`: pick a different PINE port for each parallel run. The default is
  28190. The user's PCSX2 uses 28011 when PINE is on.
- `--wait-log <regex>`: wait for a line in the emulator log instead of the
  fixed `--boot-wait`.
- `--keep`: leave the emulator running.
- `--stage`: copy the ELF's directory to a short private path. It also keeps
  this run's channel files (`livepad.bin`, `log.txt`) apart from an editor that
  is running the same project. Staging happens automatically for paths over
  100 characters.

## How it works

**A private instance.** The emulator runs as
`pcsx2-qt -batch -nogui -datapath <run dir> -elf <elf>`.

- The run directory gets a copy of the user's `PCSX2.ini` and BIOS, with PINE
  enabled on the chosen slot, HostFs on, and `GSDumpSingleFrame` rebound to a
  bare `F12`.
- The user's global `PCSX2.ini` is never written. The editor rewrites that file
  on every Run, and the user's own emulator reads it.

**Savestates over PINE.** PINE is PCSX2's IPC socket: TCP on
`127.0.0.1:<slot>` on Windows, `$XDG_RUNTIME_DIR/pcsx2.sock.<slot>` on Linux.
Opcode 9 saves to a slot.

**The GS dump through PCSX2's own hotkey.** PCSX2 has no command-line flag and
no PINE opcode for a GS dump. The script posts `WM_KEYDOWN`/`WM_KEYUP` for F12
to the windows of the PID it started. `PostMessage` queues on that one process's
thread, so nothing reaches the person at the keyboard and no focus moves. This
works on Windows only; on Linux the GS dump step is skipped and says so.

## What the analysers read

**`gs`** replays the GIF stream of the dump, starting from the GS state it opens
with, and reports:

- `DISPFB`/`PMODE`: what is displayed, and in which format;
- `FRAME`/`DTHE` per draw: whether dithering is on, and into which format;
- fill per render target, primitive counts, and `PRIM` / `TEX0` / `FRAME` changes;
- texture uploads (`TRXDIR`) by format.

`--passes` prints one frame's pass timeline. Each line is a run of draws with
the same target, texture, blend and write mask, which is how a post-effect chain
becomes readable.

**`state`** unpacks `eeMemory.bin` and `eeHwRegs.bin` from the savestate. It
prints the DMAC channel registers, then searches EE RAM below `D1_TADR` for DMA
tag chains that end in `END`. For each chain it reports:

- the tag mix;
- how many bytes sit inline in the per-frame buffer and how many are
  referenced or `CALL`ed from elsewhere, i.e. EE-written versus static data;
- the call targets;
- the VIF codes the tags carry.

## Traps, each paid for once

- **`-datapath DIR` reads `DIR\PCSX2\inis\PCSX2.ini` and `DIR\PCSX2\bios`**, not
  `DIR\inis`. A missing ini opens the setup wizard on the user's screen.
- **Write the ini without a BOM.**
- **PINE answers before the game exists.** The GUI's socket is up first. The
  title command fails until a VM runs, so the script uses that as its readiness
  test.
- **PINE reads hardware registers as zero.** `0x1000xxxx` comes back empty.
  DMAC state only comes from a savestate's `eeHwRegs.bin`. EE RAM reads over
  PINE do work, at about 9 000 per second.
- **A savestate is written at VSync.** VIF1 is therefore always found idle
  after `END`. That is why `state` recovers chains from RAM instead of following
  a live `TADR`.
- **A `.p2s` is a zip of zstd entries (method 93).** Python's `zipfile` refuses
  them. The script decompresses the raw entry data itself.
- **The dump's freeze block (state version 9) has no `PRMODE` field.** The order
  is PRIM, PRMODECONT, TEXCLUT, SCANMSK, TEXA, FOGCOL, DIMX, DTHE, COLCLAMP,
  PABE, the transfer registers, then twelve per-context registers ×2. Read with
  `PRMODE` included, every later register shifts and `FRAME` decodes as garbage.
- **Pixel counts are coverage, not fill.** Each triangle's area is scaled by the
  scissored share of its bounding box, with no depth test. Treat it as an upper
  bound: good for comparing passes and games, not for pricing the GS.
- **A TyraX frame is not in RAM whole.** StaPip sends 100+ small packets a frame
  from two ping-pong buffers (`StaPipQBufferRenderer::sendPacket`). A savestate
  shows at most the last one or two, and the chain `state` finds below `TADR` is
  usually something else. For TyraX's own chains, use the VU1 packet tap
  (`g_vuPacketHook`, `arm-vucap.py`, [devkit.md](devkit.md)). The savestate side
  is for games that build one frame chain.
- **The ELF path must be short.** PS2 `loadelf` truncates long `host:` paths and
  jumps to PC 0, which is why `--stage` exists.

## Reference: a commercial 60 Hz PS2 racing title, captured 2026-09-20 and 2026-09-23

These are measured numbers from one race scene of a shipped game that holds
60 Hz, recorded as a frame-rate yardstick for
[ee-submission-rearchitecture.md](ee-submission-rearchitecture.md). They describe
what reaches the hardware, not how that game is written, and they are not a
design to copy.

**What reaches the GS, per frame:**

- ~4.4–5.1 Mpx of coverage, almost all of it blended and textured;
- ~33–35 k primitives, nearly all triangle strips, and ~57–62 k vertex kicks;
- ~550 state runs and ~640 texture uploads, about 3.3 MB of `IMAGE` per frame.

**Buffers:**

- The scene renders into **CT32 + Z24** with `DTHE=0`.
- One 640×448 blit copies it, read as `CT24`, into a **CT16S** display buffer
  with `DTHE=1`. Only the output is dithered.
- After the scene they reuse the Z buffer's VRAM for blur ping-pong. They read
  depth as a texture (`Z16`) and the framebuffer's alpha as a palette index
  (`T8H`).

**How the scene reaches VU1:**

- **One VIF1 chain carries the whole 3D scene**, 4 300–5 000 tags. The frame
  buffer holding it (225–260 KB) is evidently assembled in the scratchpad and
  written out by `fromSPR` DMA: in every savestate `fromSPR`'s `MADR` stops where
  that buffer ends. DMA-written RAM needs no data-cache write-back. A few small chains
  (HUD, 2D) follow it.
- **745–969 `CALL`s into 600–760 prebaked blocks.** Each block begins with its
  own `RET` tag and carries `UNPACK`s: V4-32 position, V2-16
  UV, V3-8 and V4-8, about 68 vertices per chunk, `STCYCL` 3/1 with `TOPS`
  double buffering and `ITOP` = vertex count. That is **1.3–1.75 MB of static
  data a frame the EE never touches**, against 169–195 KB it writes itself.
- **Per object, the EE writes about 16 qw:** a ~10 qw `UNPACK` of matrix and
  parameters, a 5 qw `DIRECT` of GS state, and `MSCNT` between chunks.
- **It also drains VU1 constantly:** `FLUSHE` about 600 times a frame. A
  per-mesh `FLUSHE` is not what separates us from 60 Hz.

For comparison, a TyraX frame in the same tool (vehicle-playground, its boot
view, PCSX2, 2026-09-23): ~1.1 Mpx of coverage and ~30 k primitives per
VSync, all CT32 with no dithering and no uploads. The GS load is about a quarter
of the reference title's.
