# devlink — the devkit's channel through the IOP

**Status:** phases 1 (Remote Pad) and 2 (Live Debugger) done and measured on
a PS2. Everything else in the devkit still rides
`host:` files; this page is both the plan and the record.

## Why

Every devkit channel today is a file on the `host:` filesystem
([devkit.md](devkit.md)). In PCSX2 that is a cheap host syscall. Over ps2link
each `fopen`/`fread`/`fwrite` is a SIF RPC to the IOP, a TCP exchange with
`ps2client` on the PC, and the reply back — and **the EE sits in `loop()` for
all of it**, ahead of `beginFrame()`. Measured on a physical PS2
(`examples/vehicle-playground`, 2026-09-23): 26.3 ms frame period and 15/50
missed fields with Remote Pad + Live Debugger on, against 20.4 ms and 0–1/50
with both compiled out. Remote Pad alone is 12.5 of those round trips per 50
frames (it reads `livepad.bin` every 4th frame).

The PS2 already has a second processor that owns the network: the IOP (on a
late Slim, the Deckard PowerPC emulating it — same modules, same API). The EE
only has to hand data over and carry on.

## Shape

`devlink.irx` is a small IOP module the **game** loads (debug builds, under
ps2link only). It is not part of ps2link: the ps2link on a console is flashed
to a memory card, and a channel that needed a reflash per change would never
be iterated on. It imports the `ps2ip` socket API that ps2link already has
resident, so it costs no extra network stack.

```
editor ──UDP 18198──▶ IOP: devlink thread ──SIF0 DMA──▶ EE RAM mailbox ◀── game reads (uncached)
editor ◀──UDP chunks── IOP: report slot ◀──SIF1 DMA── game hands a report over
```

- **Inbound (editor → game).** The IOP thread waits in `select`, checks
  the datagram, and DMAs it straight into a fixed mailbox in EE RAM whose
  address the game passed as the module's argument. **The first word routes
  it**: every devkit format starts with its own magic, so `TXPD` goes to the
  Remote Pad mailbox and `TXDC` (a whole `livedbg.cmd`, length-prefixed) to the
  Live Debugger's. The game reads mailboxes through the uncached segment: no
  RPC, no syscall, no cache maintenance, no waiting.
- **Outbound (game → editor).** At startup the IOP allocates two 32 KB report
  slots and DMAs their addresses into the game's *hello* box (whose address was
  the third module argument). The game copies a report into a staging buffer,
  writes it back from the cache and issues **one SIF1 DMA** into a free slot -
  header first, a footer echoing the sequence last. The IOP thread polls the
  slots every 4 ms, sends a complete one as numbered 1 KB `TXDO` chunks, and
  DMAs the acknowledged sequence back into the hello box, which is how the
  game knows the slot is free. Both slots busy = this report is skipped (the
  next one supersedes it); nothing ever waits.
