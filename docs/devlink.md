# devlink — the devkit's channel through the IOP

**Status:** phase 1 in progress (Remote Pad). Everything else in the devkit
still rides `host:` files; this page is both the plan and the record.

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
editor ──UDP 18198──▶ IOP: devlink thread ──SIF0 DMA──▶ EE RAM mailbox
                                                          ▲
                                         game reads it ───┘ (uncached, no RPC)
```

- **Inbound (editor → game).** The IOP thread blocks in `recvfrom`, checks
  the datagram, and DMAs it straight into a fixed mailbox in EE RAM whose
  address the game passed as the module's argument. The game reads the mailbox
  through the uncached segment: no RPC, no syscall, no cache maintenance, no
  waiting. Cost on the EE: a 48-byte uncached read per frame.
- **Outbound (game → editor)** — phase 2. The game fills a buffer and issues
  one SIF1 DMA every N frames; the IOP thread sends it as datagrams. The EE
  pays for the DMA setup only.
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

Measurements: pending.
