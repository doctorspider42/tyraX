#!/usr/bin/env python3
"""Screenshots and synthetic input on a GNOME/Wayland session.

Talks straight to mutter's private D-Bus APIs -- org.gnome.Mutter.ScreenCast for
pixels and org.gnome.Mutter.RemoteDesktop for keyboard/mouse.  Neither asks the
user for permission, unlike the xdg-desktop-portal equivalents, and both work
for native Wayland windows (the editor's GLFW window is one), which is what X11
tools cannot do.

    wayland-control.py shot -o shot.png [--area X,Y,W,H] [--cursor] [--delay S]
    wayland-control.py move X Y
    wayland-control.py movrel DX DY          # for pointer-locked clients
    wayland-control.py button left down|up   # held drags built by hand
    wayland-control.py click [left|right|middle] [--at X,Y] [--double]
    wayland-control.py drag X1 Y1 X2 Y2 [--button left]
    wayland-control.py scroll up|down|left|right [--steps N] [--at X,Y]
    wayland-control.py key ctrl+n            # or: Return, Escape, F5, a
    wayland-control.py keydown w             # held key; keyup w releases it
    wayland-control.py type "some text"
    wayland-control.py script FILE|-         # one line per command, ONE session

`script` is the mode that matters: every other form creates a fresh mutter
session, and a session dies with the process, so a sequence of one-shot calls
re-negotiates PipeWire every time (~0.6 s each) and drops pointer state in
between.  A script runs the whole interaction in a single session:

    move 93 79
    click
    sleep 0.3
    shot menu.png
    key Escape

Requires python3-gi and gstreamer1.0-pipewire (both are on a stock Ubuntu
GNOME).  Coordinates are global screen pixels, matching a full-screen `shot`.
"""

import argparse
import shlex
import sys
import time

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gio, GLib, Gst  # noqa: E402

SC = "org.gnome.Mutter.ScreenCast"
RD = "org.gnome.Mutter.RemoteDesktop"

# evdev button codes -- NotifyPointerButton speaks evdev, not X11 button numbers.
BUTTONS = {"left": 0x110, "right": 0x111, "middle": 0x112}

# X11 keysyms.  Printable ASCII maps to its own code point, so only the
# non-printables need a table.
KEYSYMS = {
    "backspace": 0xFF08, "tab": 0xFF09, "return": 0xFF0D, "enter": 0xFF0D,
    "escape": 0xFF1B, "esc": 0xFF1B, "space": 0x0020, "delete": 0xFFFF,
    "home": 0xFF50, "left": 0xFF51, "up": 0xFF52, "right": 0xFF53,
    "down": 0xFF54, "pageup": 0xFF55, "pagedown": 0xFF56, "end": 0xFF57,
    "insert": 0xFF63, "menu": 0xFF67,
    "shift": 0xFFE1, "ctrl": 0xFFE3, "control": 0xFFE3, "alt": 0xFFE9,
    "super": 0xFFEB, "meta": 0xFFEB,
}
KEYSYMS.update({f"f{i}": 0xFFBD + i for i in range(1, 13)})


def keysym(name):
    if len(name) == 1:
        return ord(name)
    low = name.lower()
    if low in KEYSYMS:
        return KEYSYMS[low]
    if low.startswith("0x"):
        return int(low, 16)
    raise SystemExit(f"unknown key name: {name}")


