# The TyraX ps2link

The **only** ps2link this editor deploys to. `ps2link` is the network debug
loader that backs *Run on PS2* / F6: the console boots the game off your PC and
serves the whole `host:` filesystem the devkit rides on. We run a patched one,
so it is built here rather than downloaded — `setup.ps1` / `setup.sh` fetch
[ps2client](../ps2client/README.md) (the PC side) and nothing else.

Full end-to-end setup — hardware, flashing, `IPCONFIG.DAT`, the editor
preference, troubleshooting — is in
**[docs/ps2link-setup.md](../../docs/ps2link-setup.md)**. This file is about the
build itself.

## Build it

Needs Docker Desktop running. From the repo root (or this folder):

```powershell
tools/ps2link/build.ps1
```

Linux: `tools/ps2link/build.sh`. `-Clean` / `--clean` throws the work tree
away first.

**`ps2link.elf` is linked at `0x01ee8000`** (top of RAM). `-Low` / `--low`
builds the same thing at `0x00094000` — the "BIOS unused" window below the
`0x00100000` a game loads at — as `ps2link-low.elf`. `-NoUsb` / `--no-usb`
leaves the USB HID stack out and adds `-nousb` to the name; the switches
combine, so `-Low -NoUsb` writes `ps2link-low-nousb.elf`.

Upstream defaults to the low address and its CI publishes both variants
("default" and "highloading"); we default to high instead, because the low
build black-screens when booted from FreeMcBoot's menu or a shortcut (it boots
from uLaunchELF fine), and that is a worse failure than the high build's
trade-off: it sits at ~31.9 MB, so a game that allocates its way up there would
overwrite it. The boot screen prints the address, so the flashed card
identifies itself.

Why the low build black-screens under FMCB is **not settled, and the obvious
explanation is wrong**. The theory was image size: our patch bakes in the USB
HID stack, so the image reaches further into the same window as stock ps2link
and presumably over whatever FMCB keeps resident there. Measured, all at
`0x00094000`:

| build | image ends at | ELF | boots from the FMCB menu |
|---|---|---|---|
| stock upstream | `0x000dea20` | 233 396 B | reported yes, not re-verified |
| ours, `-NoUsb` / `--no-usb` | `0x000df3a0` | 235 828 B | **no** |
| ours, full | `0x000eb5a0` | 285 492 B | **no** |

A build within 2.4 KB of upstream fails exactly like the full one, so the
~50 KB tail is not the cause. What is left is either something else in the
patch, or the low address simply not working with that FMCB at all — in which
case stock ps2link would fail there too and the "it used to work" is a
misremembering. The next step is to boot a pristine upstream ELF from that
menu:

```bash
docker run --rm -v "$PWD/tools/ps2link/build:/work" -v "$PWD/tools/ps2link:/out"   ps2dev/ps2dev:latest sh -c 'cd /work && git checkout -- . && make clean >/dev/null 2>&1;
    make ee LOADHIGH=0 >/dev/null 2>&1 && cp ee/ps2link.elf /out/ps2link-upstream-low.elf'
```

None of this blocks anything: the high build is the default and boots from
everything we have tried.

Both scripts clone a **pinned** ps2link (`0c6138c`), apply
[`tyrax.patch`](tyrax.patch), run `make ee` inside the official `ps2dev/ps2dev`
toolchain image and write **`ps2link.elf`** here (~280 KB). It builds with the
current ps2dev toolchain, independent of the older `h4570/tyra` image the games
build in — ps2link is a standalone program. The ELF is gitignored; the patch is
what this repo maintains.

You can tell our build apart on the console: the boot screen reads
**“Welcome to TyraX ps2link r4 (USB keyboard + mouse)”** instead of
“Welcome to ps2link”. The `r<n>` is bumped whenever the patch changes console
behaviour — r1 was USB HID only, r2 added the hang/leak fixes, r3 silences the
SPU2, r4 makes stopping a running game work — so a memory card can be
identified without guessing.

## What the patch does

Four groups: the USB HID stack it started as, a set of robustness fixes to
upstream's error paths, the SPU2 silencing, and making the reset command work
against a running game.

### 1. The USB HID stack

