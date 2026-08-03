# Keyboard & mouse controls

Generated games can be played with a USB keyboard and mouse — in PCSX2 out of
the box, and on a real PS2 with USB devices plugged into the front ports.
The feature is a project preference: *Project > Preferences > Build >
Keyboard & mouse controls* (stored as `"keyboardMouse"` in the `.tyra`).

**New projects start with it off.** A pad game gains nothing from loading three
IRX drivers it never polls, and the console only speaks the USB HID *boot
protocol* (see below), so this is a choice to make on purpose rather than a
default to discover. Tick it for a keyboard/mouse game — nothing else changes,
the pad keeps working either way. Projects created before that default keep
whatever they saved, and projects older than the preference itself still load
with it **on**, exactly as they did when the feature shipped for everyone.

## How it works

- **Engine** (`vendor/tyra`): with `EngineOptions::loadUsbKbdMouse` the
  `IrxLoader` loads `usbd` + the PS2SDK HID drivers `ps2kbd`/`ps2mouse`
  (embedded like every other IRX), and `Engine::kbdMouse` (`pad/kbd_mouse.*`)
  polls them once per frame: a 256-bit held/clicked bitmap of USB HID key
  codes from the keyboard's raw read mode, and relative mouse deltas +
  a button mask (the mouse driver runs in DIFF mode — x/y/wheel accumulate on
  the IOP between frames and reset on read). Devices hot-plug; when absent,
  every query returns "no input".
- **Virtual pad**: the generated game maps keys onto pad buttons through
  `Pad::injectVirtual` (a TyraX fork addition) — held keys OR into the pad's
  pressed set, click edges are derived engine-side, WASD deflects the left
  stick fully (the analog deadzone / response curve applies to it like a real
  stick). Because everything downstream reads `engine->pad`, menus, the save
  menu, flow-graph *On Button* triggers, scripts and the walkers all react to
  the keyboard without knowing it exists — and a real DualShock keeps working
  at the same time. `injectVirtual` takes an overlay **slot** (this path uses 0,
  the [Remote Pad](remote-pad.md) uses 1) because click edges are derived from
  the previous overlay: two sources sharing one history would each look like the
  other had released everything, turning a held button into a click every frame.
- **Mouse look** bypasses the stick path: the FPP and Player-entity walkers
  add the per-frame deltas straight to yaw/pitch, so the same swipe turns the
  same angle at any frame rate (no `g_frameScale`, no deadzone eating slow
  movements).

## Default bindings

The bindings are authored in **Tools > Input Map** and reach the game two ways:
the generated `inc/input_map.gen.hpp`/`.cpp` tables (what the runtime reads, and
what a player's in-game rebind changes) and the generated, still **user-ownable**
`inc/controls.hpp` macros derived from the same default preset. Rebind buttons in
the Input Map, not by hand — see
[docs/input-bindings.md](input-bindings.md). Keys are USB HID usage codes
(usb.org HID Usage Tables).

The table below is the **default preset** every project starts with (and that
`project::ensureInputActions` backfills into projects predating the Input Map),
so out of the box nothing changed:

| Input | Code | Maps to |
| --- | --- | --- |
| `W` / `S` / `A` / `D` | 0x1A/0x16/0x04/0x07 | left stick (walk/strafe) |
| `Space` | 0x2C | `BTN_JUMP` (Cross): jump / menu select |
| `E` | 0x08 | `BTN_USE` (Square): interact |
| `Enter` | 0x28 | Cross (menus: select) |
| `Backspace` | 0x2A | Triangle (menus: back / close) |
| `Esc` | 0x29 | Start (pause menu) |
| `R` | 0x15 | Circle (save menu: load slot) |
| Arrow keys | 0x4F–0x52 | d-pad |
| `Left Shift` | 0xE1 | the **sprint** action (pad R2) |
| Mouse motion | — | camera look (`MOUSE_SENSITIVITY`, radians per count = 0.003 × value) |
| Left / right / middle button | — | `BTN_USE` / `BTN_JUMP` / Circle |

Every one of these is one action's `key` slot in the Input Map's default preset,
so they are all editable per project. They are **not** rebindable in-game: a
menu *Rebind key* row covers the pad only, because this whole feature is still
experimental (see the hardware status below) — keyboard/mouse rebinding is meant
to get its own dedicated menu later. The fold itself lives in
`src/gen/input_map.gen.cpp`
(`inputApplyKeyboardMouse`) and walks the LIVE bindings, which is why a preset
switch or a rebind moves the keys too;
`controls.hpp`'s `applyKeyboardMouseInput()` is now a one-line call into it.

An older user-owned `controls.hpp` (without the keyboard section) keeps
compiling against a regenerated game: every call site in `terrain_game.cpp`
is guarded by the `TYRAX_KBD_MOUSE` define the new file introduces. Such a file
also keeps its own hardcoded fold, so keyboard *rebinding* does nothing until you
delete it and let the current version regenerate.

## PCSX2

The editor configures PCSX2 automatically before every launch (same policy
as the forced `HostFs` setting): with the preference on, `PCSX2.ini` gets

```ini
[USB1]
Type = hidkbd
hidkbd_Keyboard = Keyboard

[USB2]
Type = hidmouse
hidmouse_Pointer = Pointer-0
hidmouse_LeftButton = Pointer-0/LeftButton
hidmouse_RightButton = Pointer-0/RightButton
hidmouse_MiddleButton = Pointer-0/MiddleButton
```

While the game window is focused, PCSX2 captures the mouse (hides the cursor
and recenters it every frame — standard FPS capture) and streams host input
into the emulated USB devices. `bin/log.txt` shows
`KbdMouse: keyboard driver ready` / `mouse driver ready` when the drivers
enumerated the devices.

### Linux/Wayland: the camera spins into the floor (fixed, but know why)

That recentring is the whole trick, and it is also where it breaks. PCSX2's
relative-mouse mode warps the host cursor back to the window centre after every
motion event and reports **how far it had travelled from the centre** as the
delta. **Wayland does not let a client move the pointer**, so on a native
Wayland surface the warp is a silent no-op: the cursor stays where your hand
put it, and every later event reports the whole distance from the centre
instead of the movement — over and over, every frame. The camera slams into
the pitch limit and keeps turning. It reads exactly like a broken game, and it
is not: the same ELF, the same mouse, the same PCSX2 behave perfectly through
XWayland.

Measured on GNOME/Wayland (fpp fixture, five *identical* 10-count nudges
downward, horizon row in the captured frame):

| | 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- |
| XWayland (`QT_QPA_PLATFORM=xcb`) | −17 px | −16 | −15 | −17 | −16 |
| native Wayland | −171 px | −20 | *horizon off screen* | — | — |

So **the editor launches PCSX2 with `QT_QPA_PLATFORM=xcb` on a Wayland
session** (`Runner::launchPCSX2`, and the same guard in a generated project's
`run.sh`), and says so in *Output*. It is deliberately narrow: only when the
project's *Keyboard & mouse controls* preference is on (a pad game never reads
the mouse, and XWayland costs it fractional-scaling crispness for nothing),
only when `DISPLAY` is set so there is an XWayland to fall back to, and never
when you exported `QT_QPA_PLATFORM` yourself — **that export is the opt-out.**

