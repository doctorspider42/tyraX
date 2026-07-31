# Running and debugging on a real PS2 — the TyraX ps2link

The console side of this editor is **always our own ps2link**: a pinned upstream
checkout plus [`tools/ps2link/tyrax.patch`](../tools/ps2link/tyrax.patch), built
here in Docker and flashed onto the memory card once. Stock ps2link is not a
supported target — ours adds the USB HID stack, fixes the hangs that made a
session need a console Reset, and silences the SPU2 on reset — and every "how
does the console behave" answer in these docs assumes the TyraX build.

Consequently **nothing downloads a ps2link for you**: `setup.ps1` / `setup.sh`
fetch [ps2client](../tools/ps2client/README.md) (the PC side) and stop there.
You build the console side, which takes one command and about a minute.

## What you need

| | |
|---|---|
| A PS2 with ethernet | SCPH-700xx and later have it built in; a fat PS2 needs the official Network Adaptor in the expansion bay. |
| A way to boot homebrew | FreeMcBoot / FreeDVDBoot / uLaunchELF / a disc-swap exploit — whatever already runs `.ELF` files off your memory card. |
| A memory card | It holds `PS2LINK.ELF` + `IPCONFIG.DAT`. |
| A wired LAN | Console and PC on the **same subnet**; the PS2 has no Wi-Fi and ps2link has no DHCP. |
| Docker Desktop on the PC (plain `docker` on Linux) | The ps2link build runs inside the `ps2dev/ps2dev` toolchain image. |

## 1. Build the TyraX ps2link

```powershell
tools/ps2link/build.ps1
```

On Linux: `tools/ps2link/build.sh` (the same script, same pins). Add `-Clean` /
`--clean` to throw the work tree away first.

It clones ps2link pinned at `0c6138c`, applies `tyrax.patch`, runs `make ee`
inside `ps2dev/ps2dev:latest` and drops **`tools/ps2link/ps2link.elf`** (~280 KB)
next to the script. The first run pulls the toolchain image; later runs reuse the
clone in `tools/ps2link/build/`. The ELF is gitignored — the patch is the source
of truth, not the binary.

What the patch does to upstream today, in two groups.

**The USB HID stack** — what the patch was originally for:

| File | Change |
|---|---|
| `ee/Makefile` | bakes `usbd.irx`, `ps2kbd.irx`, `ps2mouse.irx` (prebuilt in `$PS2SDK/iop/irx/`) into the ELF |
| `ee/irx_variables.h` | externs the three embedded buffers |
| `ee/ps2link.c` | loads them in `loadModules()` — **`usbd` first**, the other two import its symbols — brands the welcome screen, and guards `PS2_DISABLE_AUTOSTART_PTHREAD` so newer ps2sdk versions still compile |

The USB stack has to come up on ps2link's **own freshly reset IOP**: a game
deployed over the network runs on that same IOP without resetting it, so it
cannot bring the stack up itself (the full story is in
[keyboard-mouse.md](keyboard-mouse.md)). Baking it into ps2link is what makes a
keyboard and mouse work over an F6 deploy at all.

**Hang and leak fixes (r2)** — upstream ps2link is 20-year-old debug-loader code
that assumes nothing ever goes wrong on the wire. Every one of these turned a
recoverable hiccup into "the console is dead, hit Reset":

