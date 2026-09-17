"""Compare two arms' self-captured frames, WITHIN each arm before between them.

    python compare_captures.py ctl=<dir> cand=<dir> [--poses 0,2]

Each directory holds `poseN-k.png` written by capture-arm.ps1 - the game's own
`--capture-frame`, so there is no emulator window, no title bar, no FPS readout
and no crop to argue about. Needs PIL and numpy.

The order is the point and it is not negotiable: an arm whose own repeats
differ cannot support a between-arm number, so the within-arm spread is
printed first and a non-zero one is a refusal rather than a caveat. Only DAY
poses are comparable on this fixture; the night ones have authored lamp flicker
and twinkling stars and never settle.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)


def diff(a, b):
    d = np.abs(a - b).max(axis=2)
    return int((d > 0).sum()), int(d.max()), float(d.mean())


p = argparse.ArgumentParser(description=__doc__,
                            formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('arms', nargs='+', help='name=<capture dir>')
p.add_argument('--poses', default='0,2')
a = p.parse_args()
arms = [(s.partition('=')[0], Path(s.partition('=')[2])) for s in a.arms]
poses = [int(x) for x in a.poses.split(',')]

shots = {}
for name, d in arms:
    for pose in poses:
        files = sorted(d.glob(f'pose{pose}-*.png'))
        if files:
            shots[(name, pose)] = [(f.name, load(f)) for f in files]

print('WITHIN each arm - repeat 1 against every later repeat')
print(f'{"arm":<16}{"pose":>5}{"repeat":>9}{"pixels":>10}{"worst":>8}')
print('-' * 48)
clean = True
for (name, pose), lst in sorted(shots.items()):
    for fname, img in lst[1:]:
        px, worst, _ = diff(lst[0][1], img)
        if px:
            clean = False
        print(f'{name:<16}{pose:>5}{fname:>9}{px:>10}{worst:>8}')
if not clean:
    print('\nAn arm is not repeatable at that pose. STOP - no between-arm '
          'number can be read from it.')

print('\nBETWEEN arms - repeat 1 of each, whole self-captured frame')
print(f'{"comparison":<34}{"pose":>5}{"size":>12}{"pixels":>10}{"worst":>8}'
      f'{"mean":>9}')
print('-' * 78)
base = arms[0][0]
for name, _ in arms[1:]:
    for pose in poses:
        if (base, pose) not in shots or (name, pose) not in shots:
            continue
        ia, ib = shots[(base, pose)][0][1], shots[(name, pose)][0][1]
        if ia.shape != ib.shape:
            print(f'{base + " vs " + name:<34}{pose:>5}  size mismatch')
            continue
        px, worst, mean = diff(ia, ib)
        print(f'{base + " vs " + name:<34}{pose:>5}'
              f'{f"{ia.shape[1]}x{ia.shape[0]}":>12}{px:>10}{worst:>8}'
              f'{mean:>9.5f}')