Two things this is *not*. It is not the VM: it reproduces the same way whether
or not the pointer is a VMware/VirtualBox absolute device. And it is not the
PS2 side — `KbdMouse::update` already forces `PS2MOUSE_READMODE_DIFF` and
zero-inits the packet, which are the two ways the *guest* can produce a
constant phantom delta (both are commented in
`vendor/tyra/engine/src/pad/kbd_mouse.cpp`). If mouse look ever runs away
again, check which Qt platform plugin PCSX2 actually loaded before suspecting
either:

```bash
grep -oE "libq(xcb|wayland)[a-z-]*\.so" /proc/$(pgrep -x pcsx2-qt)/maps | sort -u
```

### In a VM: turn the guest's mouse integration OFF

Same root cause from the other side, and this one the editor can only warn
about. A virtual machine's guest-integration pointer — VMware's `VMMouse`,
VirtualBox's *mouse integration*, a QEMU/SPICE tablet — is **absolute**: it is
what lets the cursor cross between host and guest without grabbing, and it
re-asserts the physical position the instant anything moves the pointer.

PCSX2 recognises its own warp by the motion event arriving **from the window
centre**. With an absolute pointer the echo does not arrive from the centre, so
PCSX2 reads its own warp as movement and warps again — a feedback loop. Measured
on a VMware guest, with the game logging every packet it received:

- **42%** of the packets carried an exact multiple of **127** — the HID boot
  protocol's `int8` limit, i.e. saturated packets, several per frame
- **46%** carried **254+ counts in a single frame**; the worst frame carried
  **762** (a synthetic 10-count nudge, for comparison, arrives as exactly `10`)
- consecutive deltas alternated **−381/+381, −254/+254** — the warp bouncing

What that looks like while playing, all four from the same loop: alt-tabbing to
the window (focus, no click) spins the camera absurdly fast; clicking to capture
works for about half a second and then goes dead; **moving the mouse drops the
emulation speed**, because the event storm eats the emulator's main thread; and
the pad and keyboard keep working perfectly throughout, which is what makes it
look like a game bug.

The editor prints a warning naming the offending device when it finds one
(`absoluteVmPointer` in `src/runner.cpp` reads `/proc/bus/input/devices` for an
ABS-without-REL pointer). The cure is on the **host**, with the VM powered off:

| VM | Setting |
| --- | --- |
| VMware | `vmmouse.present = "FALSE"` in the `.vmx` |
| VirtualBox | *Machine > Disable Mouse Integration* (Host+I) |
| QEMU/libvirt | drop the `tablet` input device, leave the relative mouse |

The guest keeps its ordinary relative PS/2 mouse (`/proc/bus/input/devices`
lists both on VMware), so warps stick, the echo is recognised and mouse look
behaves. The cost is that the cursor no longer glides between host and guest —
you grab it into the VM like it's 2005, which is exactly what an FPS wants.

## Real hardware & ps2link

