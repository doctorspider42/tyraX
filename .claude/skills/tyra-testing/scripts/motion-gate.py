#!/usr/bin/env python3
"""The MOTION gate: does the picture survive being MOVED?

Every other automated check on this repo freezes the camera.  That is what
makes them reproducible and it is exactly what makes them blind: four defects
on the upscaler branch were found by a person and by nothing else, and every
one of them lives only while the picture is moving (see the tyra-testing skill,
"The motion gate").

This is the analysis half.  The capture half is `motion-gate.ps1` (Windows);
either writes the same burst directory, so the numbers mean the same thing:

    <run>/hold/burst.json + frame000.bin ...   raw BGRA, w*h*4 per capture
    <run>/pan/  ...
    <run>/dolly/ ...
    <run>/return/ ...

Usage:

    python motion-gate.py <runDir> [--baseline <otherRunDir>] [options]
    python motion-gate.py <runDir>/pan          # one leg on its own

--------------------------------------------------------------------------
THE HARD PART, AND HOW IT IS SOLVED

A gate that runs the camera has to answer one question before it can answer any
other: *is this frame different from the last one because the scene moved, or
because the picture is broken?*  On `examples/upscaler-lab` the running
emitters contribute ~1.04/255 of frame-to-frame change on their own - LARGER
than the 0.77/255 artefact the parked stability gate hunts - and a walking
camera moves every pixel in the frame.  "% of pixels that changed" is not a
signal here, it is a description of the route.

So the statistic is not the difference between two captures.  It is the part of
that difference that A RIGID MOVE OF THE WHOLE PICTURE CANNOT EXPLAIN:

    d       the global displacement between two captures, by phase correlation
    subpx   the sub-pixel part of that displacement
    raw     mean |A - B|                       ... dominated by the route
    mc      mean |shift(A, round(d)) - B|      ... what the move did not explain
    mc/raw  the UNEXPLAINED FRACTION - scale-free, so it does not care how much
            time passed between the two captures or how far the camera went

Legitimate camera motion is a near-rigid move of the whole frame, so it lands
in `d` and leaves `mc` small.  Parallax, disocclusion and the emitters put a
floor under `mc`, but that floor grows with the amount of change, which is
exactly what dividing by `raw` removes.  Every defect this gate exists for is
something a rigid move cannot express:

  * a sub-pixel BOB (the jitter shake) is a half-pixel displacement of the
    whole picture ON TOP of the route.  With the camera parked it is the
    ENTIRE displacement, so `subpx` reads it directly, in pixels, and needs no
    threshold argument at all; in motion it inflates `mc/raw`.
  * a TORN frame is two different displacements in one picture.  Correlating
    horizontal BANDS separately and comparing them against the whole-frame `d`
    finds it; a whole-frame estimate averages the tear away.
  * a BLACK frame is a luma collapse.  No statistic needed - say it.
  * STREAKING on a grazing surface is spatial, not temporal, so every residual
    here is also reported PER BAND: "the ground band is 6x the sky band" is the
    shape that artefact makes.

None of this needs the two runs to be frame-aligned, and that is deliberate.
The route is frame-indexed in an object script - `--pad`'s driver refreshes at
25 Hz off the HOST clock, so a stick lands at a different frame offset in every
run - but the CAPTURES are not frame-locked to anything: they are taken as fast
as the tool will go, on purpose, because an even stride is precisely how the
jitter shake survived its first harness.  Comparing capture k of one run
against capture k of another would therefore report the sampler's own phase as
a finding.  Runs are compared as DISTRIBUTIONS over the same route, per leg.

Exit code 0 = nothing flagged, 1 = something flagged, 2 = the burst is unusable.
"""

import argparse
import json
import math
import os
import sys

import numpy as np

LEG_ORDER = ["hold", "pan", "dolly", "return"]

# ---------------------------------------------------------------- loading ---


def load_burst(d):
    # utf-8-sig: PowerShell 5.1's `Set-Content -Encoding utf8` writes a BOM and
    # json.load dies on it with "Expecting value: line 1 column 1".
    with open(os.path.join(d, "burst.json"), "r", encoding="utf-8-sig") as f:
        return json.load(f)