class Desk:
    """One mutter remote-desktop session with a linked screencast stream."""

    def __init__(self, monitor=None, cursor=False):
        Gst.init(None)
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        self.pipeline = None

        self.rd = self._call(RD, "/org/gnome/Mutter/RemoteDesktop", RD,
                             "CreateSession")[0]
        rd_id = self._call(RD, self.rd, "org.freedesktop.DBus.Properties", "Get",
                           GLib.Variant("(ss)", (RD + ".Session", "SessionId")))[0]

        self.sc = self._call(SC, "/org/gnome/Mutter/ScreenCast", SC, "CreateSession",
                             GLib.Variant("(a{sv})", ({
                                 "remote-desktop-session-id": GLib.Variant("s", rd_id),
                             },)))[0]

        # Always record the whole monitor: --area is a crop, so pointer
        # coordinates and screenshot coordinates stay in one global space.
        self.stream = self._call(
            SC, self.sc, SC + ".Session", "RecordMonitor",
            GLib.Variant("(sa{sv})", (monitor or self._first_monitor(), {
                "cursor-mode": GLib.Variant("u", 1 if cursor else 0),
            })))[0]

        node = {}
        loop = GLib.MainLoop()

        def on_signal(_c, _s, _o, _i, name, params):
            if name == "PipeWireStreamAdded":
                node["id"] = params[0]
                loop.quit()

        self.bus.signal_subscribe(SC, SC + ".Stream", "PipeWireStreamAdded",
                                  self.stream, None, 0, on_signal)
        self._call(RD, self.rd, RD + ".Session", "Start")
        GLib.timeout_add_seconds(5, loop.quit)
        loop.run()
        if "id" not in node:
            raise SystemExit("mutter never published a PipeWire stream")
        self.node = node["id"]

    # -- plumbing ---------------------------------------------------------
    def _call(self, dest, path, iface, method, params=None):
        return self.bus.call_sync(dest, path, iface, method, params, None, 0, -1, None)

    def _first_monitor(self):
        r = self._call("org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig",
                       "org.gnome.Mutter.DisplayConfig", "GetCurrentState")
        return r[1][0][0][0]

    def _rd(self, method, params=None):
        self._call(RD, self.rd, RD + ".Session", method, params)

    def close(self):
        if self.pipeline:
            self.pipeline.set_state(Gst.State.NULL)
        try:
            self._rd("Stop")
        except GLib.Error:
            pass

    # -- capture ----------------------------------------------------------
    def _start_pipeline(self):
        if self.pipeline:
            return
        self.pipeline = Gst.parse_launch(
            f"pipewiresrc path={self.node} always-copy=true ! videoconvert ! "
            "video/x-raw,format=RGBA ! appsink name=sink max-buffers=2 drop=true sync=false")
        self.sink = self.pipeline.get_by_name("sink")
        self.pipeline.set_state(Gst.State.PLAYING)

    def shot(self, path, area=None, settle=0.4):
        from PIL import Image
        self._start_pipeline()
        # Mutter only pushes a buffer when something changed, so take whatever
        # arrives during the settle window and keep the newest.
        deadline = GLib.get_monotonic_time() + int(settle * 1e6)
        hard = deadline + 5_000_000
        last = None
        while True:
            sample = self.sink.emit("try-pull-sample", 200 * Gst.MSECOND)
            if sample is not None:
                last = sample
            now = GLib.get_monotonic_time()
            if last is not None and now > deadline:
                break
            if now > hard:
                raise SystemExit("no frames from the screencast stream")
        caps = last.get_caps().get_structure(0)
        w, h = caps.get_value("width"), caps.get_value("height")
        buf = last.get_buffer()
        ok, info = buf.map(Gst.MapFlags.READ)
        img = Image.frombytes("RGBA", (w, h), info.data, "raw", "RGBA", w * 4).convert("RGB")
        buf.unmap(info)
        if area:
            x, y, aw, ah = area
            img = img.crop((x, y, x + aw, y + ah))
        img.save(path)
        print(f"{path} {img.width}x{img.height}")

    # -- input ------------------------------------------------------------
    def move(self, x, y):
        self._rd("NotifyPointerMotionAbsolute",
                 GLib.Variant("(sdd)", (self.stream, float(x), float(y))))

    def move_rel(self, dx, dy):
        # The only motion a pointer-locked client sees (PCSX2 with mouse
        # capture on, or a game grabbing the cursor); absolute motion is
        # swallowed by the lock.
        self._rd("NotifyPointerMotionRelative", GLib.Variant("(dd)", (float(dx), float(dy))))

    def button(self, name, state):
        self._rd("NotifyPointerButton", GLib.Variant("(ib)", (BUTTONS[name], state)))

    def click(self, name="left", at=None, double=False):
        if at:
            self.move(*at)
            time.sleep(0.05)
        for _ in range(2 if double else 1):
            self.button(name, True)
            time.sleep(0.03)
            self.button(name, False)
            time.sleep(0.05)

    def drag(self, x1, y1, x2, y2, name="left", steps=20):
        self.move(x1, y1)
        time.sleep(0.05)
        self.button(name, True)
        for i in range(1, steps + 1):
            self.move(x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps)
            time.sleep(0.01)
        time.sleep(0.05)
        self.button(name, False)

    def scroll(self, direction, steps=3, at=None):
        if at:
            self.move(*at)
        axis, sign = (0, 1) if direction in ("down", "up") else (1, 1)
        if direction in ("up", "left"):
            sign = -1
        for _ in range(steps):
            self._rd("NotifyPointerAxisDiscrete", GLib.Variant("(ui)", (axis, sign)))
            time.sleep(0.02)

    def key(self, combo):
        parts = combo.split("+") if combo != "+" else ["+"]
        syms = [keysym(p) for p in parts]
        for s in syms:
            self._rd("NotifyKeyboardKeysym", GLib.Variant("(ub)", (s, True)))
            time.sleep(0.02)
        for s in reversed(syms):
            self._rd("NotifyKeyboardKeysym", GLib.Variant("(ub)", (s, False)))
            time.sleep(0.02)

    def key_state(self, name, state):
        # Held keys are how a pad direction is tested: `keydown Up`, sleep,
        # `keyup Up` walks the player for a measurable distance.
        self._rd("NotifyKeyboardKeysym", GLib.Variant("(ub)", (keysym(name), state)))

    def type_text(self, text, delay=0.012):
        for ch in text:
            sym = 0xFF0D if ch == "\n" else ord(ch)
            self._rd("NotifyKeyboardKeysym", GLib.Variant("(ub)", (sym, True)))
            time.sleep(delay)
            self._rd("NotifyKeyboardKeysym", GLib.Variant("(ub)", (sym, False)))
            time.sleep(delay)