| Where | What was wrong |
|---|---|
| `iop/net_fio.c` `pko_recv_bytes()` | `recv()` returning **0** — an orderly close, i.e. what happens every time `ps2client` exits or the editor is closed mid-session — did `left -= 0` and spun forever at IOP priority 9 **holding the `host:` semaphore**. Every later file access then blocked for good. This is the big one. |
| `iop/net_fio.c` `pko_accept_pkt()` | an unexpected or over-long reply was abandoned **without consuming its body**, so those bytes were read as the next reply's header and the stream stayed permanently out of frame. Now drained; a `len` below the header size drops the connection instead of computing a huge unsigned body length. |
| `iop/net_fio.c` `pko_read_file()` | the PC's byte count went straight into the receive loop with no check against the caller's buffer — a desynced stream wrote past it, corrupting IOP memory far from the cause. Now clamped. |
| `iop/net_fio.c` `pko_file_serv()` | a failing `accept()` busy-looped (lwip out of TCP PCBs after a burst of deploys is enough); the `socket`/`bind`/`listen` error paths returned **without `ExitDeleteThread()`**, leaking the thread and its stack. |
| `iop/cmdHandler.c` | same busy-loop on a failing `recvfrom()`; `execiop` with `argc == 0` wrapped an unsigned bound and walked `strlen()` across IOP RAM. |
| `iop/excepHandler.c` | `strlen(NULL)` whenever the faulting PC was outside every loaded module — i.e. the commonest crash of all — so the IOP died *reporting* a crash and told you nothing. The frame `memcpy` also over-read 136 bytes and only fit because an operator-precedence slip over-allocated the buffer 2.7×. |
| `ee/cmdHandler.c` `pkoExecEE()` | a failed thread start zeroed **`cmdThreadID`** — the wrong variable, and the one the SIF DMA handler wakes. After that ps2link answered no command at all. |
| `ee/cmdHandler.c` `pkoReset()` | ~~`while (SifIopSync());` — **inverted**~~. **This one was wrong and r4 undid it.** `SifIopSync()` returns a single bit (BOOTEND), so upstream's loop waits for the reset to be *taken* while `restartIOP()`'s waits for the boot to *finish* — two phases, not a typo. "Correcting" one into the other left the loop waiting for a bit that was still set from the previous boot, i.e. waiting for nothing. r4 does both waits, in order. |
| `ee/cmdHandler.c` | `dumpmem`/`writemem`/`dumpreg` returned on failure without `close()`, leaking a `host:` fd on both sides; `writemem` copied the requested length rather than what `read()` delivered; `gsexec` DMA'd a packet-supplied qword count out of a buffer it had only filled 128 bytes of. |
| `ee/cmdHandler.c` `pkoDumpReg()` | `unsigned int regs[REGALL_SIZE]` used a byte count as an element count. Do **not** "fix" that with `/4`: `REGALL_SIZE` is 508 bytes while the `REGVU0`/`REGVU1` asm stores 896 into the same buffer, so the 4× over-allocation was load-bearing — it is sized for the real worst case instead, and marked `aligned(16)`, because `sqc2`/`sq` reach it and the R5900 masks the low 4 bits of an `lq`/`sq` address rather than faulting. |
| `ee/excepHandler.c` | the exception-name table has 14 entries and was indexed with a 0..31 code, so half the codes printed whatever followed it — a second fault while reporting the first. |

**Silencing the SPU2 on reset (r3)** — the SPU2 is a separate block from the IOP
and it **keeps its register state across an IOP reset**, so it carried on looping
whatever voices the dead game had keyed and mixing whatever autodma had left in
its input. That is the "sound goes haywire when ps2link restarts": it affected
every redeploy, not just *Stop on PS2*.

`spu2Silence()` in `iop/cmdHandler.c` now keys off all 24 voices on both cores,
zeroes their volumes, unroutes them from the dry and reverb mixes, clears `MMIX`
(the autodma input — the half a voices-only fix would miss), clears `CORE_ATTR`
and zeroes the output volume block. It runs **on both sides of the reset**: from
`pkoReset()` while ps2link still owns the IOP, and again from ps2link.irx's
`_start()` when it comes back up (which also covers an IOP reset that never went
through `pkoReset()` at all — a game resetting it, or a crash). The boot log line
`SPU2 silenced` is how you confirm on hardware that it fired.

Two things make this safe rather than a way to end up with no audio at all:
every write is a zero or a key-off, so it cannot *set* a bit that was not already
set (no chance of tripping an undocumented reset line); and every register it
touches is one that `libsd`'s init — what `audsrv_init()` ends up calling —
reprograms anyway, so the next game restores the lot. That second claim is
checked, not assumed:

```bash
docker run --rm ps2dev/ps2dev:latest sh -c 'mipsel-none-elf-objdump -d $PS2SDK/iop/irx/libsd.irx | grep -oE "sh[[:space:]]+[a-z0-9]+,[0-9]+\("'
```

against the same extraction for `spu2Silence` in `build/iop/obj/cmdHandler.o` —
every address the routine writes is in `libsd`'s set. Redo it if you change the
routine.