def collect(run):
    """A run is either one burst directory or a directory of leg bursts."""
    if os.path.exists(os.path.join(run, "burst.json")):
        return [(load_burst(run).get("legName") or os.path.basename(run), run)]
    out = []
    for name in sorted(os.listdir(run),
                       key=lambda n: (LEG_ORDER.index(n) if n in LEG_ORDER else 99, n)):
        d = os.path.join(run, name)
        if os.path.exists(os.path.join(d, "burst.json")):
            out.append((load_burst(d).get("legName") or name, d))
    return out


def load_luma(path, w, h, box):
    """Raw BGRA -> float32 luma, cropped to `box` (x, y, w, h).

    The crop is taken ONCE and passed in, never recomputed per frame.  A crop
    that follows the content re-registers a picture that slid by a line into an
    identical image, which makes a whole-frame displacement invisible BY
    CONSTRUCTION - the same reason the parked gate forbids -Trim.
    """
    a = np.fromfile(path, dtype=np.uint8)
    if a.size < w * h * 4:
        raise SystemExit("short frame file: %s (%d bytes, want %d)" %
                         (path, a.size, w * h * 4))
    a = a[:w * h * 4].reshape(h, w, 4).astype(np.float32)
    y = (299.0 * a[:, :, 2] + 587.0 * a[:, :, 1] + 114.0 * a[:, :, 0]) / 1000.0
    x0, y0, cw, ch = box
    return np.ascontiguousarray(y[y0:y0 + ch, x0:x0 + cw])


def content_box(path, w, h, floor=10.0, cover=0.25):
    """The PICTURE inside the render area, from ONE frame.

    PCSX2 pads the 4:3 picture with black bars; they are constant, so they only
    dilute the means.  A plain bounding box of everything brighter than `floor`
    does NOT find the picture, though - PCSX2 draws its own FPS readout in the
    black bar, and one line of thin white text drags the box back over the
    letterbox and into the noise the crop exists to exclude.  So a row or column
    counts only when a real FRACTION of it is lit (`cover`): a picture row is
    almost entirely above the floor, an OSD line is a few per cent of one.

    Computed once, from the first frame of the FIRST leg of the FIRST arm, and
    then reused verbatim everywhere else (--box), so every number in the report
    describes the same rectangle.
    """
    a = np.fromfile(path, dtype=np.uint8)[:w * h * 4].reshape(h, w, 4).astype(np.int32)
    y = (299 * a[:, :, 2] + 587 * a[:, :, 1] + 114 * a[:, :, 0]) // 1000
    lit = y > floor
    rows = np.where(lit.mean(axis=1) >= cover)[0]
    cols = np.where(lit.mean(axis=0) >= cover)[0]
    if rows.size == 0 or cols.size == 0:
        return (0, 0, w, h)
    return (int(cols[0]), int(rows[0]),
            int(cols[-1] - cols[0] + 1), int(rows[-1] - rows[0] + 1))


# ------------------------------------------------------------ correlation ---