- **Who the reports go to.** The editor that last sent a valid datagram, **to
  the port it sent from, from port 18198**. That is not just convenient: a
  stateful firewall (Windows Defender's included) lets the chunks in only
  because they are replies. So the editor sends and receives on one socket
  (`wire::sendDatagram` / `wire::recvDatagram`), and resends the current
  command every second to keep the conversation - and the firewall's state -
  alive.
- **Payloads keep their file formats.** The datagram for Remote Pad is
  byte-for-byte `livepad.bin` (36 bytes, `seq` + footer echo), so torn reads
  are rejected by the same check, and the editor's encoder is shared.
- **PCSX2 keeps `host:` files.** There they cost nothing, and nothing new needs
  setting up. The game picks the transport at boot: devlink when running under
  ps2link and the module loaded, files otherwise. The editor always writes the
  file and, when a console IP is configured, also sends the datagram.

### Rules the module keeps

- **Below audsrv.** The IOP schedules by strict priority and has already
  starved the audio once (TCP traffic from ps2link, docs/ps2link-setup.md). The
  devlink thread runs at a low priority and does one bounded thing per packet.
- **No new SIF RPC.** The SIF RPC path has crashed under heavy devkit traffic
  before (`rpc_packet_free`, docs/devkit.md). Setup goes through the module
  argument, data through plain SIF DMA.
- **One buffer in flight, waited out.** The IOP waits for its DMA to complete
  before it reuses the source buffer.
- **Latest state wins.** Inbound channels carry absolute state with a sequence
  number (all of them already do); a lost datagram is replaced by the next one.

## Phases

1. **Remote Pad over devlink** — the most frequent poll. Module, build scripts,
   game-side mailbox, editor UDP send. Verify in the ps2link emulator rig
   (PCSX2 + DEV9 bridged, docs/ps2link-setup.md), then on a console: frame
   period with Remote Pad on must match Remote Pad off, and audio must not
   regress.
2. **Live Debugger command + snapshot** — `livedbg.cmd` inbound (it is already
   full desired state + seq), `livedbg.bin` outbound via SIF1 + chunked UDP.
   The editor reads a socket instead of the file; parsers unchanged.
3. **Live Link / Live Logic / Time Machine / input recorder** — inbound ones
   follow phase 1, larger payloads get chunking with acknowledgements.
4. **Logs** — `TYRA_LOG` into the outbound ring instead of a synchronous tty
   RPC per line.
5. **Crash watchdog** — the IOP outlives a dead EE; it notices the outbound
   stream stopping and tells the editor, instead of the editor guessing from a
   file's age.

## Phase 1 record

Pieces:

| Piece | Where |
|---|---|
| IOP module source + build scripts | `vendor/tyra/devlink/` |
| Built module (committed) and its embedded copy | `vendor/tyra/devlink/bin/devlink.irx`, `vendor/tyra/engine/src/debug/devlink_irx.cpp` |
| EE loader + mailbox | `vendor/tyra/engine/inc/debug/devlink.hpp`, `src/debug/devlink.cpp` |
| Game side (Remote Pad reads the mailbox) | `src/templates.cpp` (live_pad runtime) |
| Editor side (datagram next to the file) | `src/livepad.*`, `src/devkit_ui.cpp`, `src/main.cpp` (`--pad`) |

### Measured on a PS2 (2026-11-07)

Physical PAL PS2 over ps2link (r8), a fresh FPP scratch project with the
debug profile, **Remote Pad the only devkit channel on** (Live Link, Live
Debugger, Live Logic, Time Machine off), `TYRA_FRAME_PROFILE 2`. Two ELFs that
differ only in whether `DevLink::start()` may load the module; 25 windows of
50 frames each.

| | `pre` mean | `pre` max | period | missed |
|---|---:|---:|---:|---:|
| devlink (mailbox read every frame) | **0.16 ms** | 0.21 ms | 20.00 ms | 0 |
| `host:` file, polled every 4th frame | **0.82 ms** | 1.17 ms | 20.00 ms | 0 |

`pre` is everything in `loop()` ahead of `beginFrame()`, so the difference is
the poll: about 0.66 ms per frame on average, which is **about 2.6 ms per
`host:` round trip**, paid every 4th frame. This scene had 18 ms of slack, so
the period did not move; in a scene within ~2.6 ms of its 20 ms field, every
poll frame waits a second field. That is the 15/50 misses from the
vehicle-playground measurement above.

What the log showed, in order:
`devlink: up on udp/18198`, then the IOP's own
`devlink: listening on udp/18198 (pad mailbox 0x0044cbc0)`, and when the PC
sent datagrams `devlink: first Remote Pad state arrived (seq ...)`. That last
line is written once per boot from `DevLink::readPad`, so a run can tell the
transport works without seeing the screen. What the overlay then does with the
state is the same code as the file path (`applyState`), verified in PCSX2 by a
right-stick turn through `livepad.bin`.

Not yet covered: an audio check with music playing (the IOP thread runs at
100, below audsrv, and only wakes per datagram), and the Remote Pad panel
driving a console end to end (the datagrams above came from a test script with
the same bytes `livepad::write` sends).

## Phase 2 record - the Live Debugger

| Piece | Where |
|---|---|
| IOP: channel routing, command mailbox, report slots, `TXDO` sender | `vendor/tyra/devlink/iop/devlink.c` (4.5 KB module) |
| EE: `DevLink::readCommand`, `DevLink::sendReport` | `vendor/tyra/engine/src/debug/devlink.cpp` |
| Game: command from the mailbox, report to the IOP, PCSX2 cadence | `src/templates.cpp` (live_debug runtime) |
| Editor: command datagram + 1 s keepalive, chunk receiver -> `bin/livedbg.bin` | `src/devkit_ui.cpp` (`livedbgTick`), `src/devlink_host.*`, `src/wire.*` |
| Reassembly harness | `tools/verify-devlink-reassembler.cpp` |

How the pieces behave:

- **The editor side writes `bin/livedbg.bin`** from each reassembled report,
  atomically, so every reader of that file - the parsers, the Stats page, the
  stale-snapshot and hang detection in [live-debugger.md](live-debugger.md) -
  is unchanged. A dead console still shows as a file that stopped changing.
- **The boot snapshot still goes to the file.** Before the editor attaches
  there is nobody to send datagrams to, and the Runner reads that file as the
  game-is-up marker. It is one `host:` write per boot.
- **Cadence**: with devlink up, the game polls the command and reports at the
  PCSX2 cadence (every 6 frames) instead of every 25 - a poll is a memory read
  and a report one DMA.
- **Not moved yet** (still `host:`, all one-shot on request): the VU1 capture,
  the self-screenshot (`frame.tga`), the render-cost capture and the crash
  report. They cost a round trip when asked for, not every N frames.
- **Size**: a report bigger than a slot (32 KB) falls back to the file. The
  largest livedbg.bin the format allows is about 22 KB plus 12 bytes per watch
  variable.

The harness (`g++ -std=c++20 -Isrc tools/verify-devlink-reassembler.cpp
src/devlink_host.cpp`) feeds chunks in order, shuffled, with one lost, twice,
and across a game reboot (sequence back to 1).

### Measured on a PS2 (2026-11-07)

Same fixture as phase 1, now with the Live Debugger on and **attached** (a
command sent, so the game reports periodically), Remote Pad on too,
`TYRA_FRAME_PROFILE 2`, 25 windows of 50 frames after attaching:

| | `pre` mean | `pre` max | period | missed |
|---|---:|---:|---:|---:|
| devlink (report every 6 frames) | **0.17 ms** | 0.21 ms | 20.00 ms | **0** |
| `host:` files (debugger every 25, pad every 4) | **1.67 ms** | 2.18 ms | 20.32 ms | **20 of 1250** |

The file arm missed frames **in a scene with 18 ms of slack**: a poll frame
that stacks a debugger round trip on a pad round trip, plus the snapshot write,
runs past a field on its own. With devlink the debugger reports four times as
often and the game does not notice.

Over UDP the attached game sent 125 reports in 15 s - 8.3 a second, one every
6 frames at 50 fps - all complete, all valid `TXDB` with a rising frame
counter. The log showed `devlink: first Live Debugger command arrived`.
The datagrams came from a test script that sends what `livedbgTick` sends;
the editor panel driving a console end to end is still owed.

### Testing devlink without the console

`tools/ps2rig/devlink-emu-rig.ps1` boots ps2link in a portable PCSX2 with DEV9
bridged onto the LAN. **It shares one trap with every console run: each
`ps2client` on the PC listens on the same UDP tty port**, so a rig run and a
real-PS2 run at the same time steal each other's log. Run one at a time.