def parse_area(s):
    return tuple(int(v) for v in s.replace("x", ",").split(","))


def run_op(desk, argv):
    op = argv[0]
    rest = argv[1:]
    if op == "shot":
        p = argparse.ArgumentParser(prog="shot", add_help=False)
        p.add_argument("out", nargs="?", default="shot.png")
        p.add_argument("-o", "--out", dest="out2")
        p.add_argument("--area")
        p.add_argument("--delay", type=float, default=0.0)
        p.add_argument("--settle", type=float, default=0.4)
        a = p.parse_args(rest)
        if a.delay:
            time.sleep(a.delay)
        desk.shot(a.out2 or a.out, parse_area(a.area) if a.area else None, a.settle)
    elif op == "move":
        desk.move(float(rest[0]), float(rest[1]))
    elif op == "movrel":
        desk.move_rel(float(rest[0]), float(rest[1]))
    elif op == "button":
        desk.button(rest[0], rest[1] in ("down", "1", "true"))
    elif op == "click":
        p = argparse.ArgumentParser(prog="click", add_help=False)
        p.add_argument("button", nargs="?", default="left", choices=list(BUTTONS))
        p.add_argument("--at")
        p.add_argument("--double", action="store_true")
        a = p.parse_args(rest)
        desk.click(a.button, parse_area(a.at) if a.at else None, a.double)
    elif op == "drag":
        p = argparse.ArgumentParser(prog="drag", add_help=False)
        p.add_argument("coords", nargs=4, type=float)
        p.add_argument("--button", default="left", choices=list(BUTTONS))
        a = p.parse_args(rest)
        desk.drag(*a.coords, name=a.button)
    elif op == "scroll":
        p = argparse.ArgumentParser(prog="scroll", add_help=False)
        p.add_argument("direction", choices=["up", "down", "left", "right"])
        p.add_argument("--steps", type=int, default=3)
        p.add_argument("--at")
        a = p.parse_args(rest)
        desk.scroll(a.direction, a.steps, parse_area(a.at) if a.at else None)
    elif op == "key":
        for combo in rest:
            desk.key(combo)
            time.sleep(0.05)
    elif op in ("keydown", "keyup"):
        desk.key_state(rest[0], op == "keydown")
    elif op == "type":
        desk.type_text(" ".join(rest))
    elif op == "sleep":
        time.sleep(float(rest[0]))
    else:
        raise SystemExit(f"unknown command: {op}")


def main():
    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return
    cursor = "--cursor" in argv
    argv = [a for a in argv if a != "--cursor"]
    monitor = None
    if "--monitor" in argv:
        i = argv.index("--monitor")
        monitor = argv[i + 1]
        del argv[i:i + 2]

    if argv[0] == "script":
        src = sys.stdin if len(argv) < 2 or argv[1] == "-" else open(argv[1])
        lines = [shlex.split(l) for l in src if l.strip() and not l.lstrip().startswith("#")]
    else:
        lines = [argv]

    desk = Desk(monitor=monitor, cursor=cursor)
    try:
        for op in lines:
            run_op(desk, op)
    finally:
        desk.close()


if __name__ == "__main__":
    main()
