"""Compare arms of the WORST-PACKED-PRODUCER round, per producer, per pose.

The column that decides this round is PACKAGES, not triangles, and the reason
is written down in docs/model-pipeline.md, "What the triangle counters count":
a strip array deliberately carries degenerate triangles - two repeats either
side of every seam inside a run, plus the tail padding - and StaPipTelemetry
counts a package's primitives as `size - 2`. So ONE SURFACE REPORTS MORE
TRIANGLES AS A STRIP THAN AS A LIST, and `triangles` cannot be used to check
that two arms draw the same geometry when the arms differ in representation.
What CAN: the vertex count falls, the package count falls, and the pixels are
byte-identical (that is the picture arm's job, not this script's).

Usage:
  compare_packing.py ctl=<dir> wheels=<dir> patch=<dir> both=<dir> [--phase 0]
Each <dir> holds a frame-inventory.csv written by inventory-frame.py.
"""
import argparse
import csv
from pathlib import Path

POSES = ['garage day', 'garage night', 'outer day', 'outer night']
# The producers this round touches, plus the sub-split of one of them.
FOCUS = ['wheels', 'proj_shadows', 'proj_silhouette', 'proj_patch',
         'proj_wall', 'proj_rest']


def load(path):
    rows = {}
    with open(Path(path) / 'frame-inventory.csv', newline='') as f:
        for r in csv.DictReader(f):
            if r['kind'] != 'producer':
                continue
            n = int(r['frames']) or 1
            rows.setdefault(int(r['phase']), {})[r['producer']] = {
                k: int(r[k]) / n for k in
                ('triangles', 'packages', 'packages_outside',
                 'packages_guard', 'vertices', 'flushes', 'bags')}
    return rows


def total(rows, phase, field):
    return sum(v[field] for k, v in rows[phase].items()
               if k not in FOCUS or k in ('wheels', 'proj_shadows'))


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('arms', nargs='+', help='label=dir')
    p.add_argument('--phase', type=int, default=None,
                   help='one pose only (0..3); default: all four')
    a = p.parse_args()
    arms = {}
    order = []
    for spec in a.arms:
        label, _, path = spec.partition('=')
        arms[label] = load(path)
        order.append(label)
    base = order[0]
    phases = [a.phase] if a.phase is not None else range(4)

    for ph in phases:
        print(f'\n=== pose {ph} ({POSES[ph]}) ===')
        print(f'{"producer":<18}{"arm":<9}{"pkg":>8}{"pkgOut":>8}'
              f'{"verts":>9}{"tri":>9}{"bags":>7}{"dPkg":>8}')
        for prod in FOCUS:
            if prod not in arms[base][ph]:
                continue
            for label in order:
                r = arms[label][ph].get(prod)
                if r is None:
                    continue
                d = r['packages'] - arms[base][ph][prod]['packages']
                print(f'{prod if label == base else "":<18}{label:<9}'
                      f'{r["packages"]:>8.1f}{r["packages_outside"]:>8.1f}'
                      f'{r["vertices"]:>9.1f}{r["triangles"]:>9.1f}'
                      f'{r["bags"]:>7.1f}'
                      f'{"" if label == base else f"{d:+8.1f}"}')
        print(f'{"FRAME TOTAL":<18}{"":<9}')
        for label in order:
            pkg = total(arms[label], ph, 'packages')
            tri = total(arms[label], ph, 'triangles')
            vrt = total(arms[label], ph, 'vertices')
            d = pkg - total(arms[base], ph, 'packages')
            print(f'{"":<18}{label:<9}{pkg:>8.1f}{"":>8}{vrt:>9.1f}'
                  f'{tri:>9.1f}{"":>7}'
                  f'{"" if label == base else f"{d:+8.1f}"}')


if __name__ == '__main__':
    main()
