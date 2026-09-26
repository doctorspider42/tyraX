"""Compare benchmark pose captures: within-arm repeats first, then arms.

A within-arm pair that is not byte-identical is a broken instrument, not a
result - so this prints those first and refuses to draw a conclusion until
they are clean. `--crop-bottom N` drops N rows, which is how PCSX2's own
status bar (a live FPS/EE readout, the one thing that moves in a parked
frame) is kept out of the comparison.
"""
import argparse
import os
import sys
from PIL import Image, ImageChops

POSES = ['garage-day', 'outer-day']


def load(d, name, i, crop_bottom, crop_top):
    im = Image.open(os.path.join(d, f'{name}-{i}.png')).convert('RGB')
    if crop_bottom or crop_top:
        im = im.crop((0, crop_top, im.width, im.height - crop_bottom))
    return im


def diff(a, b):
    d = ImageChops.difference(a, b)
    bbox = d.getbbox()
    px = [p for p in d.getdata() if p != (0, 0, 0)]
    worst = max((max(p) for p in px), default=0)
    return len(px), worst, bbox


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('dirs', nargs='+', help='NAME=dir')
    ap.add_argument('--crop-bottom', type=int, default=0)
    ap.add_argument('--crop-top', type=int, default=0)
    a = ap.parse_args()
    arms = {}
    for spec in a.dirs:
        n, d = spec.split('=', 1)
        arms[n] = d

    ok = True
    for pose in POSES:
        print(f'\n== {pose}')
        ims = {}
        for n, d in arms.items():
            ims[n] = [load(d, pose, i, a.crop_bottom, a.crop_top)
                      for i in (1, 2, 3)]
            print(f'  {n}: {ims[n][0].size}')
            for x, y in ((0, 1), (0, 2), (1, 2)):
                c, w, bb = diff(ims[n][x], ims[n][y])
                flag = '' if c == 0 else '   <-- NOT byte-identical'
                print(f'    repeat {x+1}v{y+1}: changed={c:8d} worstLevel={w:3d}'
                      f' bbox={bb}{flag}')
                if c:
                    ok = False
        names = list(arms)
        for i in range(len(names) - 1):
            for j in range(i + 1, len(names)):
                c, w, bb = diff(ims[names[i]][0], ims[names[j]][0])
                print(f'  {names[i]} vs {names[j]}: changed={c:8d}'
                      f' worstLevel={w:3d} bbox={bb}')
    print('\nwithin-arm repeats byte-identical:', ok)
    sys.exit(0 if ok else 2)