Because ps2link handles it, **"Stop on PS2" no longer deploys `silencer.elf`**
and no longer sleeps ~7 s waiting for it; Stop is now just the reset.
[`tools/silencer`](../tools/silencer/) stays in the tree as a manual fallback for
a console still running a pre-r3 ps2link.

**Stopping a running game (r4)** — *Stop on PS2* never actually worked against a
game on hardware, and it failed in two independent ways. Both are fixed, both
were measured on a real console.

*The command did not arrive.* ps2link's IOP-side command listener ran at
priority 60 while the `host:` file server ran at 9, and IOP scheduling is
strictly priority-based. A game of ours polls `host:` every frame (Remote Pad,
Live Link, the time machine, the live texture channel — around ten opens per
frame), which keeps the file server permanently runnable, and the listener
never got a slot: three resets over 20 s went unanswered, while the same
command against an idle ps2link was answered instantly. The listener now takes
`USER_HIGHEST_PRIORITY` (9) and the file server sits one below it. Note 9 is
the highest a user thread may have — 8 makes `CreateThread` fail, which ps2link
used to report only through `dbgprintf`, i.e. not at all in a release build.
That now goes through `printf`.

*ps2link did not come back.* Killing the game's thread stops the CPU, not the
hardware: the DMAC, both VUs, VIF0/1, the GIF and the IPU were left mid-chain,
and the game's `libeedebug` crash handler still owned the EE exception vectors.
`pkoReset()` now quiesces the peripherals (`ResetEE`), re-arms the SIF0 chain
that goes down with them, and takes the vectors back before restarting.

The restart itself hid the subtlest bug of the lot. `SifIopSync()` returns a
single bit — BOOTEND — so `while (SifIopSync())` waits for the reset to be
*taken* and `while (!SifIopSync())` waits for the boot to *finish*: different
phases, not a typo, and r2 "corrected" the first into the second. Upstream got
away with it because `ExecPS2()` followed and the fresh image's `SifInitRpc()`
does its own waiting. Both waits now run, in order. And the restart stays an
`ExecPS2()`: three attempts at re-arming ps2link in place each survived exactly
one Stop and wedged on the second, because ps2sdk keeps its "SIF is
initialized" flags in `.bss` and only a re-executed image gets them cleared by
crt0.

Verified on the console: four Run → Stop cycles back to back with no power
cycle, the game dying on every Stop and the next deploy booting into a clean
IOP.

None of this changes the protocol, so it needs no `ps2client` change and an old
`ps2client` still talks to it.

## 2. Put it on the console

1. Copy `ps2link.elf` onto the memory card as **`PS2LINK.ELF`** (uLaunchELF over
   USB or the network, a PS2 memory-card manager on the PC, whatever your
   FMCB setup already uses to install homebrew).

   > **Two link addresses, and the default here is not upstream's.** ps2link can
   > link at `0x00094000` — the "BIOS unused" window *below* the `0x00100000` a
   > game loads at, which is what keeps it alive underneath the running game —
   > or at `0x01ee8000`, the top of RAM. Upstream defaults to the low one and
   > publishes both ("default" and "highloading"); **we default to the high
   > one**, because the low build **black-screens when booted from FreeMcBoot's
   > menu or a shortcut** (uLaunchELF boots it fine), and a console that shows
   > nothing and answers nothing on the network is a worse failure than a
   > memory ceiling no scene has come near. `build.ps1` / `build.sh` therefore
   > write the high build as `ps2link.elf`; `-Low` / `--low` writes the low one
   > as `ps2link-low.elf`. The boot screen prints `ps2link loaded at 0x...`, so
   > a flashed card always says which one it is.
   >
   > Why the low build breaks there is **unresolved, and the obvious answer has
   > been ruled out**. The theory was that our USB HID stack pushes the image
   > ~50 KB further into that window than stock ps2link (`0x000eb5a0` against
   > `0x000dea20`) and so onto whatever FMCB keeps resident. But a `--no-usb`
   > build, which ends at `0x000df3a0` — 2.4 KB from upstream — black-screens
   > exactly the same. So it is either something else in the patch, or the low
   > address does not work with that FMCB at all, stock ps2link included. The
   > high build boots from everything tried, so nothing is blocked on the
   > answer.
