"""Per-pose packages and vertices for the cheap-caster round.

Reads two frame-inventory.csv files (the counts instrument's own output, which
accumulates over `frames` frames per phase) and prints PER FRAME figures.
Packages lead every table: the console has priced a VU1 package at 19.5 us in
garage day and 31.8 at night, and the triangle column can point the wrong way.
"""
import csv
import sys
from pathlib import Path

POSES = ['garage day', 'garage night', 'outer day', 'outer night']
ROWS = ['proj_silhouette', 'proj_patch', 'proj_shadows']
COLS = ['packages', 'vertices', 'bags', 'triangles']


def load(path):
    out = {}
    with open(path, newline='', encoding='utf-8') as fh:
        for r in csv.DictReader(fh):
            if r['kind'] != 'producer':
                continue
            ph = int(r['phase'])
            frames = int(r['frames']) or 1
            out[(ph, r['producer'])] = {
                c: int(r[c]) / frames for c in COLS
            }
    return out


def main():
    ctl = load(sys.argv[1])
    cnd = load(sys.argv[2])
    print('Per frame. "ctl" = the caster\'s own textured, per-vertex-coloured')
    print('bags (today); "cheap" = the single-colour untextured silhouette bag.')
    for ph in range(4):
        print()
        print('=== pose %d (%s) ===' % (ph, POSES[ph]))
        print('%-18s %10s %10s %10s' % ('', 'ctl', 'cheap', 'delta'))
        for producer in ROWS:
            a = ctl.get((ph, producer))
            b = cnd.get((ph, producer))
            if a is None or b is None:
                continue
            for c in ('packages', 'vertices', 'bags'):
                if a[c] == 0 and b[c] == 0:
                    continue
                print('%-18s %10.1f %10.1f %+10.1f'
                      % (producer + '.' + c, a[c], b[c], b[c] - a[c]))
        # the frame as a whole, so a package moved out of the bracket rather
        # than out of the frame would show up
        fa = ctl.get((ph, 'frame')) or ctl.get((ph, 'total'))
        fb = cnd.get((ph, 'frame')) or cnd.get((ph, 'total'))
        if fa and fb:
            for c in ('packages', 'vertices'):
                print('%-18s %10.1f %10.1f %+10.1f'
                      % ('FRAME.' + c, fa[c], fb[c], fb[c] - fa[c]))


if __name__ == '__main__':
    main()
