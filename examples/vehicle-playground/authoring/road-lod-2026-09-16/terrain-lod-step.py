"""How big is the terrain's shape change when a detail band crosses it?

A band change is visible as "a shape settling" (docs/terrain-lod.md). This
measures the settling: the largest vertical disagreement between the dense mesh
and the every-2nd-sample mesh, anywhere on the district's heightfield, and what
that subtends in pixels at the distance the band actually switches.
"""
import json, os, sys, math

root = sys.argv[1]
band = float(sys.argv[2]) if len(sys.argv) > 2 else 160.0
proj = [f for f in os.listdir(root) if f.endswith('.tyra')][0]
d = json.load(open(os.path.join(root, proj)))
s = d['scenes'][0]
W = float(s['terrain']['width'])
D = float(s['terrain']['depth'])
tok = open(os.path.join(root, 'terrain-main.heights')).read().split()
hmW, hmD = int(tok[0]), int(tok[1])
H = [float(x) for x in tok[2:2 + hmW * hmD]]


def h(a, b):
    a = min(max(a, 0), hmW - 1)
    b = min(max(b, 0), hmD - 1)
    return H[b * hmW + a]


def mesh(gx, gz, step):
    ix = int(gx // step) * step
    iz = int(gz // step) * step
    fx = (gx - ix) / step
    fz = (gz - iz) / step
    if fx + fz <= 1.0:
        return (h(ix, iz) + fx * (h(ix + step, iz) - h(ix, iz))
                + fz * (h(ix, iz + step) - h(ix, iz)))
    return (h(ix + step, iz + step)
            + (1.0 - fz) * (h(ix + step, iz) - h(ix + step, iz + step))
            + (1.0 - fx) * (h(ix, iz + step) - h(ix + step, iz + step)))


cell = W / (hmW - 1)
worst = {2: 0.0, 4: 0.0}
# Every dense vertex, plus both triangle centroids of every dense cell.
pts = []
for b in range(hmD):
    for a in range(hmW):
        pts.append((float(a), float(b)))
for b in range(hmD - 1):
    for a in range(hmW - 1):
        pts.append((a + 1.0 / 3.0, b + 1.0 / 3.0))
        pts.append((a + 2.0 / 3.0, b + 2.0 / 3.0))
for gx, gz in pts:
    gx = min(gx, hmW - 1.001)
    gz = min(gz, hmD - 1.001)
    dense = mesh(gx, gz, 1)
    for step in (2, 4):
        worst[step] = max(worst[step], abs(mesh(gx, gz, step) - dense))

# PAL 512x448 native, the fixture's raster; a 60-degree vertical FOV.
px_per_rad = 448.0 / (2.0 * math.tan(math.radians(30.0)))
print('terrain cell %.1f world units, relief %.3f..%.3f' % (cell, min(H), max(H)))
for step, start in ((2, band), (4, band * 2.2)):
    ang = worst[step] / start
    print('every %dth sample: worst vertical disagreement %.4f world units; '
          'the band starts at %.0f units, so it subtends %.3f pixels'
          % (step, worst[step], start, ang * px_per_rad))
