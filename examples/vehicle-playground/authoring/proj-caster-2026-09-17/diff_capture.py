"""Pixel comparison of two captures, with a bounding box and a diff image.

A silhouette is a shadow, so the acceptance for this round is the picture, not
the package count. This prints how many pixels moved, by how much, and WHERE -
a count alone cannot tell a shifted shadow edge from a shadow that vanished.
"""
import sys
from pathlib import Path
from PIL import Image


def main():
    a = Image.open(sys.argv[1]).convert('RGB')
    b = Image.open(sys.argv[2]).convert('RGB')
    out = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    if a.size != b.size:
        raise SystemExit('size mismatch: %s vs %s' % (a.size, b.size))
    pa, pb = a.load(), b.load()
    w, h = a.size
    n = 0
    worst = 0
    total = 0
    x0, y0, x1, y1 = w, h, -1, -1
    mask = Image.new('RGB', (w, h), (0, 0, 0))
    pm = mask.load()
    for y in range(h):
        for x in range(w):
            ca, cb = pa[x, y], pb[x, y]
            d = max(abs(ca[0] - cb[0]), abs(ca[1] - cb[1]), abs(ca[2] - cb[2]))
            if d:
                n += 1
                total += d
                worst = max(worst, d)
                x0, y0 = min(x0, x), min(y0, y)
                x1, y1 = max(x1, x), max(y1, y)
                v = min(255, 40 + d * 6)
                pm[x, y] = (v, 0, 0)
            else:
                # keep a dim copy of the base so the diff is readable in place
                g = sum(ca) // 9
                pm[x, y] = (g, g, g)
    px = w * h
    print('%s' % sys.argv[1])
    print('%s' % sys.argv[2])
    print('size            %dx%d (%d px)' % (w, h, px))
    print('pixels differing %d  (%.4f%% of the frame)' % (n, 100.0 * n / px))
    if n:
        print('max channel delta %d' % worst)
        print('mean delta over differing px %.2f' % (total / n))
        print('bounding box    x %d..%d  y %d..%d  (%dx%d)'
              % (x0, x1, y0, y1, x1 - x0 + 1, y1 - y0 + 1))
    if out:
        mask.save(out)
        print('diff image -> %s' % out)


if __name__ == '__main__':
    main()
