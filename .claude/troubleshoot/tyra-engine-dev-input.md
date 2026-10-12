# troubleshoot/tyra-engine-dev-input.md

USB keyboard/mouse input, the virtual pad overlay, pad rumble and the input
recorder's replace semantics. Symptom first.

## Actuator rumble

**Cause:** the pad's act-direct DualShock rumble — the on/off buzz motor + 0-255 heavy
motor.
**Fix:** `Pad::setActuators`, behind the Vibrate Pad flow node / `padVibrate()` script
helper.

## USB keyboard/mouse input

**Cause:** TyraX adds `pad/kbd_mouse.*` — `Engine::kbdMouse` polls the PS2SDK `ps2kbd`
(raw mode, 256-bit HID-code bitmap) and `ps2mouse` (DIFF mode: per-frame deltas + button
mask) drivers, loaded by the IrxLoader behind `EngineOptions::loadUsbKbdMouse`.
**Fix:** `Pad::injectVirtual` overlays a virtual pad — held buttons OR into the polled
state, click edges derived from the previous overlay, stick offsets clamped — which is how
generated games map keys onto the pad. It takes an overlay **slot** (`Pad::VIRT_SLOTS`, one
`virtPrev` per slot): 0 is this keyboard/mouse fold, 1 is the editor's Remote Pad
(docs/remote-pad.md). **A second source must never reuse slot 0** — the two would each read
as the other releasing everything, so every held button re-clicks every frame.

## `setState` REPLACES the polled state (input recorder)

**Cause:** `Pad::setState` and `KbdMouse::setState` are the OTHER shape and exist for the
input recorder (docs/input-replay.md): they REPLACE the polled state instead of merging
into it, because a recording has to win over whatever a physical controller is doing.
**Fix:** two details are load-bearing. `Pad::setState` re-seeds **every** `virtPrev` slot
from the state it wrote, or the first frame after a replay ends fabricates click edges out
of the difference. And `KbdMouse` grew a `forced` flag that `isEnabled()` ORs in, because
every reader in a generated game gates on it — without it a replayed keystroke is silently
dropped on any machine with no USB keyboard attached.

## A boot freeze on the Tyra logo under ps2link

**Cause:** ps2kbd/ps2mouse import usbd's symbols and drivers added to an already-running
ps2link's un-reset IOP never come up cleanly (PS2MouseInit then spins forever on an RPC
server that never registered). So USB input is **skipped under ps2link**.
**Fix:** the `loadUsbKbdMouseUnderPs2Link` option instead targets the **TyraX ps2link**
(`tools/ps2link` — bakes usbd+ps2kbd+ps2mouse into ps2link's OWN boot; it is the ONLY
ps2link the editor deploys to, so the option is on by default — docs/ps2link-setup.md): the
engine then loads NO USB modules and reuses that resident stack.
`KbdMouse::init(underPs2Link)` guards PS2MouseInit on the keyboard device having opened, so
running it against a stock ps2link logs instead of hanging.

## Real-hardware USB traps

**Cause:** two traps found the hard way. PS2MouseData must be zero-initialised (a real
mouse sends no packet on a still frame, so garbage read as a constant delta spins the
camera — PCSX2 never shows it) and the read mode is set to DIFF explicitly.
**Fix:** the IrxLoader gives HID a fixed settle delay after load since USB enumeration is
async on real hardware but instant in PCSX2. **Hardware-unconfirmed** beyond "drivers
ready" — the test devices didn't speak USB HID boot protocol.