Three mechanical edits to ps2link's `ee/` tree that bake the USB HID stack —
`usbd` + `ps2kbd` + `ps2mouse`, shipped prebuilt in `$PS2SDK/iop/irx/` — into
ps2link's own boot:

| File | Change |
|---|---|
| `ee/Makefile` | adds the three IRX to `IRX_FILES` (embedded by the generic `%_irx.c` rule) |
| `ee/irx_variables.h` | externs the three embedded buffers |
| `ee/ps2link.c` | `loadModules()` gains three `SifExecModuleBuffer` calls, **`usbd` first**; the welcome screen is branded; `PS2_DISABLE_AUTOSTART_PTHREAD` is `#ifdef`-guarded so newer ps2sdk versions still build |

**Why it has to be ps2link that loads them.** The network (SMAP) build of
ps2link loads no `usbd`, and `ps2kbd`/`ps2mouse` import `usbd`'s symbols. A game
deployed over the network runs on the IOP ps2link keeps alive — without a reset —
so it cannot bring the stack up afterwards: the keyboard half-works via an
iomanX device and `PS2MouseInit()` spins forever on an RPC server that never
registered, freezing the boot on the Tyra logo. Loading them at ps2link's own
clean boot (right after its IOP reset, `usbd` first) leaves the drivers and
their RPC servers resident before any game runs, so the game just **reuses**
them. The saga was written up as entries 91–95 of the retired `PROGRESS.md`
(readable from git history — see [docs/backlog.md](../../docs/backlog.md)); the
standing documentation is
[docs/keyboard-mouse.md](../../docs/keyboard-mouse.md).

The baked-in drivers live on the IOP ps2link keeps resident, so a game that
**resets the IOP** wipes them — the editor's generated games don't reset under
ps2link (the `keepIopResident` path), so they're fine.

