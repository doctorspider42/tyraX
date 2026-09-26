"""Compare two inventory arms on what the shared reflection probe COST them.

    python compare_probe.py ctl=<dir> cand=<dir> [--labels a,b,c,d]

Each `<dir>` is a results directory holding a `frame-inventory.csv`. The only
rows that matter are the probe's two producers: `hits` is how often the probe
CAPTURED, and that is the whole measurement for a reuse change - the triangles
and flushes follow it exactly, because a capture either happens in full or does
not happen at all.

Milliseconds are NOT measured here and PCSX2 could not measure them anyway.
They are derived from one hardware anchor: the probe costs **2.07 ms** of
garage-day frame at its every-second-frame cadence
(examples/vehicle-playground/authoring/road-lod-2026-09-16), so one capture is
**4.14 ms** and a capture rate of r per frame is 4.14 * r ms of average frame.
The derived column is labelled as derived and is a projection until somebody
runs the arms on the console.
"""
import argparse
import csv
from pathlib import Path

PROBE = ('env_probe_sky', 'env_probe_objs')
ANCHOR_MS_PER_CAPTURE = 4.14  # garage day, physical PS2, see the module docstring


def load(path):
    rows = list(csv.DictReader((Path(path) / 'frame-inventory.csv').open()))
    out = {}
    for ph in range(4):
        here = [r for r in rows if int(r['phase']) == ph]
        sel = [r for r in here if r['kind'] == 'producer']
        frames = max((int(r['frames']) for r in sel), default=0)
        if not frames:
            continue
        # One capture enters both probe slots exactly once, so the sky slot's
        # hit count IS the capture count; the objects slot counts one drain per
        # submitted object as well and cannot be used for it.
        caps = sum(int(r['hits']) for r in sel if r['producer'] == 'env_probe_sky')
        tri = sum(int(r['triangles']) for r in sel if r['producer'] in PROBE)
        pkg = sum(int(r['packages']) for r in sel if r['producer'] in PROBE)
        flush = sum(int(r['flushes']) for r in sel if r['producer'] in PROBE)
        total = sum(int(r['triangles']) for r in sel)
        # The gate's own row (1.106.0 trees only): beats consulted, beats
        # skipped, and the staleness it let stand, in target pixels x1000.
        g = [r for r in here if r['kind'] == 'reuse']
        beats = int(g[0]['hits']) if g else 0
        taken = int(g[0]['bags']) if g else 0
        worst = int(g[0]['triangles']) / 1000.0 if g else 0.0
        seen = int(g[0]['packages']) / 1000.0 if g else 0.0
        out[ph] = dict(frames=frames, caps=caps, tri=tri, pkg=pkg, flush=flush,
                       total=total, beats=beats, taken=taken, worst=worst,
                       seen=seen)
    return out


p = argparse.ArgumentParser(description=__doc__,
                            formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('arms', nargs='+', help='name=<results dir>')
p.add_argument('--labels', default='0,1,2,3',
               help='comma-separated pose names, in phase order')
a = p.parse_args()
labels = a.labels.split(',')
arms = []
for spec in a.arms:
    name, _, path = spec.partition('=')
    arms.append((name, load(path)))

base = arms[0][1]
for name, data in arms:
    print(f'\n=== {name} ===')
    print(f'{"pose":<14}{"frames":>7}{"captures":>10}{"per frame":>11}'
          f'{"tri/frame":>11}{"pkg/frame":>11}{"flush/frame":>13}'
          f'{"% of frame":>12}')
    print('-' * 89)
    for ph, d in sorted(data.items()):
        lab = labels[ph] if ph < len(labels) else str(ph)
        share = 100.0 * d['tri'] / d['total'] if d['total'] else 0.0
        print(f'{lab:<14}{d["frames"]:>7}{d["caps"]:>10}'
              f'{d["caps"] / d["frames"]:>11.4f}{d["tri"] / d["frames"]:>11.1f}'
              f'{d["pkg"] / d["frames"]:>11.1f}{d["flush"] / d["frames"]:>13.2f}'
              f'{share:>11.1f}%')
    if any(d['beats'] for d in data.values()):
        print(f'\n{"":<14}{"beats":>7}{"reused":>10}{"reuse rate":>12}'
              f'{"worst permitted px":>21}{"worst seen px":>16}')
        print('-' * 89)
        for ph, d in sorted(data.items()):
            lab = labels[ph] if ph < len(labels) else str(ph)
            rate = d['taken'] / d['beats'] if d['beats'] else 0.0
            print(f'{lab:<14}{d["beats"]:>7}{d["taken"]:>10}{rate:>12.4f}'
                  f'{d["worst"]:>21.3f}{d["seen"]:>16.3f}')
    if name != arms[0][0]:
        print(f'{"":<14}{"vs " + arms[0][0]:>17}{"d captures":>11}'
              f'{"d tri/frame":>13}{"derived ms":>13}')
        for ph, d in sorted(data.items()):
            if ph not in base:
                continue
            lab = labels[ph] if ph < len(labels) else str(ph)
            dr = d['caps'] / d['frames'] - base[ph]['caps'] / base[ph]['frames']
            dt = d['tri'] / d['frames'] - base[ph]['tri'] / base[ph]['frames']
            print(f'{lab:<14}{"":>17}{dr:>11.4f}{dt:>13.1f}'
                  f'{dr * ANCHOR_MS_PER_CAPTURE:>13.3f}')
print('\nThe ms column is DERIVED from the hardware anchor (4.14 ms per '
      'capture,\ngarage day) and is a projection, not a measurement.')
