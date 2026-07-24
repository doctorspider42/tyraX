# Custom ps2link with USB keyboard + mouse

A drop-in replacement for **ps2link** (the network debug loader that backs the
editor's *Run on PS2* / F6 deploy) that bakes the USB HID stack —
`usbd` + `ps2kbd` + `ps2mouse` — into its own boot. With it, a game deployed
over the network can use a **keyboard and mouse** on real hardware; stock
ps2link can't (see below).

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
`ee/ps2link.c` `loadModules()` (three `SifExecModuleBuffer`, **`usbd` first**).
It ships prebuilt `usbd`/`ps2kbd`/`ps2mouse` IRX from `$PS2SDK/iop/irx/`.

## Use it

1. **Flash** the built `ps2link.elf` onto your PS2 in place of stock ps2link
   (memory card via uLaunchELF/FMCB, or whatever boots your ps2link), and boot
   it as usual. Plug a **wired USB keyboard and mouse** into the front ports.
2. In the editor: *Project > Preferences > Build > Keyboard & mouse controls*,
   then tick both **Force under ps2link (experimental)** and
   **ps2link already has USB drivers (reuse + mouse)**.
3. Build + **F6**. Watch the *Output* panel / `ps2client`:
   `KbdMouse: keyboard driver ready` **and** `mouse driver ready` = full
   keyboard + mouse over the network deploy.

> Only tick "ps2link already has USB drivers" when you actually booted this
> custom ps2link. On stock ps2link the reuse path finds no drivers and
> `PS2MouseInit` hangs — leave it off there (keyboard-only) and use an exported
> ISO for full mouse.

## Caveat

The baked-in drivers live on the IOP that ps2link keeps resident. A game that
**resets the IOP** wipes them — the editor's generated games don't reset under
ps2link (that's the `keepIopResident` path), so they're fine. This only helps
the network dev loop; a burned/USB ISO already gets full keyboard+mouse the
normal way.

`usbhid.patch` is pinned to ps2link commit `0c6138c`. If it stops applying
against a newer ps2link, re-generate it against the three files above.