On a real console the same build reads real USB HID devices — nothing to
configure — when the game boots from a disc, USB or the memory card. The F6
network deploy works too, but only because of *how* it boots: the console runs
the **TyraX ps2link**, which carries the USB stack for the game to reuse (below,
and [ps2link-setup.md](ps2link-setup.md)).

### Why a game can't load the drivers itself over F6 / "Run on PS2"

The generated `main.cpp` detects the ps2link deploy and sets
`IrxLoader::keepIopResident = true` — resetting the IOP would unload the
ps2link serving the game. The engine then computes
`withKbdMouse = loadUsbKbdMouse && (!keepIopResident || loadUsbKbdMouseUnderPs2Link)`:
without the resident stack it loads nothing, `KbdMouse::init()` never runs and
`applyKeyboardMouseInput` is a no-op. The pad loads separately, so a stock
ps2link gives you **pad works, keyboard/mouse dead silent** — that is the guard
doing its job, not a bug.

### Keyboard & mouse over ps2link: the TyraX ps2link

Reading logs is the hard part on hardware: a burned ISO has no `host:`
filesystem, so there is no `bin/log.txt`. A ps2link deploy is the opposite —
it forwards the EE console over the network (the editor's *Output* panel /
`ps2client` show `TYRA_LOG` live) — and it is also where a game cannot bring up
its own USB stack.

Why it can't simply be switched on: `ps2kbd`/`ps2mouse` import `usbd`'s
symbols, and a network-booted ps2link (its device list shows
`tty:(TTY via SMAP UDP)` + `dev9x:` and no USB) carries no `usbd` at all.
Adding the stack to a ps2link that is **already running** doesn't work either —
its IOP is never reset, the keyboard half-comes-up via its iomanX device
(`usbkbd:`) but `PS2MouseInit()` spins forever on a `ps2mouse` RPC server that
never registered, freezing the boot on the Tyra logo.

The fix is to bake `usbd` + `ps2kbd` + `ps2mouse` into **ps2link's own boot**
(on its freshly-reset IOP, `usbd` first), so the drivers and their RPC servers
are resident before any game runs and the game just **reuses** them. That is
what the **TyraX ps2link** does, and it is the only ps2link the editor deploys
to — you build it once and flash it (full setup:
[ps2link-setup.md](ps2link-setup.md)):

```powershell
tools/ps2link/build.ps1
```

(`build.sh` on Linux.) It clones a pinned ps2link, applies
[`tyrax.patch`](../tools/ps2link/tyrax.patch)
and builds in the `ps2dev/ps2dev` toolchain image, producing a `ps2link.elf`
whose boot screen reads **“Welcome to TyraX ps2link (USB keyboard + mouse)”**.
Flash it onto the memory card as `PS2LINK.ELF` and plug in a **wired** USB
keyboard + mouse.

In *Project > Preferences > Build > Keyboard & mouse controls*, **Also over
ps2link** (stored as `"keyboardMousePs2Link"`) is **on by default** for exactly
that reason — untick it only if you deliberately boot a stock ps2link. Build,
hit F6: the engine loads no USB modules of its own (a second `usbd` would wedge
the resident one) and initialises the drivers ps2link already has. Watch
*Output* / `ps2client`:

- `open name usbkbd:dev ... open fd = 3` + `KbdMouse: keyboard driver ready`
  **and** `KbdMouse: mouse driver ready` → both drivers are up; test WASD /
  mouse look / `E` / `Space` / arrows / `Esc`.
- `KbdMouse: mouse skipped (no resident USB stack - is this the TyraX
  ps2link?)` → the keyboard device didn't open, so the mouse was skipped on
  purpose (that guard is what keeps a stock ps2link from freezing the boot).
  You are almost certainly running stock ps2link.
- `Unknown device 'usbkbd'` + `open fd = -19` + `keyboard driver NOT ready` →
  same thing: no resident USB stack.
- both drivers "ready" but nothing responds → the **devices** are the problem,
  not the software. `ps2kbd`/`ps2mouse` only speak the **USB HID boot
  protocol**; plenty of wireless dongles and gaming keyboards/mice don't expose
  it cleanly, so they work in PCSX2 (which emulates a compliant device) and are
  invisible on hardware — including to other homebrew like uLaunchELF. Testing
  a keyboard/mouse in uLaunchELF first is the quickest way to tell a device
  problem from a TyraX problem.

The baked-in drivers survive only because the generated game keeps the IOP
resident under ps2link; a game that reset the IOP would wipe them. The engine
also gives the USB stack a short settle delay after loading `ps2kbd`/`ps2mouse`
(enumeration is asynchronous on real hardware and instantaneous in PCSX2), so
`PS2KbdInit`/`PS2MouseInit` don't run before a device has attached — that helps
the ISO/USB boot path too.

> **Status:** this path is **not confirmed on real hardware.** It got as far as
> both drivers reporting ready on a physical PS2, but the test devices turned
> out not to be recognised by the console at all (uLaunchELF didn't see them
> either), so keystrokes/motion could never be verified end-to-end. PCSX2 is
> fully verified.
