# Keyboard & mouse controls

Generated games can be played with a USB keyboard and mouse — in PCSX2 out of
the box, and on a real PS2 with USB devices plugged into the front ports.
The feature is a project preference: *Project > Preferences > Build >
Keyboard & mouse controls* (on by default, stored as `"keyboardMouse"` in the
`.tyra`).

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
  at the same time.
- **Mouse look** bypasses the stick path: the FPP and Player-entity walkers
  add the per-frame deltas straight to yaw/pitch, so the same swipe turns the
  same angle at any frame rate (no `g_frameScale`, no deadzone eating slow
  movements).

## Default bindings (`inc/controls.hpp`)

The bindings live in the generated `controls.hpp` — a **user-ownable** file
(delete the marker line to take ownership and remap per project). Keys are
USB HID usage codes (usb.org HID Usage Tables).

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
| Mouse motion | — | camera look (`MOUSE_SENSITIVITY`, radians per count = 0.003 × value) |
| Left / right / middle button | — | `BTN_USE` / `BTN_JUMP` / Circle |

An older user-owned `controls.hpp` (without the keyboard section) keeps
compiling against a regenerated game: every call site in `terrain_game.cpp`
is guarded by the `TYRAX_KBD_MOUSE` define the new file introduces.

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

## Real hardware & ps2link

On a real console the same build reads real USB HID devices — nothing to
configure — **as long as the game boots from a disc, USB or the memory card,
not from a ps2link network deploy**. Exception: **network deploys under
ps2link skip the feature by default** (the engine refuses to load a second
`usbd` — ps2link is commonly booted from a USB stick, and a second USB stack
wedges the one already serving it). PCSX2 launches and exported ISOs are
unaffected.

So the supported way to run keyboard/mouse on hardware is `Project > Export
PS2 ISO` (burn it, or boot the ISO/ELF from USB via a loader) — *not* the F6
"Run on PS2" network deploy.

### Why it does nothing over F6 / "Run on PS2"

The generated `main.cpp` detects the ps2link deploy and sets
`IrxLoader::keepIopResident = true`; the engine then computes
`withKbdMouse = loadUsbKbdMouse && !keepIopResident`, which is false, so the
drivers never load, `KbdMouse::init()` never runs and `applyKeyboardMouseInput`
is a no-op. The pad loads separately, so **pad works but keyboard/mouse gives
zero reaction** — that is the guard doing its job, not a bug.

### Keyboard & mouse over ps2link: the TyraX ps2link

Reading logs is the hard part on hardware: a burned ISO has no `host:`
filesystem, so there is no `bin/log.txt`. A ps2link deploy is the opposite —
it forwards the EE console over the network (the editor's *Output* panel /
`ps2client` show `TYRA_LOG` live) — but it is exactly where the feature is off
by default.

Why it can't simply be switched on: `ps2kbd`/`ps2mouse` import `usbd`'s
symbols, and a network-booted ps2link (its device list shows
`tty:(TTY via SMAP UDP)` + `dev9x:` and no USB) carries no `usbd` at all.
Adding the stack to a ps2link that is **already running** doesn't work either —
its IOP is never reset, the keyboard half-comes-up via its iomanX device
(`usbkbd:`) but `PS2MouseInit()` spins forever on a `ps2mouse` RPC server that
never registered, freezing the boot on the Tyra logo.

The fix is to bake `usbd` + `ps2kbd` + `ps2mouse` into **ps2link's own boot**
(on its freshly-reset IOP, `usbd` first), so the drivers and their RPC servers
are resident before any game runs and the game just **reuses** them. Build that
ps2link from [`tools/ps2link-usbhid/`](../tools/ps2link-usbhid/README.md):

```powershell
tools/ps2link-usbhid/build.ps1
```

It clones a pinned ps2link, applies a three-file patch and builds in the
`ps2dev/ps2dev` toolchain image, producing a `ps2link.elf` whose boot screen
reads **“Welcome to TyraX ps2link (USB keyboard + mouse)”**. Flash it onto your
PS2 in place of stock ps2link and plug in a **wired** USB keyboard + mouse.

Then in *Project > Preferences > Build > Keyboard & mouse controls* tick
**Also over ps2link — needs the TyraX ps2link** (stored as
`"keyboardMousePs2Link"`), build and hit F6. The engine loads no USB modules of
its own (a second `usbd` would wedge the resident one) and initialises the
drivers ps2link already has. Watch *Output* / `ps2client`:

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
