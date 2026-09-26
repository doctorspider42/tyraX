"""What the baked VEHICLE WHEEL models actually carry, straight out of .tmdl.

Half of this round's change lives in an asset rather than in code: vehbake now
calls meshstrip on the wheel with a weld that ignores normals
(docs/vehicles.md, "The wheel batch is a strip"). A generated-source grep
cannot see that, and a capture hash certainly cannot, so the arm's identity
block reads the file.

The padded column is the number that matters to the frame: the wheel batch
concatenates four wheels a car and each block is rounded up to a whole number
of runs, or a VU1 package would splice two wheels into one triangle.

Usage: wheel-strip-report.py <dir with veh-*-wheel.tmdl>
"""
import math
import struct
import sys
from pathlib import Path


def parts(path):
    b = path.read_bytes()
    off = 0
    magic, ver = struct.unpack_from('<4sI', b, off)
    off += 8
    assert magic == b'TMDL', f'{path} is not a .tmdl'
    off += 24  # min[3], max[3]
    pc, = struct.unpack_from('<I', b, off)
    off += 4
    for _ in range(pc):
        off += 32 + 64 + 64 + 12          # name, texture, refl, kd
        if ver >= 2:
            off += 12                      # ke
        off += 4 + 4                       # reflStrength, flags
        vc, = struct.unpack_from('<I', b, off)
        off += 4 + vc * 8 * 4
        aoc, = struct.unpack_from('<I', b, off)
        off += 4 + aoc
        lc, = struct.unpack_from('<I', b, off)
        off += 4
        for _ in range(lc):
            lvc, = struct.unpack_from('<I', b, off)
            off += 4 + lvc * 8 * 4
            lao, = struct.unpack_from('<I', b, off)
            off += 4 + lao
        run = 0
        strip = 0
        if ver >= 4:
            run, = struct.unpack_from('<I', b, off)
            off += 4
            for m in range(1 + lc):
                mvc, = struct.unpack_from('<I', b, off)
                off += 4 + mvc * 8 * 4
                mao, = struct.unpack_from('<I', b, off)
                off += 4 + mao
                if m == 0:
                    strip = mvc
        yield vc, run, strip


def main(argv):
    root = Path(argv[1])
    for path in sorted(root.glob('*-wheel.tmdl')):
        for lst, run, strip in parts(path):
            if not run or not strip:
                print(f'{path.name}: LIST only, {lst} verts '
                      f'({math.ceil(lst / 75)} pkgs/wheel @75)')
                continue
            padded = strip + (-strip % run)
            print(f'{path.name}: list {lst} -> strip {strip} '
                  f'({strip / lst:.3f}x), run {run}, padded {padded}, '
                  f'{padded // run} pkgs/wheel vs {math.ceil(lst / 75)}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
