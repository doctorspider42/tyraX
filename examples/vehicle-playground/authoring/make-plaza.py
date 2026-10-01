"""The Motor District's cobbled plaza patch (Python 3 + Pillow).

The central crossing of Garage boulevard and Market cross street carries a
per-junction override (docs/roads.md, "Junction overrides") whose patch
material is this one. A junction patch maps world XZ straight onto the texture
(u = 0.5 + dx / 32, the same for v), so the 128-pixel image spans 32 units at 4
pixels per unit, centred on the crossing, and only its middle ~13 x 11 units
are ever drawn. The design is symmetric about both axes, so which way the
image's rows run does not matter.

Deterministic: re-running writes byte-identical files.
"""
from pathlib import Path
import math
import random
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
TEX = ROOT / 'res/textures'
MAT = ROOT / 'res/materials'

N = 128
PX = N / 32.0  # pixels per world unit
C = N / 2.0


def px(units):
    return C + units * PX


rng = random.Random(1450)
img = Image.new('RGB', (N, N), (58, 56, 54))
d = ImageDraw.Draw(img)

# Setts: 1 x 1 unit stones (3 px + 1 px joint), rows offset by half a stone,
# warm grey with a little per-stone variation.
for row in range(N // 4):
    off = 2 if row % 2 else 0
    for col in range(-1, N // 4 + 1):
        base = rng.randrange(118, 150)
        tint = (base + 10, base + 2, base - 8)
        for yy in range(row * 4, row * 4 + 3):
            for xx in range(col * 4 + off, col * 4 + off + 3):
                if 0 <= xx < N and 0 <= yy < N:
                    img.putpixel((xx, yy), tuple(max(0, c - rng.randrange(0, 10))
                                                 for c in tint))

# The patch spans the overlap of the two streets: x +-6.65 (Garage boulevard,
# 13 wide, runs along Z) and z +-5.65 (Market cross street, 11 wide, along X).
# Columns are x, rows are z.
HX, HZ = 6.65, 5.65


def rect(x0, z0, x1, z1, fill):
    d.rectangle((px(x0), px(z0), px(x1) - 1, px(z1) - 1), fill=fill)


# A darker granite band framing the square.
band = (70, 68, 74)
BX, BZ, T = 4.6, 3.6, 0.5
rect(-BX, -BZ, BX, -BZ + T, band)
rect(-BX, BZ - T, BX, BZ, band)
rect(-BX, -BZ, -BX + T, BZ, band)
rect(BX - T, -BZ, BX, BZ, band)

# Zebra crossings where each street enters: 0.5-unit bars with 0.5-unit gaps,
# running along the street, between the band and the patch edge.
white = (226, 224, 212)
for i in range(-4, 4):  # bars across Garage boulevard, at the +-Z entries
    a = i + 0.25
    rect(a, HZ - 1.75, a + 0.5, HZ - 0.25, white)
    rect(a, -HZ + 0.25, a + 0.5, -HZ + 1.75, white)
for i in range(-3, 3):  # bars across Market cross street, at the +-X entries
    a = i + 0.25
    rect(HX - 1.75, a, HX - 0.25, a + 0.5, white)
    rect(-HX + 0.25, a, -HX + 1.75, a + 0.5, white)

# A compass rose of lighter setts in the middle, around a brass disc.
light = (178, 170, 152)
for k in range(8):
    ang = k * math.pi / 4
    reach = 2.8 if k % 2 == 0 else 1.9
    t = 0.0
    while t <= reach:
        x = int(round(px(math.cos(ang) * t)))
        y = int(round(px(math.sin(ang) * t)))
        d.rectangle((x - 1, y - 1, x, y), fill=light)
        t += 0.25
d.ellipse((px(-0.9), px(-0.9), px(0.9) - 1, px(0.9) - 1), fill=(196, 150, 70))

img.save(TEX / 'district-plaza.png', optimize=True)
(MAT / 'district-plaza.mtl').write_text(
    'newmtl district-plaza\nKd 1 1 1\nmap_Kd ../textures/district-plaza.png\n',
    encoding='utf-8', newline='\n')
print('wrote', TEX / 'district-plaza.png')