def phase_shift(a, b, max_shift):
    """Displacement that best takes `a` onto `b`: (dy, dx, sy, sx, sharp).

    Phase correlation: two images that differ by a translation have a
    cross-power spectrum that is a pure phase ramp, whose inverse transform is
    a spike at the shift.  O(n log n) instead of O(n * search^2), immune to a
    brightness difference, and - the part that matters here - it degrades into
    a BROAD, LOW peak rather than lying when the two frames are not related by
    a translation at all.  `sharp` (peak over the mean of the search window) is
    that confession, and every consumer below is gated on it: a band of empty
    sky correlates on noise and will otherwise report a confident 19-pixel
    displacement, which reads exactly like a torn frame.

    (sy, sx) is the sub-pixel part, from a parabola through the peak's
    neighbours.  It is REPORTED, never applied: `mc` compensates only the
    INTEGER displacement on purpose, so that a sub-pixel bob stays in the
    residual instead of being absorbed by the estimator that is meant to
    measure it.
    """
    a = a - a.mean()
    b = b - b.mean()
    # A window kills the wrap-around edge energy that otherwise plants a false
    # peak at (0,0) on a moving picture.
    wy = np.hanning(a.shape[0])[:, None]
    wx = np.hanning(a.shape[1])[None, :]
    fa = np.fft.rfft2(a * wy * wx)
    fb = np.fft.rfft2(b * wy * wx)
    r = fa.conj() * fb
    m = np.abs(r)
    m[m < 1e-9] = 1e-9
    c = np.fft.irfft2(r / m, s=a.shape)
    h, w = c.shape
    ms = min(int(max_shift), h // 2 - 2, w // 2 - 2)
    # Only shifts inside +-max_shift are plausible for one capture interval;
    # a larger "match" is the correlator finding a repeat of the texture.
    corners = np.concatenate([
        c[:ms + 1, :ms + 1].ravel(), c[:ms + 1, w - ms:].ravel(),
        c[h - ms:, :ms + 1].ravel(), c[h - ms:, w - ms:].ravel()])
    win = np.full_like(c, -1.0)
    win[:ms + 1, :ms + 1] = c[:ms + 1, :ms + 1]
    win[:ms + 1, w - ms:] = c[:ms + 1, w - ms:]
    win[h - ms:, :ms + 1] = c[h - ms:, :ms + 1]
    win[h - ms:, w - ms:] = c[h - ms:, w - ms:]
    idx = int(np.argmax(win))
    py, px = idx // w, idx % w
    peak = float(win[py, px])
    floor = float(np.abs(corners).mean())
    sharp = peak / max(floor, 1e-12)
    dy = py - h if py > h // 2 else py
    dx = px - w if px > w // 2 else px

    def sub(c1, c0, c2):
        den = c1 - 2.0 * c0 + c2
        return 0.0 if abs(den) < 1e-12 else 0.5 * (c1 - c2) / den
    sy = sub(c[(py - 1) % h, px], c[py, px], c[(py + 1) % h, px])
    sx = sub(c[py, (px - 1) % w], c[py, px], c[py, (px + 1) % w])
    return dy, dx, float(np.clip(sy, -1, 1)), float(np.clip(sx, -1, 1)), sharp


def band_step(bshift):
    """How much one band's displacement JUMPS away from its neighbours'.

    A tear is a DISCONTINUITY - the frame holds two pictures from different
    moments, with a seam between them.  Forward motion is not: a dolly shows a
    smooth RAMP of band displacements (measured on the reference arm: dy of
    0,0,0,0,+3,+6 from sky to ground, every frame), and a detector that only
    looks at "do the bands disagree with the whole frame" calls every forward
    step a tear.  So the statistic is the largest adjacent-band difference in
    EXCESS of the typical one, which a ramp leaves at ~0 and a seam does not.

    Returns (excess_px, band_index_of_the_step).
    """
    good = [(k, dy, dx) for k, (dy, dx, ok) in enumerate(bshift) if ok]
    if len(good) < 3:
        return 0.0, -1
    diffs = []
    for (k0, y0, x0), (k1, y1, x1) in zip(good, good[1:]):
        if k1 != k0 + 1:
            continue     # a gap: no adjacency to measure
        diffs.append((math.hypot(y1 - y0, x1 - x0), k1))
    if len(diffs) < 2:
        return 0.0, -1
    mags = sorted(v for v, _ in diffs)
    typical = mags[len(mags) // 2]
    worst, band = max(diffs)
    return max(0.0, worst - typical), band


def shifted_absdiff(a, b, dy, dx):
    """mean |shift(a, d) - b| over the overlap only (no wrap, no padding)."""
    h, w = a.shape
    ay0, by0 = (0, dy) if dy >= 0 else (-dy, 0)
    ax0, bx0 = (0, dx) if dx >= 0 else (-dx, 0)
    ph, pw = h - abs(dy), w - abs(dx)
    if ph <= 8 or pw <= 8:
        return float("nan"), None
    av = a[ay0:ay0 + ph, ax0:ax0 + pw]
    bv = b[by0:by0 + ph, bx0:bx0 + pw]
    dif = np.abs(av - bv)
    return float(dif.mean()), dif


# -------------------------------------------------------------- statistics --


def split2(vals):
    """Best two-cluster split of a 1-D sample - the parked gate's test.

    Returns (n0, n1, within, between).  Two BALANCED clusters, near-zero within
    and large between, IS an alternation; the raw spread is not.  A lopsided
    split (39/1) is the algorithm cutting noise and means nothing, which is why
    the balance is printed next to the ratio and both are required.
    """
    v = np.sort(np.asarray(vals, dtype=np.float64))
    n = v.size
    if n < 6:
        return n, 0, 0.0, 0.0
    best = None
    for k in range(2, n - 1):
        lo, hi = v[:k], v[k:]
        wss = lo.var() * lo.size + hi.var() * hi.size
        if best is None or wss < best[0]:
            best = (wss, k, lo, hi)
    _, k, lo, hi = best
    within = math.sqrt((lo.var() * lo.size + hi.var() * hi.size) / n)
    return int(k), int(n - k), within, float(hi.mean() - lo.mean())


# ------------------------------------------------------------------ leg -----


def analyse_leg(d, args, box):
    meta = load_burst(d)
    w, h = meta["w"], meta["h"]
    paths = [os.path.join(d, f["file"]) for f in meta["frames"]]
    if len(paths) < 8:
        print("motion-gate: %s has only %d captures - not a burst" % (d, len(paths)))
        sys.exit(2)
    if box is None:
        box = tuple(meta.get("box") or content_box(paths[0], w, h))
    frames = [load_luma(p, w, h, box) for p in paths]
    ch, cw = frames[0].shape

    nb = args.bands
    edges = [int(round(ch * k / nb)) for k in range(nb + 1)]

    recs = []
    for i in range(len(frames) - 1):
        a, b = frames[i], frames[i + 1]
        raw = float(np.abs(a - b).mean())
        # A capture pair that is byte-identical is the SAMPLER outrunning the
        # game, not a finding, and averaging those zeros into everything else
        # quietly halves every statistic in the table.  Counted, then dropped.
        dup = raw < 1e-6
        dy, dx, sy, sx, sharp = phase_shift(a, b, args.max_shift)
        mc, _ = shifted_absdiff(a, b, dy, dx)
        bmc, bshift = [], []
        for k in range(nb):
            y0, y1 = edges[k], edges[k + 1]
            ba, bb = a[y0:y1], b[y0:y1]
            m, _ = shifted_absdiff(ba, bb, dy, dx)
            bmc.append(m)
            # Half scale for the band pass: a tear is several pixels wide and
            # survives it, while the eight extra correlations per capture pair
            # are what decide whether a four-leg run analyses in one minute or
            # five.  The whole-frame estimate stays at full resolution, because
            # that one is measured to a fraction of a pixel.
            bdy, bdx, _, _, bsharp = phase_shift(ba[::2, ::2], bb[::2, ::2],
                                                 max(4, args.max_shift // 2))
            bdy *= 2
            bdx *= 2
            # A band gets a vote only when its own correlation is a real
            # fraction as confident as the whole frame's.  Measured on the
            # reference arm: the good bands of a panning frame score 0.65-0.85
            # of the global peak and the two that reported a bogus 30 px
            # scored 0.26-0.33.  An absolute floor does not separate them.
            bshift.append((bdy, bdx, bsharp >= args.band_rel * sharp))
        tear, tearband = band_step(bshift)
        m = meta["frames"][i + 1]
        recs.append(dict(
            i=i, t=m["t"], gf=m.get("gameFrame"), file=m["file"], dup=dup,
            dy=dy, dx=dx, subpx=math.hypot(sy, sx), sharp=sharp,
            disp=math.hypot(dy, dx), raw=raw, mc=mc,
            # The displacement the picture ACTUALLY underwent, sub-pixel part
            # included.  `mc` compensates only the integer part, so a bob that
            # happens to land on a whole window pixel - a quarter-raster-pixel
            # jitter upscaled 2.1x nearly does - would be absorbed by the
            # compensation and vanish from `mc`.  On a parked leg this column
            # is the whole measurement: the camera is not moving, so any
            # displacement at all is the picture moving on its own.
            move=math.hypot(dy + sy, dx + sx),
            frac=(mc / raw if raw > 1e-6 else 0.0),
            tear=tear, bmc=bmc, tearband=tearband,
            luma=float(b.mean())))
    return meta, box, recs, edges


def leg_stats(recs, args, name=None):
    live = [r for r in recs if not r["dup"]]
    if len(live) < 3:
        return None
    frac = np.array([r["frac"] for r in live])
    subpx = np.array([r["subpx"] for r in live])
    mc = np.array([r["mc"] for r in live])
    raw = np.array([r["raw"] for r in live])
    disp = np.array([r["disp"] for r in live])
    # Whether the CAMERA is moving is a property of the ROUTE, so it is taken
    # from the leg's name and not from the measurement.  Deriving it from the
    # measurement is a trap with teeth: a parked leg whose picture is bobbing
    # measures as "moving", the gate switches to the moving-leg statistic, and
    # the one finding that needed no argument at all - the camera is parked and
    # the picture is not - is the finding that gets thrown away.
    moving = (name != "hold") if name in LEG_ORDER else float(np.median(disp)) > 1.0
    # In a moving leg the scale-free unexplained fraction is the statistic; on
    # the parked leg it is 1.0 by construction (there is no displacement to
    # explain anything with), so there the sub-pixel displacement IS the
    # measurement - and it is in pixels, which needs no threshold argument.
    move = np.array([r["move"] for r in live])
    key = frac if moving else move
    n0, n1, within, between = split2(key)
    return dict(
        n=len(live), dup=len(recs) - len(live), moving=moving,
        disp=float(np.median(disp)), raw=float(np.median(raw)),
        mc=float(np.median(mc)), frac=float(np.median(frac)),
        move=float(np.median(move)), moveMax=float(move.max()),
        subpx=float(np.median(subpx)), subpxMax=float(subpx.max()),
        split=(n0, n1), within=within, between=between,
        band=[float(np.median([r["bmc"][k] for r in live]))
              for k in range(len(live[0]["bmc"]))],
        luma=float(np.median([r["luma"] for r in live])))


# ------------------------------------------------------------------ main ----


def print_leg(name, d, meta, recs, st, args):
    dts = np.diff([r["t"] for r in recs]) if len(recs) > 2 else np.array([0.0])
    print("-- leg %-7s %s" % (name, d))
    line = ("   %d captures, %.1f Hz, %d duplicate pair(s)" %
            (len(recs) + 1, 1.0 / max(dts.mean(), 1e-6), st["dup"] if st else 0))
    if meta.get("fps"):
        line += ", game %.1f fps (%.2f game frames per capture)" % (
            meta["fps"], dts.mean() * meta["fps"])
    print(line)
    if args.verbose:
        print("     #   t    move  subpx   sharp     raw      mc   mc/raw"
              "   tear   luma")
        for r in recs:
            print("   %3d %5.2f %6.1f %6.2f %7.1f %7.3f %7.3f %7.3f %6.1f %6.1f%s" %
                  (r["i"], r["t"], r["move"], r["subpx"], r["sharp"], r["raw"],
                   r["mc"], r["frac"], r["tear"], r["luma"],
                   "  dup" if r["dup"] else ""))
    if st:
        print("   median move %.2f px (max %.2f), raw %.3f, mc %.3f, "
              "mc/raw %.3f" %
              (st["move"], st["moveMax"], st["raw"], st["mc"], st["frac"]))
        print("   per-band mc (top to bottom): %s" %
              "  ".join("%.3f" % v for v in st["band"]))
        print("   two-cluster split of %s: %d/%d, within %.4f, between %.4f"
              " (%.1fx)" % ("mc/raw" if st["moving"] else "the displacement",
                            st["split"][0], st["split"][1], st["within"],
                            st["between"], st["between"] / max(st["within"], 1e-9)))
    print()


def verdicts(name, recs, st, args):
    flags = []
    luma = np.array([r["luma"] for r in recs])
    med = float(np.median(luma))
    for r in recs:
        if med > 1.0 and r["luma"] < args.black * med:
            flags.append(("BLACK FRAME", name, r,
                          "luma %.1f against a leg median of %.1f" % (r["luma"], med)))
    for r in recs:
        if r["tear"] >= args.tear and not r["dup"]:
            flags.append(("TORN FRAME", name, r,
                          "band %d's displacement jumps %.1f px clear of the "
                          "smooth ramp its neighbours follow (whole frame %d,%d)"
                          " - a seam, not a gradient" %
                          (r["tearband"], r["tear"], r["dx"], r["dy"])))
    if st and st["n"] >= 8:
        bal = min(st["split"]) / float(max(1, sum(st["split"])))
        ratio = st["between"] / max(st["within"], 1e-9)
        # A ratio on its own is meaningless when both numbers are tiny: the
        # reference arm's parked leg splits 54/40 at a between of 0.0004 px,
        # which is 3.1x its within and still nothing at all.  The separation
        # has to be a real quantity in the statistic's own units too.
        floor = args.min_between if st["moving"] else args.min_between_px
        if bal >= args.balance and ratio >= args.ratio and st["between"] >= floor:
            flags.append(("PERIOD-2 (bob)", name, None,
                          "%s splits %d/%d, within %.4f, between %.4f (%.1fx) - "
                          "two balanced clusters is an alternation, not spread" %
                          ("mc/raw" if st["moving"] else "the displacement",
                           st["split"][0], st["split"][1], st["within"],
                           st["between"], ratio)))
    if st and not st["moving"] and st["move"] >= args.subpx:
        flags.append(("PICTURE MOVES", name, None,
                      "the camera is PARKED on this leg and the picture still "
                      "displaces %.2f px per capture pair (max %.2f)" %
                      (st["move"], st["moveMax"])))
    return flags


def still_median(d, meta, box, take=21):
    """A clean still image of a parked pose: the per-pixel median of the burst.

    The median is what removes the emitters - a particle is a minority of the
    samples at any pixel - so two arms parked at the SAME pose (the route is
    frame-indexed, so they are) become directly comparable pictures, with no
    frame alignment and no statistics.
    """
    w, h = meta["w"], meta["h"]
    paths = [os.path.join(d, f["file"]) for f in meta["frames"]]
    step = max(1, len(paths) // take)
    stack = np.stack([load_luma(p, w, h, box) for p in paths[::step][:take]])
    return np.median(stack, axis=0)


def region_report(base, this, args):
    """WHAT STOPPED BEING DRAWN - per tile, on the parked leg, arm against arm.

    This exists because of a specific failure: a full-screen pass left a
    hardcoded ZBUF mask behind, the next frame's clear stamped depth through the
    texture heap, and the visible result was "the terrain is gone".  The clue
    that broke the case open was not the terrain at all - it was that the
    CROSSHAIR was missing too, in an arm whose terrain was untextured.  A
    whole-frame difference averages that away; a tile map does not.

    So: for every tile, how much did it change, and did it LOSE its detail?
    A tile that changed a lot while its local variance collapsed stopped being
    drawn.  A tile that changed a lot and kept its detail merely looks
    different, which is a much weaker finding and is reported as such.
    """
    out = []
    for name in [n for n in LEG_ORDER if n in base and n in this]:
        a, b = base[name], this[name]
        if not a["st"] or not b["st"] or a["st"]["moving"] or b["st"]["moving"]:
            continue    # only a parked pose is comparable between runs
        # ...and only while it really IS parked in both arms.  A bobbing
        # picture smears its own median (every capture at a different
        # sub-pixel offset), local variance drops everywhere, and the tile
        # test then reports the whole textured half of the frame as "gone".
        # Measured: the jitter arm displaces 5.34 px per pair on this leg and
        # produces 41 such tiles, none of which stopped being drawn.
        steady = max(a["st"]["move"], b["st"]["move"])
        if steady > args.tile_steady:
            out.append((name, [], [], [], None, None,
                        "the picture displaces %.2f px per capture pair on this "
                        "leg, so its median is smeared and a detail comparison "
                        "would report the whole textured frame as gone - fix the "
                        "bob first, then ask this question" % steady))
            continue
        ma = still_median(a["dir"], a["meta"], args._box)
        mb = still_median(b["dir"], b["meta"], args._box)
        h, w = ma.shape
        ty, tx = args.tiles_y, args.tiles_x
        gone, appeared, changed = [], [], []
        for j in range(ty):
            for i in range(tx):
                y0, y1 = h * j // ty, h * (j + 1) // ty
                x0, x1 = w * i // tx, w * (i + 1) // tx
                ta, tb = ma[y0:y1, x0:x1], mb[y0:y1, x0:x1]
                diff = float(np.abs(ta - tb).mean())
                da, db = float(ta.std()), float(tb.std())
                if diff < args.tile_diff:
                    continue
                rect = (x0, y0, x1 - x0, y1 - y0)
                if da >= args.tile_detail and db <= args.tile_lost * da:
                    gone.append((rect, diff, da, db))
                elif db >= args.tile_detail and da <= args.tile_lost * db:
                    appeared.append((rect, diff, db, da))
                else:
                    changed.append((rect, diff, da, db))
        out.append((name, gone, appeared, changed, ma, mb, None))
    return out


def dump_pngs(d, meta, box, recs, which, outdir):
    """Write the frames a human should LOOK at, plus the amplified residual.

    A number nobody can go and look at is not an actionable report - and "the
    frame is missing the geometry entirely" versus "the frame drew it wrong" is
    a distinction that threw out two wrong theories in an hour on this branch.
    Only a picture answers that one.
    """
    try:
        from PIL import Image
    except ImportError:
        print("   (PIL not available - no PNGs written)")
        return
    os.makedirs(outdir, exist_ok=True)
    w, h = meta["w"], meta["h"]
    paths = [os.path.join(d, f["file"]) for f in meta["frames"]]
    x0, y0, cw, chh = box
    for i in which:
        for k in (i, i + 1):
            a = np.fromfile(paths[k], dtype=np.uint8)[:w * h * 4].reshape(h, w, 4)
            p = os.path.join(outdir, "at%03d_frame%03d.png" % (i, k))
            Image.fromarray(a[:, :, [2, 1, 0]][y0:y0 + chh, x0:x0 + cw]).save(p)
            print("   %s" % p)
        a = load_luma(paths[i], w, h, box)
        b = load_luma(paths[i + 1], w, h, box)
        r = recs[i]
        _, dif = shifted_absdiff(a, b, r["dy"], r["dx"])
        if dif is not None:
            p = os.path.join(outdir, "at%03d_unexplained_x8.png" % i)
            Image.fromarray(np.clip(dif * 8.0, 0, 255).astype(np.uint8)).save(p)
            print("   %s   (what the move did not explain, amplified 8x)" % p)


def run(runDir, args, box):
    print("== %s" % runDir)
    legs, allflags, data = collect(runDir), [], {}
    if not legs:
        print("motion-gate: no burst found under %s" % runDir)
        sys.exit(2)
    for name, d in legs:
        meta, box, recs, _ = analyse_leg(d, args, box)
        st = leg_stats(recs, args, name)
        print_leg(name, d, meta, recs, st, args)
        allflags += verdicts(name, recs, st, args)
        data[name] = dict(st=st, recs=recs, meta=meta, dir=d)
    return box, data, allflags


def compare(base, this, args):
    """Arm against arm, per leg.  Change ONE knob between the arms and the
    route, the emitters and the reconstruction are common mode: what is left is
    the knob.  This is the honest form of the test - an absolute threshold on
    `mc` would be a threshold on the route."""
    print("== this run against the baseline, per leg")
    print("   leg      base mc  this mc   ratio   base mc/raw this mc/raw  ratio"
          "   base move  this move")
    worse = []
    for name in [n for n in LEG_ORDER if n in base and n in this] + \
                [n for n in this if n not in LEG_ORDER and n in base]:
        a, b = base[name]["st"], this[name]["st"]
        if not a or not b:
            continue
        rm = b["mc"] / max(a["mc"], 1e-9)
        rf = b["frac"] / max(a["frac"], 1e-9)
        print("   %-8s %7.3f %8.3f %7.2fx %11.3f %11.3f %6.2fx %11.2f %10.2f" %
              (name, a["mc"], b["mc"], rm, a["frac"], b["frac"], rf,
               a["move"], b["move"]))
        key = rf if b["moving"] else rm
        if key >= args.arm_ratio:
            worse.append((name, rm, rf, b["moving"]))
    print()
    return worse


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--baseline", default=None,
                    help="the same route with ONE knob changed")
    ap.add_argument("--bands", type=int, default=6)
    ap.add_argument("--max-shift", type=int, default=64)
    ap.add_argument("--box", default=None, help="x,y,w,h - force the crop")
    ap.add_argument("--verbose", action="store_true",
                    help="the per-capture table, not just the leg summary")
    ap.add_argument("--black", type=float, default=0.45,
                    help="a frame under this fraction of the leg's median luma")
    ap.add_argument("--tear", type=float, default=6.0,
                    help="px of band-to-band disagreement that counts as a tear")
    ap.add_argument("--band-rel", type=float, default=0.4,
                    help="a band votes on tearing only when its correlation is "
                         "this fraction as confident as the whole frame's")
    ap.add_argument("--min-between", type=float, default=0.05,
                    help="least cluster separation (in mc/raw) that is a bob")
    ap.add_argument("--min-between-px", type=float, default=0.05,
                    help="least cluster separation (in px) on a parked leg")
    ap.add_argument("--subpx", type=float, default=0.15,
                    help="px of displacement on the PARKED leg that is a finding")
    ap.add_argument("--balance", type=float, default=0.30,
                    help="minimum cluster balance for a period-2 verdict")
    ap.add_argument("--ratio", type=float, default=3.0,
                    help="between/within ratio for a period-2 verdict")
    ap.add_argument("--arm-ratio", type=float, default=1.25,
                    help="per-leg ratio over the baseline that is a finding")
    ap.add_argument("--tiles-x", type=int, default=24)
    ap.add_argument("--tiles-y", type=int, default=18)
    ap.add_argument("--tile-diff", type=float, default=4.0,
                    help="mean |A-B| in a tile below which it did not change")
    ap.add_argument("--tile-detail", type=float, default=3.0,
                    help="local std below which a tile held nothing to lose")
    ap.add_argument("--tile-steady", type=float, default=0.5,
                    help="px of bob on the parked leg above which the tile "
                         "comparison refuses to answer")
    ap.add_argument("--tile-lost", type=float, default=0.4,
                    help="fraction of the baseline's detail that counts as gone")
    ap.add_argument("--png", type=int, default=1,
                    help="how many worst captures per run to write out as PNG")
    args = ap.parse_args()
    args._box = None

    box = None
    if args.box:
        box = tuple(int(v) for v in args.box.replace("x", ",").split(","))

    base = None
    if args.baseline:
        box, base, bflags = run(args.baseline, args, box)
        if bflags:
            print("   the BASELINE's own flags: %s" %
                  ", ".join(sorted(set("%s/%s" % (f[0], f[1]) for f in bflags))))
            print("   (a verdict the baseline also produces on the same leg is "
                  "not a finding about THIS run, and is dropped below)")
            print()

    box, this, flags = run(args.run, args, box)
    if base is not None:
        common = set((f[0], f[1]) for f in bflags)
        flags = [f for f in flags if (f[0], f[1]) not in common]
    args._box = box
    worse = compare(base, this, args) if base else []
    regions = region_report(base, this, args) if base else []
    for name, gone, appeared, changed, ma, mb, skip in regions:
        print("== what stopped being drawn (leg %s, parked pose, %dx%d tiles)" %
              (name, args.tiles_x, args.tiles_y))
        if skip:
            print("   NOT ASKED: %s" % skip)
            print()
            continue
        if not gone and not appeared:
            print("   nothing gone, nothing new; %d tile(s) merely look "
                  "different." % len(changed))
        for rect, diff, da, db in gone:
            print("   GONE      tile at %d,%d %dx%d - detail %.1f -> %.1f, "
                  "mean diff %.1f/255" % (rect + (da, db, diff)))
        for rect, diff, db, da in appeared:
            print("   NEW       tile at %d,%d %dx%d - detail %.1f -> %.1f, "
                  "mean diff %.1f/255" % (rect + (da, db, diff)))
        if (gone or appeared) and changed:
            print("   (%d further tile(s) changed without losing detail)" %
                  len(changed))
        print()

    print("== verdict")
    if not flags and not worse and not any(g or a for _, g, a, _, _, _, _ in regions):
        print("   nothing flagged.")
    for kind, leg, r, why in flags:
        where = ""
        if r is not None:
            where = " - capture #%d of leg %s (t=%.2f s, game frame %s, %s)" % (
                r["i"], leg, r["t"], r["gf"], r["file"])
        print("   %-15s leg %-7s %s%s" % (kind, leg, why, where))
    for name, gone, appeared, _, _, _, _ in regions:
        if gone:
            print("   %-15s leg %-7s %d tile(s) lost their detail entirely - "
                  "something that WAS drawn is not being drawn any more" %
                  ("REGION GONE", name, len(gone)))
        if appeared:
            print("   %-15s leg %-7s %d tile(s) gained detail that the baseline "
                  "did not have" % ("REGION NEW", name, len(appeared)))
    for name, rm, rf, moving in worse:
        print("   %-15s leg %-7s %.2fx the baseline's unexplained residual "
              "(%.2fx as a fraction of all change)" %
              ("WORSE IN MOTION" if moving else "WORSE WHEN PARKED", name, rm, rf))

    if args.png > 0:
        print()
        print("== look at these")
        for name, blob in this.items():
            live = [r for r in blob["recs"] if not r["dup"]]
            if not live:
                continue
            key = "frac" if (blob["st"] and blob["st"]["moving"]) else "raw"
            worst = sorted(live, key=lambda r: -r[key])[:args.png]
            dump_pngs(blob["dir"], blob["meta"], box, blob["recs"],
                      [r["i"] for r in worst],
                      os.path.join(blob["dir"], "look"))

    sys.exit(1 if (flags or worse) else 0)


if __name__ == "__main__":
    main()
