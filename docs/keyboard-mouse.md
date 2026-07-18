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
configure. Exception: **network deploys under ps2link skip the feature**
(the engine refuses to load a second `usbd` — ps2link is commonly booted
from a USB stick, and a second USB stack wedges the one already serving it).
PCSX2 launches and exported ISOs are unaffected.
