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

Both scripts clone a **pinned** ps2link (`0c6138c`), apply
[`tyrax.patch`](tyrax.patch), run `make ee` inside the official `ps2dev/ps2dev`
toolchain image and write **`ps2link.elf`** here (~280 KB). It builds with the
current ps2dev toolchain, independent of the older `h4570/tyra` image the games
build in — ps2link is a standalone program. The ELF is gitignored; the patch is
what this repo maintains.

You can tell our build apart on the console: the boot screen reads
**“Welcome to TyraX ps2link (USB keyboard + mouse)”** instead of
“Welcome to ps2link”.

## What the patch does

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
re-generate the patch against the new tree. If a change alters the boot banner,
update [docs/ps2link-setup.md](../../docs/ps2link-setup.md) — that banner is how
anyone tells this build apart from a stock one.
