# Running and debugging on a real PS2 — the TyraX ps2link

![Debug channels used with ps2link](img/project-preferences-build.png)

The console side of this editor is **always our own ps2link**: a pinned upstream
checkout plus [`tools/ps2link/tyrax.patch`](../tools/ps2link/tyrax.patch), built
here in Docker and flashed onto the memory card once. Stock ps2link is not a
supported target — ours adds the USB HID stack, fixes the hangs that made a
session need a console Reset, and silences the SPU2 on reset — and every "how
does the console behave" answer in these docs assumes the TyraX build.

Consequently **nothing downloads a ps2link for you**: `setup.ps1` / `setup.sh`
fetch [ps2client](../tools/ps2client/README.md) (the PC side) and stop there.
You build the console side, which takes one command and about a minute.

## Quickstart

The rest of this page is long because it carries the evidence for every choice
in it. If you just want a console running, this is the whole of it — five steps,
once.

```powershell
tools/ps2link/build.ps1 -Low -NoUsb -Packed
```

1. **Build** — the command above writes `tools/ps2link/ps2link-low-nousb-packed.elf`.
   *This is the build to flash*, and the flags are not optional: `-Packed` is
   what lets FreeMcBoot's menu boot it at all, `-Low` keeps it out of the top of
   RAM where a big scene would overwrite it, and `-NoUsb` keeps it small enough
   to fit under the menu's limit. Linux: `tools/ps2link/build.sh --low --no-usb --packed`.
2. **Copy two files to the memory card**, in the same folder: that ELF renamed
   to `PS2LINK.ELF`, and an `IPCONFIG.DAT` containing one line of three
   space-separated values — IP, netmask, gateway, e.g.
   `192.168.1.42 255.255.255.0 192.168.1.1`. Pick an IP on your PC's subnet.
   **No `IPCONFIG.DAT` and the console silently falls back to `192.168.1.10`**
   and vanishes from the LAN.