2. Put an **`IPCONFIG.DAT`** in the *same directory* — ps2link opens it by a
   relative path, so it reads the one next to itself. One line, three
   space-separated fields, `ip netmask gateway`:

   ```
   192.168.1.42 255.255.255.0 192.168.1.1
   ```

   Give the console a **static address outside your router's DHCP pool**;
   the editor stores that IP and the deploy is UDP, so a changing address just
   looks like a dead console.
3. Boot it. The screen must read:

   ```
   Welcome to TyraX ps2link r4 (USB keyboard + mouse)
   based on ps2link
   ...
   Net config: 192.168.1.42  255.255.255.0  192.168.1.1
   ```

   Plain **"Welcome to ps2link"** means you booted a stock build — nothing below
   is supported on it. **No `r<n>`** (just "Welcome to TyraX ps2link (USB
   keyboard + mouse)") means an r1 build: it has the keyboard and mouse but none
   of the hang fixes below, so reflash it. The number is bumped whenever
   `tyrax.patch` changes console behaviour, precisely so a flashed card can be
   identified without guessing. And if `Net config:` reads **192.168.1.10 /
   255.255.255.0 / 192.168.1.0**, those are the values compiled into ps2link as
   a fallback: your `IPCONFIG.DAT` was not found or not readable, and it is
   *not* using the address you configured.

## 3. Point the editor at it

*Edit > Preferences > Real PS2 (network deploy) > **PS2 (ps2link) IP***. This is
a **machine-global** editor setting (`editor.ini`), shared by every project —
not project data, so it never travels in a `.tyra` file. Leave it empty to
disable the F6 path entirely.

Headless, the IP can also come from the command line:

```powershell
tyrax-editor --build <projectDir> --run-ps2 192.168.1.42
```

## 4. Deploy and run

**F6** = build && run on PS2, **Ctrl+F6** = run without building. The run
toolbar's target dropdown has the same thing as *PlayStation 2 (ps2link)* — the
run triangles turn **blue** when the console is the target and green for PCSX2.

What the editor does, in order:

1. kills any previous `ps2client` (one file server at a time);
2. clears the stale devkit channel files in `bin/` (`log.txt`, `livedbg.bin`,
   `livetime.bin`…) so the panels do not read yesterday's session;
3. writes the **`bin/ps2link.run`** marker — the game probes it over `host:` to
   learn it was network-deployed and must keep the IOP resident (ps2link's
   `execee` does not deliver argv reliably, so the `-ps2link` argument alone
   cannot be trusted; a PCSX2 launch deletes the marker);
4. `ps2client -h <ip> -t 10 reset`, waits ~3 s for ps2link to reload;
5. `ps2client -h <ip> execee host:<name>.elf -ps2link`, with the working
   directory set to the project's `bin/` — that is how the game's `host:` maps
   onto `bin/` exactly like a PCSX2 run.

That last `ps2client` **is the file server for the whole session**: the ELF,
every texture, every model and every devkit channel file is read from your PC
over the network while the game runs. Its output — including the console's
`printf`/`TYRA_LOG` — is pumped into the *Output* panel as `[ps2]` lines.

> **Keep the editor open while the game runs.** Closing it kills `ps2client`,
> and the game loses `host:` mid-session: the devkit files freeze exactly where
> they were while the console happily keeps running. The cure is a redeploy, not
> a retry.

Ports, if a firewall is in the way:

| Port | Direction | What |
|---|---|---|
| TCP 18193 | PC → console | the file-request socket ps2link listens on; `ps2client` connects to it |
| UDP 18194 | PC → console | commands (`reset`, `execee`) — fire-and-forget, no ack |
| UDP 18194 | console → PC | the console's `printf` output (udptty) — this is what the `[ps2]` lines are |

Because the commands are fire-and-forget, a dead or wrong IP makes `reset` and
`execee` both "succeed". The editor therefore treats **the first log line** as
the liveness signal and gives up after 15 s.

## 5. Debug it

