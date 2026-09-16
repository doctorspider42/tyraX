"""Does a coarse terrain tile rise above the road that follows the dense map?

The road is a decal lifted 0.12 world units above the DENSE heightfield. Terrain
detail distance rebuilds far tiles from every 2nd sample (out to 2.2x the
distance) and every 4th beyond that. Where the coarse surface sits HIGHER than
the dense one by more than the lift, the ground pokes through the asphalt.

This measures that directly, along the seven roads' own sample positions, which
is where it matters - not over the whole map.
"""
import json, glob, os, sys, math

root = sys.argv[1]
proj = [f for f in os.listdir(root) if f.endswith('.tyra')][0]
d = json.load(open(os.path.join(root, proj)))
s = d['scenes'][0]
W = float(s['terrain']['width'])
D = float(s['terrain']['depth'])
tok = open(os.path.join(root, 'terrain-main.heights')).read().split()
hmW, hmD = int(tok[0]), int(tok[1])
H = [float(x) for x in tok[2:2 + hmW * hmD]]
LIFT = 0.12


def h(a, b):
    a = min(max(a, 0), hmW - 1)
    b = min(max(b, 0), hmD - 1)
    return H[b * hmW + a]


def sample(x, z, step):
    """Height of the mesh built from every `step`-th sample, at world x/z."""
    gx = (x + W * 0.5) / W * (hmW - 1)
    gz = (z + D * 0.5) / D * (hmD - 1)
    gx = min(max(gx, 0.0), hmW - 1.001)
    gz = min(max(gz, 0.0), hmD - 1.001)
    # The coarse mesh's own cell, snapped down to a multiple of `step`.
    ix = int(gx // step) * step
    iz = int(gz // step) * step
    fx = (gx - ix) / step
    fz = (gz - iz) / step
    # The renderer's diagonal, 10 -> 01, as Viewport::terrainHeight uses.
    if fx + fz <= 1.0:
        return (h(ix, iz) + fx * (h(ix + step, iz) - h(ix, iz))
                + fz * (h(ix, iz + step) - h(ix, iz)))
    return (h(ix + step, iz + step)
            + (1.0 - fz) * (h(ix + step, iz) - h(ix + step, iz + step))
            + (1.0 - fx) * (h(ix, iz + step) - h(ix + step, iz + step)))


roads = []
for f in sorted(glob.glob(os.path.join(root, 'objects', '*.json'))):
    o = json.load(open(f))
    if o.get('type') == 'road':
        roads.append((o['name'], float(o['roadWidth']), o['roadPoints']))


def cr(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                  + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)


print('%-22s %8s %9s %9s %9s %9s' % ('road', 'samples', 'max+2x', 'max+4x',
                                     'over2x', 'over4x'))
tot = [0, 0, 0]
worst = [0.0, 0.0]
for name, width, pts in roads:
    n = len(pts) // 2

    def pt(i):
        i = min(max(i, 0), n - 1)
        return pts[i * 2], pts[i * 2 + 1]

    hw = 0.5 * max(width, 0.1)
    cross = max(1, math.ceil(width / 0.5))
    m2 = m4 = 0.0
    o2 = o4 = 0
    cnt = 0
    for seg in range(n - 1):
        ax, az = pt(seg)
        bx, bz = pt(seg + 1)
        seglen = math.hypot(bx - ax, bz - az)
        steps = int(seglen / 1.0) + 1 if seglen > 1.0 else 1
        for k in range(0 if seg == 0 else 1, steps + 1):
            t = k / steps
            p0 = pt(seg - 1); p1 = pt(seg); p2 = pt(seg + 1); p3 = pt(seg + 2)
            cx = cr(p0[0], p1[0], p2[0], p3[0], t)
            cz = cr(p0[1], p1[1], p2[1], p3[1], t)
            t2 = min(t + 0.05, 1.0)
            dx = cr(p0[0], p1[0], p2[0], p3[0], t2) - cx
            dz = cr(p0[1], p1[1], p2[1], p3[1], t2) - cz
            tl = math.hypot(dx, dz)
            if tl > 1e-6:
                dx /= tl; dz /= tl
            else:
                dx, dz = 0.0, 1.0
            rx, rz = dz * hw, -dx * hw
            for j in range(cross + 1):
                side = j / cross * 2.0 - 1.0
                x = cx + rx * side
                z = cz + rz * side
                dense = sample(x, z, 1)
                road = dense + LIFT
                for step, idx in ((2, 0), (4, 1)):
                    up = sample(x, z, step) - road
                    if idx == 0:
                        m2 = max(m2, up)
                        o2 += 1 if up > 0 else 0
                    else:
                        m4 = max(m4, up)
                        o4 += 1 if up > 0 else 0
                cnt += 1
    print('%-22s %8d %9.4f %9.4f %9d %9d' % (name, cnt, m2, m4, o2, o4))
    tot[0] += cnt; tot[1] += o2; tot[2] += o4
    worst[0] = max(worst[0], m2); worst[1] = max(worst[1], m4)
print('%-22s %8d %9.4f %9.4f %9d %9d' % ('TOTAL', tot[0], worst[0], worst[1],
                                         tot[1], tot[2]))
print('road lift above the dense heightfield: %.2f world units' % LIFT)