> **Status:** the keyboard/mouse path is not confirmed end-to-end on real
> hardware. Both drivers reported ready on a physical PS2, but the test
> keyboard/mouse turned out not to be recognised by the console at all
> (uLaunchELF didn't see them either), so keystrokes could never be verified.
> `ps2kbd`/`ps2mouse` only speak the USB HID **boot protocol**, which many
> wireless/gaming devices don't expose — check yours in uLaunchELF first.

### 2. Hang and leak fixes

Upstream ps2link assumes the wire never misbehaves, and its error paths turn
recoverable hiccups into a dead console. The full table — what was wrong, where,
and why it mattered — is in
[docs/ps2link-setup.md](../../docs/ps2link-setup.md#1-build-the-tyrax-ps2link).
The short version, worst first:

- **`pko_recv_bytes()` spun forever when the PC closed the connection.**
  `recv()` returning 0 did `left -= 0`. It happens on every `ps2client` exit, and
  it spins at IOP priority 9 while holding the `host:` semaphore, so everything
  after it blocks permanently. This is the "it pierdoli and needs a reset" bug.
- **`pko_accept_pkt()` left the byte stream out of frame** after any unexpected
  reply, because it never consumed the packet's body.
- **`pko_read_file()` trusted the PC's byte count** and wrote past the caller's
  buffer.
- **The IOP exception handler faulted on `strlen(NULL)`** for exactly the crashes
  people care about, so a game crash reported nothing.
- **`pkoExecEE()` zeroed `cmdThreadID`** (the wrong variable) on a failed launch,
  after which ps2link answered no commands at all.
- **`pkoReset()` was thought to wait on `SifIopSync()` inverted** — it was not,
  and r4 undid that "fix": the two loops in the tree wait for opposite edges of
  the same bit on purpose (see group 4).
- Busy-loops on failing `accept()`/`recvfrom()`, `host:` fd leaks in
  `dumpmem`/`writemem`/`dumpreg`, a thread + semaphore leak on re-mount, and a
  handful of unchecked wire values.

The protocol is untouched, so `tools/ps2client` needs no matching change.

### 3. Silencing the SPU2 on reset

The SPU2 is a separate block from the IOP and **keeps its registers across an IOP
reset**, so it carried on looping the dead game's voices and mixing whatever
autodma had left in its input. That is the "sound goes haywire when ps2link
restarts", and it hit every redeploy — not just *Stop*.

`spu2Silence()` (in `iop/cmdHandler.c`) keys off all 24 voices on both cores,
zeroes voice volumes, unroutes them from the dry and reverb mixes, clears `MMIX`
so the autodma input is unrouted too, clears `CORE_ATTR` and zeroes the output
volume block. It runs from `pkoReset()` *and* from ps2link.irx's `_start()`, so
it covers both sides of the reset and an IOP reset that never went through
`pkoReset()` at all. The boot log prints `SPU2 silenced` when it fires.

It can't leave you with no audio: every write is a zero or a key-off (so it
cannot set a bit that wasn't set), and every register it touches is one
`libsd`'s init reprograms — so the next `audsrv_init()` restores all of it. That
was verified by diffing the two register sets out of the disassembly rather than
assumed; the one-liner to redo it is in
[docs/ps2link-setup.md](../../docs/ps2link-setup.md#1-build-the-tyrax-ps2link).

Consequence for the editor: *Stop on PS2* no longer deploys
[`tools/silencer`](../silencer/) or sleeps ~7 s waiting for it. The tool stays in
the tree as a manual fallback for consoles still on a pre-r3 ps2link.

### 4. Stopping a running game (r4)

*Stop on PS2* had never worked against a game on hardware. Two independent
faults, both measured on a console, both covered in detail in
[docs/ps2link-setup.md](../../docs/ps2link-setup.md#1-build-the-tyrax-ps2link):

- **the command never arrived** — the IOP command listener ran at priority 60,
  *below* the `host:` file server at 9, and a game of ours polls `host:` about
  ten times a frame. It now takes `USER_HIGHEST_PRIORITY` (9, the highest a
  user thread may have) and the file server sits one below;
- **ps2link never came back** — killing the game's thread leaves the DMAC, the
  VUs, VIF, GIF and IPU running and the EE exception vectors owned by the
  game's `libeedebug` handler. `pkoReset()` now quiesces them, re-arms the SIF0
  chain, takes the vectors back, waits for *both* edges of `SifIopSync()`
  (BOOTEND clearing, then setting) and re-execs the image, which is the only
  way to get ps2sdk's `.bss`-resident state cleared.

Verified: four Run → Stop cycles back to back, no power cycle.

## Testing it without a console

PCSX2 runs no ps2link, but the `host:` protocol code can be tested on the PC:

```powershell
tools/ps2link/test/run.ps1
```

`run.sh` on Linux. It compiles the **real** `build/iop/net_fio.c` against the
stub IOP/lwip headers in `test/shim/` and drives it with a scripted fake socket,
checking framing, EOF handling, short reads and the buffer clamps. `-Pristine` /
`--pristine` runs the same tests against the untouched upstream file and
**expects failure** — that A/B is what makes the suite worth anything, so if you
change `net_fio.c`, keep both halves honest. Needs `build/` to exist (run
`build.ps1` once) and a host `gcc`.

Anything touching threads, the SIF or the GS is still hardware-only.

## Changing it

Expect to. The loop:

1. build once — `build/` is then a full ps2link checkout at the pinned commit
   with the patch applied;
2. edit the sources in `build/`;
3. **regenerate the patch before rebuilding** (both scripts start with
   `git checkout -- .` and would discard your edits):
   ```powershell
   git -C tools/ps2link/build diff --output=../tyrax.patch
   ```
   `--output` rather than `>`: git writes the bytes itself, so they stay LF and
   UTF-8 (Windows PowerShell's `>` produces UTF-16, which `git apply` rejects).
   The path is relative to the `-C` directory, hence `../`.
4. rebuild, reflash `PS2LINK.ELF`.

Keep the patch LF-only (`.gitattributes` enforces it): it is applied to a Unix
checkout inside a Linux container, and a CRLF patch fails `git apply`. To move
to a newer upstream, bump the commit in **both** `build.ps1` and `build.sh` and
re-generate the patch against the new tree. If a change alters console
behaviour, bump the `r<n>` in the banner and update
[docs/ps2link-setup.md](../../docs/ps2link-setup.md) — that banner is how anyone
tells builds apart, including which fixes a flashed card already has.

Note that the pinned commit `0c6138c` **is** upstream's head, so there is no
newer ps2link to pull these fixes from; they live here or nowhere.