Everything in the [devkit](devkit.md) rides the same `host:` filesystem channel
on hardware as it does in PCSX2, so it all just works over ps2link:
[Live Link](live-link.md), the [Live Debugger](live-debugger.md) (breakpoints on
flow-graph nodes, pause/step/step-node, watches, the timeline),
[Live Logic](live-logic.md) and the [time machine](time-machine.md). Build in
the **debug** profile — release builds carry none of it.

Differences on real hardware:

- **Slower polling.** The game re-reads the channels every 25 frames instead of
  6 (~0.5 s): each file operation is a network round-trip, and hammering the
  ps2link file server starves the game.
- **`ps2client` must be patched.** The editor ships one
  ([`tools/ps2client`](../tools/ps2client/README.md)) with `TCP_NODELAY` on the
  request socket. Without it, Nagle plus the PS2's delayed ACKs stall every
  exchange ~200 ms — measured ~4 KB/s, a 10-minute game boot.
- **No process to query.** `--debug-state` finds a PCSX2 session by reading the
  emulator's command line; on hardware there is none, which is why the editor's
  session pointer records the transport (`ps2link`) instead.
- **Keyboard and mouse work**, because this is the TyraX ps2link: tick
  *Project > Preferences > Build > Keyboard & mouse controls* and the
  *Also over ps2link* sub-option under it is already on — the game reuses
  ps2link's resident USB stack instead of loading one it cannot load. See
  [keyboard-mouse.md](keyboard-mouse.md).
