#!/usr/bin/env python3
"""Screenshots and synthetic input on a GNOME/Wayland session.

Talks straight to mutter's private D-Bus APIs -- org.gnome.Mutter.ScreenCast for
pixels and org.gnome.Mutter.RemoteDesktop for keyboard/mouse.  Neither asks the
user for permission, unlike the xdg-desktop-portal equivalents, and both work
for native Wayland windows (the editor's GLFW window is one), which is what X11
tools cannot do.

    wayland-control.py shot -o shot.png [--area X,Y,W,H] [--cursor] [--delay S]
    wayland-control.py watch DIR [--auto|--area X,Y,W,H] [--every S]
                                 [--count N|--for S] [--tile W] [--cols N]
                                 [--only-changed PCT] [--idle-stop K]
    wayland-control.py area                  # print the moving rect and exit
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

`watch` is the same session trick applied to TIME: it samples the screen on an
interval off the already-negotiated stream and reports ONE downscaled contact
sheet plus a per-frame changed-pixel table, so watching a game for a minute
costs about as much context as a single screenshot.  `--auto` finds the
emulator's picture by looking for the moving rectangle (there is no per-window
capture on Wayland); the rect is cached in DIR/area.txt and reused.

    wayland-control.py watch shots --auto --every 1 --for 20 --tile 224

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

    def grab(self, area=None, settle=0.4):
        """Newest frame off the live stream as a PIL image (cropped to area)."""
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
        return img

    def shot(self, path, area=None, settle=0.4):
        img = self.grab(area, settle)
        img.save(path)
        print(f"{path} {img.width}x{img.height}")

    def watch(self, outdir, area=None, every=1.0, count=8, tile=256, cols=None,
              sheet="sheet.png", only_changed=0.0, idle_stop=0, idle_below=0.05,
              frames=True, settle=None):
        """Sample the screen on a timer and report ONE contact sheet.

        The point is context cost: N separate screenshots of the emulator are N
        images to read, while one grid of downscaled tiles is a single small
        one, and the printed per-frame diff says which tile is worth opening at
        full resolution afterwards.
        """
        import os
        os.makedirs(outdir, exist_ok=True)
        settle = settle if settle is not None else min(0.15, every / 4.0)
        self._start_pipeline()
        tiles, prev, kept_prev, idle = [], None, None, 0
        t0 = time.monotonic()
        for i in range(count):
            due = t0 + i * every
            now = time.monotonic()
            if now < due:
                time.sleep(due - now)
            img = self.grab(area, settle)
            t = time.monotonic() - t0
            # The reported delta is against the last KEPT frame, so a skipped
            # frame's number explains why it was skipped and the sheet's labels
            # describe the tiles next to each other.
            if kept_prev is None:
                n, share, delta = 0, 0.0, "     -   "
            else:
                n, share = changed_pixels(kept_prev, img)
                delta = f"{share * 100:7.3f}%"
            live = 0.0 if prev is None else changed_pixels(prev, img)[1]
            prev = img
            keep = kept_prev is None or share * 100 >= only_changed
            mark = " " if keep else "-"
            name = f"frame{i:02d}.png"
            if keep and frames:
                img.save(os.path.join(outdir, name))
            if keep:
                tiles.append((img, f"#{i:02d} t={t:5.1f}s d={delta.strip()}"))
                kept_prev = img
            print(f"{mark}#{i:02d} t={t:6.2f}s d={delta} {n:9d}px {name if keep else '(skipped)'}",
                  flush=True)
            if idle_stop:
                idle = idle + 1 if (i and live * 100 < idle_below) else 0
                if idle >= idle_stop:
                    print(f"idle-stop: {idle} frames under {idle_below}% in a row")
                    break
        if not tiles:
            print("no frames kept")
            return
        path = sheet if os.path.isabs(sheet) else os.path.join(outdir, sheet)
        s = contact_sheet(path, tiles, cols=cols, tile_w=tile)
        print(f"{path} {s.width}x{s.height} -- {len(tiles)} tile(s), "
              f"~{token_estimate(s)} tokens (one full frame would be "
              f"~{token_estimate(tiles[0][0])})")

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


# -- watch: a downscaled contact sheet instead of N full screenshots ---------

DIFF_THRESHOLD = 16  # per-pixel luma delta that counts as "changed"


def changed_pixels(a, b):
    """How many pixels differ between two same-size RGB images, and the share."""
    from PIL import ImageChops
    d = ImageChops.difference(a, b).convert("L")
    hist = d.point(lambda v: 255 if v > DIFF_THRESHOLD else 0).histogram()
    n = hist[255]
    return n, n / float(a.width * a.height)


def _bands(profile, floor):
    """Contiguous runs of indices whose value is above floor, longest first."""
    runs, start = [], None
    for i, v in enumerate(profile):
        if v > floor and start is None:
            start = i
        elif v <= floor and start is not None:
            runs.append((start, i - 1))
            start = None
    if start is not None:
        runs.append((start, len(profile) - 1))
    runs.sort(key=lambda r: r[1] - r[0], reverse=True)
    return runs


def detect_area(desk, samples=4, gap=0.5):
    """Find the moving rectangle on screen -- the emulator's render area.

    Mutter gives no per-window capture (see the skill), so instead of a window
    id we use the one thing that is true of a game being DRIVEN and of nothing
    else on an idle desktop: its pixels change.  The widest/tallest contiguous
    band of moving columns/rows is the picture; PCSX2's status bar (the FPS
    readout) is a separate, much thinner band and loses.

    This only works while something on screen actually moves.  A parked camera
    over flat terrain, a paused emulator or a slow day-night gradient can all
    come in under the threshold -- hence the plausibility check, which fails
    loudly instead of handing back the FPS counter's bounding box.
    """
    frames = []
    for i in range(samples):
        if i:
            time.sleep(gap)
        frames.append(desk.grab(settle=0.1))
    from PIL import ImageChops
    acc = None
    for a, b in zip(frames, frames[1:]):
        d = ImageChops.difference(a, b).convert("L").point(
            lambda v: 255 if v > DIFF_THRESHOLD else 0)
        acc = d if acc is None else ImageChops.lighter(acc, d)
    w, h = acc.size
    px = acc.load()
    rows = [sum(1 for x in range(0, w, 2) if px[x, y]) for y in range(h)]
    cols = [sum(1 for y in range(0, h, 2) if px[x, y]) for x in range(w)]
    rb, cb = _bands(rows, 2), _bands(cols, 2)
    if rb and cb:
        y0, y1 = rb[0]
        x0, x1 = cb[0]
        aw, ah = x1 - x0 + 1, y1 - y0 + 1
        share = (aw * ah) / float(w * h)
        if share >= 0.02 and 0.9 <= aw / float(ah) <= 2.6:
            return (x0, y0, aw, ah)
        got = f"{x0},{y0},{aw},{ah} ({share * 100:.2f}% of the screen)"
    else:
        got = "nothing"
    raise SystemExit(
        "detect_area: no plausible moving rectangle -- found " + got + ".\n"
        "Nothing is moving enough (paused emulator, parked camera, or a change\n"
        "too slow for the threshold).  Take one `shot`, read the render area off\n"
        "it and pass --area X,Y,W,H; watch caches it in DIR/area.txt.")


def fit_aspect(area, aspect, screen):
    """Grow a rect to a known aspect, anchored at its bottom edge.

    detect_area returns what MOVES, which on a game frame is the ground and not
    the static sky above it.  The PS2 picture has a known shape (512x448 comes
    out aspect-corrected to 4:3), and its bottom edge is where the terrain
    scrolls, so growing upward from there recovers the whole frame.
    """
    x, y, w, h = area
    sw, sh = screen
    if w / float(h) > aspect:
        nw, nh = w, int(round(w / aspect))
    else:
        nw, nh = int(round(h * aspect)), h
    nw, nh = min(nw, sw), min(nh, sh)
    nx = int(round(x + w / 2.0 - nw / 2.0))
    ny = y + h - nh
    return (max(0, min(nx, sw - nw)), max(0, min(ny, sh - nh)), nw, nh)


def parse_aspect(s):
    if ":" in s:
        a, b = s.split(":")
        return float(a) / float(b)
    return float(s)


def trim_black(img, area, floor=10):
    """Shrink an area to the non-black content inside it (PCSX2's letterbox).

    Deterministic, unlike detect_area -- it needs no motion, only the black
    bars PCSX2 pads the 4:3 picture with.  Returns a screen-space rect so the
    same crop can be applied to every later frame.
    """
    g = img.convert("L")
    box = g.point(lambda v: 255 if v > floor else 0).getbbox()
    if not box:
        return area
    x, y = (area[0], area[1]) if area else (0, 0)
    return (x + box[0], y + box[1], box[2] - box[0], box[3] - box[1])


def contact_sheet(path, tiles, cols=None, tile_w=256, bg=(24, 24, 24)):
    """Lay labelled thumbnails out in a grid -- ONE image for N moments."""
    from PIL import Image, ImageDraw, ImageFont
    n = len(tiles)
    cols = cols or min(n, max(1, int(n ** 0.5 + 0.999)))
    rows = (n + cols - 1) // cols
    tw = tile_w
    th = max(1, round(tiles[0][0].height * tw / tiles[0][0].width))
    pad, lab = 6, 16
    sheet = Image.new("RGB", (cols * tw + (cols + 1) * pad,
                             rows * (th + lab) + (rows + 1) * pad), bg)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11)
    except OSError:
        font = ImageFont.load_default()
    draw = ImageDraw.Draw(sheet)
    for i, (img, label) in enumerate(tiles):
        c, r = i % cols, i // cols
        x = pad + c * (tw + pad)
        y = pad + r * (th + lab + pad)
        sheet.paste(img.resize((tw, th), Image.LANCZOS), (x, y))
        draw.text((x + 1, y + th + 2), label, fill=(210, 210, 210), font=font)
    sheet.save(path)
    return sheet


def token_estimate(img):
    """Roughly what an image costs in a Claude context window."""
    w, h = img.size
    if max(w, h) > 1568:  # the API downscales past this
        s = 1568.0 / max(w, h)
        w, h = w * s, h * s
    return int(w * h / 750)


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
    elif op == "watch":
        import os
        p = argparse.ArgumentParser(prog="watch", add_help=False)
        p.add_argument("dir", nargs="?", default="watch")
        p.add_argument("--area")
        p.add_argument("--auto", action="store_true")
        p.add_argument("--every", type=float, default=1.0)
        p.add_argument("--count", type=int, default=8)
        p.add_argument("--for", dest="secs", type=float)
        p.add_argument("--tile", type=int, default=256)
        p.add_argument("--cols", type=int)
        p.add_argument("--sheet", default="sheet.png")
        p.add_argument("--only-changed", dest="only", type=float, default=0.0)
        p.add_argument("--idle-stop", dest="idle", type=int, default=0)
        p.add_argument("--idle-below", dest="idle_below", type=float, default=0.05)
        p.add_argument("--no-frames", dest="frames", action="store_false")
        p.add_argument("--trim", action="store_true")
        p.add_argument("--aspect")
        p.add_argument("--settle", type=float)
        a = p.parse_args(rest)
        cache = os.path.join(a.dir, "area.txt")
        origin = None
        if a.auto or a.area == "auto":
            area, origin = detect_area(desk), "detected"
            if a.aspect:
                area = fit_aspect(area, parse_aspect(a.aspect), desk.grab(settle=0.1).size)
                origin = f"detected, fitted to {a.aspect}"
        elif a.area:
            area = parse_area(a.area)
        elif os.path.exists(cache):
            area, origin = parse_area(open(cache).read().strip()), "cached"
        else:
            area = None
        if a.trim:
            area, origin = trim_black(desk.grab(area, 0.2), area), "trimmed"
        if origin != "cached":
            os.makedirs(a.dir, exist_ok=True)
            if area:
                open(cache, "w").write(",".join(str(v) for v in area))
        if area:
            print(f"area {','.join(str(v) for v in area)} ({origin or 'given'})")
        count = a.count if a.secs is None else max(1, int(a.secs / a.every) + 1)
        desk.watch(a.dir, area, a.every, count, a.tile, a.cols, a.sheet,
                   a.only, a.idle, a.idle_below, a.frames, a.settle)
    elif op == "area":
        area = detect_area(desk)
        print(",".join(str(v) for v in area))
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
