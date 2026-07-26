# Custom ps2link for TyraX

A drop-in replacement for **ps2link** (the network debug loader that backs the
editor's *Run on PS2* / F6 deploy). It bakes two things into ps2link's own
boot that stock ps2link does not carry:

1. the **USB HID stack** — `usbd` + `ps2kbd` + `ps2mouse` — so a game deployed
   over the network can use a **keyboard and mouse** on real hardware;
2. **`ps2ips.irx`**, the EE-facing RPC server for the IP stack, so a game
   deployed over the network can open **sockets**.

(The folder is still named `ps2link-usbhid` for its first purpose.)

## Why the socket module is needed

ps2link already boots `netman` + `smap` + `ps2ip-nm` and keeps them resident,
so the IP stack is up while your game runs — and the game's toolchain ships
`libps2ips`, the EE-side client for it. That looks like sockets for free, and
it is not: `libps2ips` binds by RPC to **`ps2ips.irx`**, which stock ps2link
never loads. Measured, not guessed — a spike calling `ps2ip_init()` in a game
deployed over stock ps2link **hangs there forever**, waiting for an RPC server
that does not exist. The log stops at the call and the game never reaches its
first frame.

With `ps2ips.irx` resident, that bind has something to answer it. This is the
groundwork for the devkit talking to the editor over a socket instead of
polling files over `host:` — every devkit file operation is a network
round-trip today, which is why the debugger is sluggish on hardware and why a
capture takes about a second and a half.

**Sockets in a game are NOT verified yet** — this build makes the module
resident; the game-side transport is the next step.

## Why this is needed

The network (SMAP) build of ps2link loads **no `usbd`**. `ps2kbd` and
`ps2mouse` import `usbd`'s symbols, so a game that loads them *after* ps2link is
already running — onto the resident IOP that ps2link keeps alive, without an
IOP reset — can't bring the USB stack up cleanly: the keyboard half-works via an
iomanX device, but `PS2MouseInit()` spins forever and the game freezes on the
Tyra logo. (The whole saga is in [PROGRESS.md](../../PROGRESS.md) entries
91–95 and [docs/keyboard-mouse.md](../../docs/keyboard-mouse.md).)

The fix: load `usbd` + `ps2kbd` + `ps2mouse` at **ps2link's own clean boot**
(right after its IOP reset, `usbd` first). Their drivers and RPC servers are
then already resident and registered before any game runs, so the game just
**reuses** them — no second `usbd` (which would wedge the first), and the mouse
binds instead of hanging.

## Build it

Needs Docker Desktop running. From this folder:

```powershell
./build.ps1
```

This clones a pinned ps2link, applies [`usbhid.patch`](usbhid.patch), builds it
inside the official `ps2dev/ps2dev` toolchain image, and writes **`ps2link.elf`**
here. (It builds with the current ps2dev toolchain, independent of the older
`h4570/tyra` image the games build in — ps2link is a standalone program.)

The patch is three mechanical edits to ps2link's `ee/` tree:
`ee/Makefile` (embed the three IRX), `ee/irx_variables.h` (extern the buffers),
`ee/ps2link.c` (`loadModules()` gains three `SifExecModuleBuffer`, **`usbd`
first**; the welcome screen is branded). It ships prebuilt
`usbd`/`ps2kbd`/`ps2mouse` IRX from `$PS2SDK/iop/irx/`.

You can tell the build apart on the console: its boot screen says
**“Welcome to TyraX ps2link (USB keyboard + mouse)”** instead of
“Welcome to ps2link”.

## Use it

1. **Flash** the built `ps2link.elf` onto your PS2 in place of stock ps2link
   (memory card via uLaunchELF/FMCB, or whatever boots your ps2link), and boot
   it as usual — the screen should greet you as *TyraX ps2link*. Plug a
   **wired USB keyboard and mouse** into the front ports.
2. In the editor: *Project > Preferences > Build > Keyboard & mouse controls*,
   then tick **Also over ps2link — needs the TyraX ps2link**.
3. Build + **F6**. Watch the *Output* panel / `ps2client`:
   `KbdMouse: keyboard driver ready` **and** `mouse driver ready` = full
   keyboard + mouse over the network deploy.

> That checkbox assumes this ps2link. On a stock one there is no USB stack to
> reuse: the keyboard reports "not ready" and the mouse is skipped on purpose
> (the engine guards `PS2MouseInit`, which would otherwise hang the boot).

> **Status:** not confirmed end-to-end on real hardware. Both drivers reported
> ready on a physical PS2, but the test keyboard/mouse turned out not to be
> recognised by the console at all (uLaunchELF didn't see them either), so
> keystrokes could never be verified. If your devices don't respond, check them
> in uLaunchELF first: `ps2kbd`/`ps2mouse` only speak the USB HID **boot
> protocol**, which many wireless/gaming devices don't expose.

## Caveat

The baked-in drivers live on the IOP that ps2link keeps resident. A game that
**resets the IOP** wipes them — the editor's generated games don't reset under
ps2link (that's the `keepIopResident` path), so they're fine. This only helps
the network dev loop; a burned/USB ISO already gets full keyboard+mouse the
normal way.

`usbhid.patch` is pinned to ps2link commit `0c6138c`. If it stops applying
against a newer ps2link, re-generate it against the three files above.