3. **Boot it** from your launcher. The screen must read
   `Welcome to TyraX ps2link r6 (no USB)` and end with a `Net config:` line
   showing the address you chose. A lower `r<n>`, or plain "Welcome to ps2link",
   means the wrong build — see the [troubleshooting table](#when-it-does-not-work).
4. **Point the editor at it** — *Edit > Preferences > Real PS2 (network deploy)
   > PS2 (ps2link) IP*. Machine-global, not project data, so you do this once.
5. **Run with F6**, stop with the toolbar's Stop. That is it.

Two things worth knowing before you hit a wall:

- **This build has no keyboard or mouse support** — that is what `-NoUsb` costs,
  and it buys the FreeMcBoot boot. Drop `-NoUsb` if you want them and boot via
  uLaunchELF instead, which has no such size limit.
- **The PC firewall must let UDP 18194 in**, or the game runs with its log going
  nowhere and the editor reports no response.

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

It clones ps2link pinned at `0c6138c`, applies `tyrax.patch`, builds inside
`ps2dev/ps2dev:latest` and drops **`tools/ps2link/ps2link.elf`** (~120 KB) next
to the script. Like upstream's releases it is run through `ps2-packer`, which
is what lets a launcher boot it: an unpacked ELF has its segment at the final
address and FreeMcBoot's menu keeps its own loader there. `-Unpacked` /
`--unpacked` builds the raw image instead, for debugging - it only boots from
uLaunchELF. The first run pulls the toolchain image; later runs reuse the
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
That now goes through `scr_printf` — r4 sent it to `printf`, which turned out to
be a way of killing the console rather than reporting to it (see r5).

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

The game dies on every Stop, measured and reproducible. Whether **ps2link**
comes back is separate: this section used to claim four clean Run → Stop cycles,
which later re-measurement could not reproduce on any build.

None of this changes the protocol, so it needs no `ps2client` change and an old
`ps2client` still talks to it.

**No stdio anywhere in `cmdHandler.c` (r5)** — every remaining `printf()` in
that file became `scr_printf()`. They all sat on *error* paths, which is exactly
why the r4 rule ("no stdio in `pkoReset`") did not catch them, and why the bug
looked address-dependent and intermittent: an error path only runs when
something has already gone wrong.

`printf()` takes newlib's stdout lock. After `ExecPS2()` re-enters the image
that lock is a cleared `.bss` field until newlib re-initializes, so the call
faults instead of printing. Measured on the console: **`BadAddr 0x00000008`,
`EPC 0x00098604`, `Cause 0x70008008`** (ExcCode 2, TLB miss on load). `0x98604`
resolves inside `__retarget_lock_acquire_recursive`, whose prologue is

```
jal   <get thread id>
move  s0,a0          ; s0 = the lock pointer
lw    v1,8(s0)       ; <-- EPC, faults when s0 == NULL
```

so the lock argument was NULL. A disassembly sweep of the whole image found
exactly eight `printf`/`puts` call sites, all of them error reports, four of
them on the restart and deploy paths — including the two `initCmdRpc()` thread
failures, which run on *every* ps2link start. The r5 image has zero: `puts` and
`_puts_r` are no longer even linked in.

What this fixes is the **messenger**: a failure that used to take the EE down
silently now survives onto the screen and names itself. On its own it did not
fix Stop — the console still failed to come back, as a black screen with no
exception rather than a crash. That was r6's job.

**One IOP reboot per Stop, not two (r6)** — and this is the one that makes Stop
survivable. `pkoReset()` rebooted the IOP and *then* called `ExecPS2()`, while
the re-executed image's `main()` calls `restartIOP()` a few lines in and reboots
it again. The second reboot is the one that has to happen; the first one is what
broke the console.

What runs between them is crt0. ps2sdk's C runtime init — `_libcglue_init`,
`__fdman_init`, the pthread glue — runs *before* `main()` and talks to the IOP
over the SIF. Reboot the IOP and hand straight over to `ExecPS2()`, and crt0
comes up against an IOP that has just restarted with no modules on it and
nobody to answer an RPC. Measured with a GS-background probe at the top of
`main()`: the restarted image **never reached `main()`**, which is why the
screen was black, the network never came back, and no exception was ever
raised.

Leaving the reboot to `main()` means crt0 runs against the IOP the game left
behind — loaded, answering — and the reboot then happens with the C runtime
already up. `restartIOP()` gained the second `SifIopSync()` edge at the same
time: it waited only for the boot to *finish*, which falls straight through when
the BOOTEND bit is still set from the previous boot. That was harmless while
`pkoReset()` did its own complete reboot; it is now the only IOP reboot in the
program.

Measured on the console, `drone` fully running before every Stop and every
"survived" confirmed by the *next deploy* rather than by ping:

| build | Run → Stop cycles |
|---|---|
| r4 | 1–2, then a power cycle |
| r5 (stdio off the error paths) | 1 |
| r6, high, with the diagnostic probe | 8/8, series ended on its limit |
| r6, high, `pkoReset()` fixed | 12/12, series ended on its limit |
| r6, high, both reboot sites fixed | 12/12, series ended on its limit |
| **r6, low packed no-USB, booted from the FMCB menu** | **12/12, series ended on its limit** |

The last row is the one that matters: that build, in that launcher, is what a
user actually flashes, and before r6 it died on the **first** Stop every single
time.

`pkoRestartImage()` had the same reboot-then-`ExecPS2()` shape and r6 changes it
the same way. That path only runs on the initial boot, where nothing has touched
the hardware yet, which is why a memory-card boot always survived it. Booting the
**high** build over a running low one comes up cleanly and gives the 12-cycle
result above.

**Booting the low build over a running high one still does not work** — the
console answers ping afterwards but accepts no deploy, i.e. the EE never comes
up. r6 does not fix that, measured; it was tried again with both reboot sites
changed. It is a limitation of the iteration recipe, not of the build: **the
high image can be shot over a low one, but not the other way round.** The low
build is measured from a card boot instead, which is how the last table row was
taken.

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
   > **It is packed vs unpacked.** Three low builds black-screen from the FMCB
   > menu — ours (285 KB), ours without the USB stack (236 KB) and untouched
   > upstream (233 KB) — so it is neither the size nor the patch. Nor is it the
   > address: upstream's *release* `PS2LINK.ELF` has been booting from that
   > menu all along and ends up at `0x00094000` too. The difference is in the
   > ELF headers: the release (and uLaunchELF) enters at `0x01d0001c` with its
   > only segment high in memory, because it went through **`ps2-packer`** —
   > the launcher loads a stub, which decompresses the real image down to
   > `0x00094000` once the loader is out of the way. `make ee` produces the raw
   > image, whose segment lands on FMCB's loader mid-copy. Building with `make`
   > instead of `make ee` gives us the packed shape (118 740 B, entry
   > `0x01d0001c`) — **and that does not boot either**, so packing is not the
   > whole story. The proposed single-build fix is to
   > [load the USB modules from the card](backlog.md#load-ps2link-usb-modules-from-the-memory-card).
   > The high build is the default because it is the one that demonstrably boots.
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
   Welcome to TyraX ps2link r5 (USB keyboard + mouse)
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

1. clears the previous `ps2client` — **its own**, and only its own
   ([below](#one-file-server-at-a-time));
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

### One file server at a time

A console can have exactly one `ps2client` serving it, and this PC can run one
at all — it binds the ports below, which are fixed. So a deploy has to clear the
field before it starts, and **which** servers it may clear is the whole question.

Until 1.22.0 the answer was `taskkill /F /IM ps2client.exe` — machine-wide, by
name. Deploying project B therefore killed project A's file server, and A's
console kept running, blocked on `host:`, with `bin/livedbg.bin` and
`bin/livetime.bin` frozen to the second B was deployed. The `[ps2]` log kept
scrolling the whole time, because that stream is UDP straight to whichever
`ps2client` is listening and never passes through the file server: **two
transports, one of them dead, and only the live one visible.** It read as "the
debugger shows nothing" rather than as "you just killed the other session". This
repo routinely runs several editors from several worktrees at once, so it was not
an edge case.

A deploy now reaps only what it owns:

| What it finds | What it does |
|---|---|
| The `ps2client` **this editor** spawned | Killed by handle, as before — the common case and the only certain one. |
| A `ps2client` serving **this project's** ELF on **this** console | Reaped, and said so. This is the stale server a crashed or killed run leaves behind; without this, only the first deploy after a crash would work. |
| A `ps2client` a **running editor** owns | **Refused.** The log names the project holding the channel and what to do: *Stop on PS2* there, or close that editor. |
| A `ps2client` **nobody** is running an editor for | Reaped as an orphan, and said so — otherwise a crashed editor would block every later deploy with no way out. |
| Anything transient (the `listen` witness a reset runs) | Waited out for 3 s before any of the above is decided. |

Ownership is read off the process's **own command line** — `-h <ip>` says which
console and `execee host:<name>.elf` says which game — matched against the
running editors that publish themselves (`--debug-state`). A server whose command
line cannot be read is never a target: guessing is the bug this replaced. The one
limit worth knowing is that the ELF name is the project's *name*, so two copies
of one project look alike; the console check is what separates them, and two
copies serving one console cannot exist anyway.

`--debug-state` prints the same inventory, so "whose `ps2client` is that" is one
command rather than a `Get-CimInstance` by hand.

**PCSX2 got the same treatment.** `--build --run` used to reap every emulator on
the machine by name, which repeatedly interrupted measurements in another
worktree; it now closes only the instance whose `-elf` names *this* project's
ELF. Several emulators running at once is normal here and is no longer anybody
else's problem.

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
- **No game process to query.** `--debug-state` finds a PCSX2 session by reading
  the emulator's command line; on hardware there is no such process, which is why
  the editor's session pointer records the transport (`ps2link`) instead. What
  there *is* on the PC is the `ps2client` file server, and `--debug-state` lists
  it with the console and the game its command line names.
- **The game's log arrives over the network, not in `bin/log.txt`.** A PCSX2
  run writes that file over `host:`; a ps2link run would pay a network
  round-trip per line, so the generated `main.cpp` leaves
  `Tyra::Info::writeLogsToFile` off and logs to the EE console, which ps2link
  forwards to `ps2client`. The editor shows those lines as `[ps2] …` in
  **Output**, and the **Debug** window falls back to the same stream when there
  is no log file — so `TYRA_LOG`, `TYRA_ERROR` and assertion dumps land in the
  usual place either way. (Until 2026-08-06 they landed *nowhere* on hardware:
  the EE's stdout is not a tty, so newlib buffered it fully and a game that
  never exits never flushed. `TyraDebug::emit` flushes now. A game built before
  that fix logs nothing over ps2link — rebuild it before concluding that the
  code you are chasing is silent.)
- **Keyboard and mouse work**, because this is the TyraX ps2link: tick
  *Project > Preferences > Build > Keyboard & mouse controls* and the
  *Also over ps2link* sub-option under it is already on — the game reuses
  ps2link's resident USB stack instead of loading one it cannot load. See
  [keyboard-mouse.md](keyboard-mouse.md).
- **The EE crash handler** ([devkit.md](devkit.md#crashes-and-hangs))
  is still opt-in and unproven on hardware; the heartbeat post-mortem needs
  nothing from the game and works here.

**Stop on PS2** kills the file server and sends `reset` — the file server
first, both because the game polling `host:` every frame is what the command
has to cut through and because the port is then free for the `ps2client listen`
the editor runs as a witness. It clears servers by the same ownership rule as a
deploy, and a refusal stops the **reset** too: if another editor holds the
channel, the game on the console is theirs, and resetting it would stop their
game on a button that promises to stop yours. The reset is fire-and-forget UDP, so the editor
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
| Boot banner is below `r6` | r1 = keyboard/mouse only; r2 = plus the hang fixes; r3 = plus the SPU2 silencing, but Stop still wedges the console; r4 = Stop kills the game reliably; r5 = no stdio on any error path. **Stop only survives from r6.** Reflash. |
| Stop leaves the console frozen on the game | Fixed in r4. On a pre-r4 build the reset either never reached the console or killed the game and took ps2link down with it; only a power cycle recovered. |
| Stop kills the game but the console then needs a power cycle | **Fixed in r6** — `pkoReset()` rebooted the IOP immediately before `ExecPS2()` and crt0 then ran against an IOP with no modules (12/12 cycles since; see "One IOP reboot per Stop" above). On r4 it could show as an EE exception with `BadAddr 8` and on r5 as a black screen with no exception; both are the same wedge wearing different faces. Reflash r6. |
| The game boots to a frozen Tyra logo, last log line `Curent pad(0,0) status: DISCONNECT` | The pad had not settled yet and the engine waited for it forever. Fixed in the engine (`vendor/tyra`, `Pad::waitPadReady` is bounded now), so rebuild the game. A deploy also waits ~10 s after the reset for exactly this reason. |
| Stop works once and the next one wedges | Partly r4 — the `.bss` half of it — and the rest is the double IOP reboot r6 removed. Seen on r4 and r5; if you still see it on r6, that is a new bug, so say so. |
| Console wedges after the editor is closed, or after a `ps2client` dies | Fixed in r2 (`pko_recv_bytes()` spun forever on a closed peer while holding the `host:` lock). If it still happens on r2, that is a new bug — say so, it is not the old one. |
| A debug build dies after a random number of frames, `BadAddr 0x00000010`, EPC in `rpc_packet_free`/`_request_end` | A duplicate SIF RPC completion — see [the SIF RPC completion crash](#the-sif-rpc-completion-crash-and-the-guard-against-it). The engine guard stops the crash; check `SifRpcGuard::rejected()` and report the count. **Not** a VU, assembler or clipper fault, and **not** a stale ps2link. |
| `dumpmem` writes a zero-byte file | Its offset and size are **decimal**, not hex. |
| `host:` reads start returning wrong/garbage data | Fixed in r2 (an unexpected reply used to desync the byte stream permanently). |
| A game crash shows nothing at all on screen | Fixed in r2 (the IOP exception handler faulted on `strlen(NULL)` before it could report). |
| Sound keeps droning / goes haywire after a reset or redeploy | Fixed in r3. Check the boot log for `SPU2 silenced`; if it is absent you are on a pre-r3 build. As a stopgap on an old build, `ps2client -h <ip> execee host:silencer.elf` from `tools/silencer/`. |
| No sound at all in a game, on r3 only | Would mean the silence is sticking when it should not — `libsd`'s init is supposed to reprogram every register it clears. Report it with the boot log; the fallback is to flash r2 while it is diagnosed. |
| `Net config:` shows `192.168.1.10` | `IPCONFIG.DAT` was not found next to `PS2LINK.ELF`; ps2link fell back to its compiled-in default. |
| Assets crawl in, boot takes minutes | An unpatched `ps2client` on `PATH` is being used instead of `tools/ps2client/bin`. |
| `KbdMouse: mouse skipped (no resident USB stack…)` | The running ps2link has no USB stack — a stock one. |
| Both drivers "ready" but nothing responds | The devices. `ps2kbd`/`ps2mouse` only speak the USB HID **boot protocol**; test them in uLaunchELF first. |
| Devkit panels frozen, game still running | The editor (and with it `ps2client`) was closed. Redeploy. Before 1.22.0 a deploy of *any other* project did this to you too — see [One file server at a time](#one-file-server-at-a-time). |
| `[editor] The ps2link channel is already taken: ps2client pid N serving host:<other>.elf` | Another editor is holding the file server; the deploy refused rather than killing its session. *Stop on PS2* in the editor it names, or close it, then run again. |
| `[editor] Reaping an orphaned file server` | A `ps2client` was left behind by an editor that is no longer running. Expected after a crash; the deploy continues. |

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

Two layers, and neither needs the console: a host harness for the `host:`
protocol code, and — since 2026-07-31 — **the whole of ps2link running inside
PCSX2**, driven by the real `ps2client`.

#### The host harness (protocol code)

It compiles the **real** `iop/net_fio.c` out of the work tree against stub
IOP/lwip headers and drives it with a scripted fake socket:

```bash
tools/ps2link/test/run.sh
```

`run.ps1` on Windows. `--pristine` / `-Pristine` points the same tests at the
untouched upstream file and **expects them to fail** — that A/B is the evidence,
so keep both halves passing. It covers framing, EOF, short reads and the
buffer clamps.

#### ps2link in PCSX2 (the emulated console)

This is the layer that pays for the "the console is wedged, go and power-cycle
it" loop: **a wedge costs `Stop-Process`, not a walk to the console.** PCSX2
emulates DEV9, so ps2link's network comes up and `ps2client` talks to the
emulated machine exactly as it talks to the real one — same subnet, same ports,
same commands, same `execee`. It reproduces the r6 bug (see the A/B below), so
it is not a toy.

Set it up **beside** your normal PCSX2, never on top of it: parallel sessions
run their own emulator and the editor rewrites `PCSX2.ini` on every launch.

1. Copy the PCSX2 installation somewhere short (`%TEMP%\ps2link-lab\pcsx2`),
   copy the BIOS files you use into its `bios\`, and always start it with
   `-portable` so it keeps its own `inis/`, `logs/` and `snaps/`.
2. In that copy's `inis\PCSX2.ini`, section **`[DEV9/Eth]`**:

   ```ini
   [DEV9/Eth]
   EthEnable = true
   EthApi = PCAP Bridged
   EthDevice = {4694B5A4-4249-4851-99DA-900F44CDF04E}
   InterceptDHCP = false
   ```

   `EthDevice` is the **bare adapter GUID** (`Get-NetAdapter | Select-Object
   Name,InterfaceGuid`) — PCSX2 prepends `\Device\NPF_` itself. Needs Npcap and
   a **wired** adapter; bridged capture over Wi-Fi does not carry foreign MACs.
3. Put `ps2link.elf` and an `IPCONFIG.DAT` giving the emulated console a free
   **static address on your LAN** in one directory, and boot it:

   ```powershell
   Start-Process "$lab\pcsx2-qt.exe" -ArgumentList "-portable","-elf","$dir\ps2link.elf","-fastboot"
   ```

   The boot screen must show that address on its `Net config:` line — same
   check as on the console, same fallback to 192.168.1.10 if the file was not
   read. Then `ps2client -h <that ip> …` works from the PC.

Four things behave differently here, and each one has cost an hour:

- **`host:` is served by PCSX2, not by `ps2client`.** With `HostFs = true` the
  emulator answers `host:` itself, relative to the directory of the `-elf`
  image, and it shadows ps2link's own network file server. So
  `execee host:game.elf` fails with `Could not execute file` unless `game.elf`
  sits **next to `ps2link.elf`**, whatever the client's working directory is.
  For a game, copy `ps2link.elf` + `IPCONFIG.DAT` **into the project's `bin\`**
  and boot there — `host:` then maps onto `bin\` exactly like a normal PCSX2
  run. (Turning `HostFs` off hands `host:` back to ps2link, but then ps2link
  cannot read `IPCONFIG.DAT` either.)
- **Ping proves nothing, in either direction.** On hardware it answers in the
  exact failure state being chased; here it can time out while the console is
  perfectly alive, because ICMP loses to the reboot window. `execee` producing
  output is the only liveness check that means anything.
- **`emulog.txt` is opened exclusively** — stop that PCSX2 before reading it.
  It is also the only place a DEV9 misconfiguration is reported; the screen
  shows nothing.
- **`ps2client`'s stdout is block-buffered into a redirected pipe**, so a
  scripted `execee` shows nothing until the process exits. Screenshot the
  emulated console instead (`screenshot-window.ps1 -ProcessName pcsx2-qt`).

#### Worked example: the r6 Stop bug, A/B in two minutes

The cycle the editor runs on F6 and *Stop on PS2*, against the emulated
console — deploy a payload, kill the file server, `reset`, deploy again:

```powershell
Start-Process $pc -ArgumentList "-h",$ip,"execee","host:hello.elf" -WorkingDirectory $dir
Start-Sleep 10                                    # payload running
Get-CimInstance Win32_Process -Filter "name='ps2client.exe'" |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
& $pc -h $ip -t 10 reset                          # this is the Stop
Start-Sleep 12
Start-Process $pc -ArgumentList "-h",$ip,"execee","host:hello.elf" -WorkingDirectory $dir
```

| Image | After the Stop | Second Run |
|---|---|---|
| **r4** | the EE falls through to the **BIOS browser** and the console drops off the LAN | dead — the "go and power-cycle it" state |
| **r6** | the `Welcome to TyraX ps2link r6 … Net config:` banner is back | the payload runs again |

Build the image under test into the rig without disturbing the work tree by
copying `tools/ps2link` elsewhere first, then `git -C build checkout -- .`,
`git -C build apply ../tyrax.patch` and the `docker run … make ee LOADHIGH=1`
line `build.ps1` uses.

#### What the emulator cannot do

**EE exceptions.** The r4 stdio crash — `printf()` on a re-executed image, NULL
newlib lock, `lw v1,8(s0)` → `BadAddr 8` — was replayed there deliberately: the
`printf` **returned normally** and the word at `0x00000008` read back as
`0x00000000`. In PCSX2 main RAM starts at address 0, so a NULL dereference is an
ordinary load and never misses the TLB. That is also why a deliberate crash
cannot be tested in the emulator (see [devkit.md](devkit.md)) — anything about
faults, `crash.txt` or the EE crash handler stays a hardware test, as does
timing, and so does the real `host:` file server while `HostFs` is on. What the
rig is good for is everything above that line: sequencing, restarts, IOP
reboots, the network protocol, and "did the restarted image reach `main()`".

## The SIF RPC completion crash, and the guard against it

**Symptom.** A debug-profile game dies on hardware after an arbitrary number of
frames — 1 800 in one run, 122 640 clean in another with the *same* ELF — with

```
Cause 0x70008408   -> ExcCode 2 (TLBL, fault on a LOAD)
BadAddr 0x00000010
EPC 0x00271C78     -> rpc_packet_free / _request_end   (ps2sdk sifrpc.c)
```

**It is not the VU pipeline and it is not your game.** `sifrpc.c` is reachable
from no VU program; it is ps2sdk's EE-side SIF RPC client, and every `host:`
file operation is an RPC through it. Nor is it a stale ps2link: the fault was
diagnosed on a console confirmed to be running **r6**, read out of its own RAM
(see "Identifying a flashed card over the network" below).

**What it is, to the byte.** `rec_id` sits at offset `0x10` of
`SifRpcRendPkt_t`, and `rpc_packet_free()` read-modify-writes it. So `BadAddr
0x10` on a *load* pins the fault on exactly one expression:

```c
rpc_packet_free(cd->hdr.pkt_addr);   /* with cd->hdr.pkt_addr == NULL */
```

`cd` itself was valid — the handler had already read `end_function` and
`sema_id` out of it. And the **only** code that ever stores NULL there is
`_request_end`'s own last line. So the EE was handed a **second completion for
a call it had already completed**, and ps2sdk's handler validates nothing —
not the client, not the packet, not the `rpc_id`, even though its own
`sceSifCheckStatRpc()` shows the intended check.

**Why here and not in an ordinary PS2 game.** Each ps2sdk RPC client is
serialised internally (fileio by `_fio_io_sema`, audsrv by its own
`completion_sema`), but a TyraX game runs **four** SIF RPC participants at once:

| participant | priority | traffic |
|---|---|---|
| game loop | `0x40` | the devkit channels, streamed assets, save files |
| audio thread | `0x5` | `audsrv_play_audio` continuously — **preempts the loop** |
| song streamer | `0x6` | its own `fread` every ~2 ms — **preempts the loop** |
| audsrv EE RPC *server* | `0x60` | incoming fillbuf callbacks |

and over ps2link a `host:` round trip is *milliseconds of network* instead of
microseconds of emulator, which widens every window in that machinery by about
a thousandfold. That is why the fault is hardware-only and why it is a race
rather than a reproducible step. The devkit path concentrates it further: seven
channel files are polled on **one frame in 25**, back to back, because every
cooldown starts at 1 and re-arms to the same number, so they stay in lockstep
forever.

**The guard.** The engine installs its own `SIF_CMD_RPC_END` handler at init —
[`vendor/tyra/engine/src/debug/sifrpc_guard.cpp`](../vendor/tyra/engine/src/debug/sifrpc_guard.cpp).
It is ps2sdk's `_request_end` with **one** thing changed: the packet free is
skipped when the packet pointer is null, and *everything else still runs*. The
client is always completed — `end_function`, then `iSignalSema` — because
skipping that is what hangs the game.

That shape was arrived at the hard way, and the history is the useful part. The
first version returned early on rejection, and under the teardown trigger below
it **hung the game on 2 of 4 teardowns while an unguarded build survived 6 of
6**. Two independent deadlocks, both from the early return: the thread inside
`sceSifCallRpc` is parked in `WaitSema(cd->hdr.sema_id)` and only that signal
releases it, and skipping `end_function` hangs the *next* call as well, because
`fioOpen`/`fioRead` take `_fio_completion_sema` before the RPC and it is
`_fio_intr` — the end function — that releases it.

It also carried an `rpc_id` check justified by the claim that
`cd->hdr.rpc_id` only goes stale when a packet is recycled. **That claim is
false and the check is gone**: `_SifCmdIntHandler` calls `EI()` at its top so
completions dispatch re-entrantly, and ps2sdk signals the semaphore *before*
clearing `pkt_addr`, so a woken thread can start the next call on the same
client inside that window — which makes a legitimate late completion
indistinguishable from a recycled one. Do not re-add a check resting on that
argument.

It also **counts** every rejection, and that is the point: a rejection is not
normal. `SifRpcGuard::rejected()` is 0 in a healthy session, so a non-zero
count means the fault reproduced *without* taking the console down, and the
count is the instrument for finding whatever produces the duplicate. **A zero
count is not proof the race is gone** — only proof it did not fire.

> **The guard has no demonstrated benefit yet, and that is stated on purpose.**
> The fault it defends against has never been reproduced — not by two hours of
> amplified RPC load, not by a live editing session, and not by the teardown
> trigger, which an unguarded build survived 6 times out of 6. What is measured
> is that it does no harm: it boots and runs in PCSX2 across three RPC clients,
> and survives 3 of 3 teardowns on hardware. Three clean cycles against a
> 2-in-4 hang rate is p ~= 0.125, i.e. suggestive rather than settled. The
> producer of the anomalous completion is still unidentified; see
> [backlog.md](backlog.md).

### Do not run a second `ps2client` against a console serving a game

Measured 2026-08-13: firing `ps2client dumpmem` at a console while a game was
running over ps2link **stopped the game within 7 seconds**. ps2link itself
survived — ping answered, `tcp/18193` kept listening, the `execee` file server
process was still alive — but the game never wrote another devkit snapshot,
i.e. it lost `host:` and stayed lost.

This is the ["one file server at a time"](#one-file-server-at-a-time) rule seen
from the other end: the rule is usually stated as "do not kill someone else's
`ps2client`", but the converse matters just as much — **do not add a second
client to a console that is already serving one**, even a read-only one, even
your own. It is not a diagnostic you can use on a live session, and it makes a
poor amplifier in an experiment: it perturbs the file channel rather than the
SIF RPC path, so a game that dies under it has told you nothing about RPC.

Use it on an idle console (identifying a flashed card, below), or after a
session has already ended.

### Identifying a flashed card over the network

The boot banner goes to the TV via `scr_printf`, not over udptty, so it does
not appear in the `[ps2]` log — and `ps2client listen` returns nothing on some
setups. You can read it out of the console's own RAM instead, which needs no
inbound firewall rule (the reply rides the TCP socket the PC opened):

```powershell
tools/ps2client/bin/ps2client.exe -h <ip> -t 20 dumpmem 606208 393216 low.bin
```

Then `strings low.bin | grep -i "TyraX ps2link"`. **`dumpmem` takes DECIMAL
offset and size, not hex** — a hex argument yields a zero-byte file with no
error. `606208` is `0x00094000` (the low build); use `32407552` (`0x01ee8000`)
for the high build. The region that is *not* in use reads back as all zeros,
so this also tells you which of the two link addresses is flashed. A card
running the recommended build answers:

```
Welcome to TyraX ps2link r6 (no USB)
SPU2 silenced
```

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