- **The EE crash handler** ([devkit.md](devkit.md#the-ee-crash-handler-experimental-opt-in))
  is still opt-in and unproven on hardware; the heartbeat post-mortem needs
  nothing from the game and works here.

**Stop on PS2** kills the file server and sends `reset` — the file server
first, both because the game polling `host:` every frame is what the command
has to cut through and because the port is then free for the `ps2client listen`
the editor runs as a witness. The reset is fire-and-forget UDP, so the editor
watches that listener for the console's own log and repeats the command up to
three times; if nothing ever comes back it says the console is hung instead of
reporting success it cannot know. ps2link r3 silences the SPU2 itself (see
above), so the old dance — execee `host:silencer.elf`, sleep ~7 s while its
`audsrv_init()` keyed the voices off, kill its file server — is gone, along
with the 7 s. A deploy waits ~10 s after the reset: ps2link reboots the IOP,
restarts its own image and reloads every IRX before it listens again, and
landing a game in the middle of that gets a frozen Tyra logo waiting on a pad
whose driver has not finished its handshake.

## When it does not work

| What you see | What it means |
|---|---|
| `[editor] Could not reach ps2link at <ip>` | `ps2client reset` failed outright — wrong IP, console not booted into ps2link, cable/link down. |
| `[editor] No response from <ip> within 15s` | The commands went out and nothing came back. Check the IP against the console's `Net config:` line, then the PC firewall (inbound **UDP 18194** for `ps2client` — without it the game may actually be running with its log going nowhere). |
| Boot screen says "Welcome to ps2link" | Stock ps2link. Rebuild from `tools/ps2link/` and reflash. |
| Boot banner is below `r4` | r1 = keyboard/mouse only; r2 = plus the hang fixes; r3 = plus the SPU2 silencing, but Stop still wedges the console. Reflash. |
| Stop leaves the console frozen on the game, or on a black screen | Fixed in r4. On a pre-r4 build the reset either never reached the console or killed the game and took ps2link down with it; only a power cycle recovered. |
| The game boots to a frozen Tyra logo, last log line `Curent pad(0,0) status: DISCONNECT` | The pad had not settled yet and the engine waited for it forever. Fixed in the engine (`vendor/tyra`, `Pad::waitPadReady` is bounded now), so rebuild the game. A deploy also waits ~10 s after the reset for exactly this reason. |
| Stop works once and the next one wedges | Also r4 - and if you see it ON r4, it is a new bug: say so, that pattern was ps2sdk's `.bss` state and the restart no longer depends on it. |
| Console wedges after the editor is closed, or after a `ps2client` dies | Fixed in r2 (`pko_recv_bytes()` spun forever on a closed peer while holding the `host:` lock). If it still happens on r2, that is a new bug — say so, it is not the old one. |
| `host:` reads start returning wrong/garbage data | Fixed in r2 (an unexpected reply used to desync the byte stream permanently). |
| A game crash shows nothing at all on screen | Fixed in r2 (the IOP exception handler faulted on `strlen(NULL)` before it could report). |
| Sound keeps droning / goes haywire after a reset or redeploy | Fixed in r3. Check the boot log for `SPU2 silenced`; if it is absent you are on a pre-r3 build. As a stopgap on an old build, `ps2client -h <ip> execee host:silencer.elf` from `tools/silencer/`. |
| No sound at all in a game, on r3 only | Would mean the silence is sticking when it should not — `libsd`'s init is supposed to reprogram every register it clears. Report it with the boot log; the fallback is to flash r2 while it is diagnosed. |
| `Net config:` shows `192.168.1.10` | `IPCONFIG.DAT` was not found next to `PS2LINK.ELF`; ps2link fell back to its compiled-in default. |
| Assets crawl in, boot takes minutes | An unpatched `ps2client` on `PATH` is being used instead of `tools/ps2client/bin`. |
| `KbdMouse: mouse skipped (no resident USB stack…)` | The running ps2link has no USB stack — a stock one. |
| Both drivers "ready" but nothing responds | The devices. `ps2kbd`/`ps2mouse` only speak the USB HID **boot protocol**; test them in uLaunchELF first. |
| Devkit panels frozen, game still running | The editor (and with it `ps2client`) was closed. Redeploy. |

## Changing the patch

We expect to keep changing our ps2link, so the loop is:

1. Run a build once — `tools/ps2link/build/` is then a full ps2link checkout at
   the pinned commit with `tyrax.patch` applied.
2. Edit the sources there.
3. **Regenerate the patch before rebuilding**, because both build scripts start
   with `git checkout -- .` and would throw your edits away:

   ```powershell
   git -C tools/ps2link/build diff --output=../tyrax.patch
   ```

   `--output` rather than `>` on purpose: git writes the file itself, so the
   bytes stay LF and UTF-8. Windows PowerShell's `>` would hand you a UTF-16
   file that `git apply` refuses. (The path is relative to the `-C` directory,
   hence the `../`.)

4. Rebuild (`tools/ps2link/build.ps1`, or `build.sh` on Linux) and reflash
   `PS2LINK.ELF`.

### Testing a change without a console

PCSX2 runs no ps2link, so the console used to be the only test rig. For the
`host:` protocol code there is now a host harness — it compiles the **real**
`iop/net_fio.c` out of the work tree against stub IOP/lwip headers and drives it
with a scripted fake socket:

```bash
tools/ps2link/test/run.sh
```

`run.ps1` on Windows. `--pristine` / `-Pristine` points the same tests at the
untouched upstream file and **expects them to fail** — that A/B is the evidence,
so keep both halves passing. It covers framing, EOF, short reads and the
buffer clamps; anything involving threads, the SIF or the GS still needs
hardware.

### Known remaining rough edges

Not fixed in r3, recorded so nobody re-discovers them:

- **`pkoSendSifCmd()` reuses one `rpc_data` buffer with no wait for the previous
  SIF DMA to land.** Two commands close together (or a crash report racing a
  queued command) can clobber each other. The fix is a bounded `sceSifDmaStat()`
  spin, but it would have to run inside the IOP exception handler, where the
  ps2sdk header explicitly warns you are in a bad state — not worth doing blind,
  without hardware to test it on.
- **Throughput is protocol-bound, not code-bound.** Writes go out in
  ≤1446-byte chunks and each one waits for its own `WRITE_RLY`, so `host:` write
  bandwidth is one round-trip per 1.4 KB. Raising that means changing the packet
  size on **both** sides (ps2link *and* `ps2client`), which is a protocol break;
  `TCP_NODELAY` (already on at both ends) was the cheap 50× win and it is spent.
  Reads are already single-round-trip and unchunked.

Keep the patch **LF-only** (`.gitattributes` enforces it) — it is applied to a
Unix checkout inside a Linux container, and a CRLF patch fails `git apply`. To
move to a newer upstream, bump the pinned commit in **both** `build.ps1` and
`build.sh` and re-generate the patch against the new tree. And if a change
alters what the console reports on boot, update the expected banner in step 2 above — that
banner is how anyone tells the two builds apart.
