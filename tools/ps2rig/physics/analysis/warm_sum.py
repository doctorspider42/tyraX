"""warm_sum.py LOG... - cold (KFRSUM) vs warm (KFRWARM) per-route us/bag and
stage split, plus the route-5 no-clip census (KFRCEN)."""
import re, sys
T = 294.912  # ticks per us
names = {1: 'stamp', 2: 'culled', 3: 'whole', 4: 'direct', 5: 'partial', 6: 'pstrip', 7: 'other', 8: 'sub', 9: 'billb'}


def table(txt, tag):
    rs = {}
    for m in re.finditer(tag + r' (\d+)((?: \d+){29})', txt):
        rs[int(m.group(1))] = [int(x) for x in m.group(2).split()]
    return rs


for fn in sys.argv[1:]:
    txt = open(fn, encoding='utf-8', errors='replace').read()
    arm = re.search(r'ARMSEL (\d+)', txt)
    print('%s arm %s' % (fn, arm.group(1) if arm else '?'))
    cold, warm = table(txt, 'KFRSUM'), table(txt, 'KFRWARM')
    for r in sorted(cold):
        c = cold[r]
        if not c[0]: continue
        line = '  r%d %-7s cold n %6d %6.1f us (s %.1f b %.1f p %.1f d %.1f f %.1f) sends %.2f' % (
            r, names.get(r, ''), c[0], c[2] / c[0] / T, c[3] / c[0] / T, c[4] / c[0] / T, c[5] / c[0] / T,
            c[6] / c[0] / T, c[7] / c[0] / T, c[19] / c[0])
        w = warm.get(r)
        if w and w[0]:
            line += ' | warm n %6d %6.1f us (s %.1f b %.1f p %.1f d %.1f f %.1f)' % (
                w[0], w[2] / w[0] / T, w[3] / w[0] / T, w[4] / w[0] / T, w[5] / w[0] / T, w[6] / w[0] / T, w[7] / w[0] / T)
        print(line)
    m = re.search(r'KFRCEN (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)', txt)
    if m:
        v = [int(x) for x in m.groups()]
        if v[0]:
            print('  census partial bags %d (%.1f/frame, %.1f us) | no clip pkg %d = %.0f %% (%.1f us) | all drawn %d = %.0f %% (%.1f us)' % (
                v[0], v[0] / 900, v[1] / v[0] / T, v[2], 100 * v[2] / v[0], v[3] / max(v[2], 1) / T,
                v[4], 100 * v[4] / v[0], v[5] / max(v[4], 1) / T))
