"""lab_miss.py LOG [LOG2] - render-label means for missed vs clean frames in
the drive window (250..673), from the KF + KFL rings."""
import re, sys
from collections import defaultdict


def load(fn):
    kf, kfl, names = {}, {}, {}
    for line in open(fn, encoding='utf-8', errors='replace'):
        m = re.search(r'KFLAB (\d+) (\S+)', line)
        if m:
            names[int(m.group(1))] = m.group(2)
            continue
        if 'KFL ' in line:
            for m in re.finditer(r'(\d+):([\d,]+)', line):
                kfl[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x]
        elif 'KF ' in line and 'fields' not in line:
            for m in re.finditer(r'(\d+):([\d,]+)', line):
                kf[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x]
    return kf, kfl, names


for fn in sys.argv[1:]:
    kf, kfl, names = load(fn)
    miss = [f for f in kf if 250 <= f < 673 and kf[f][0] > 1750]
    clean = [f for f in kf if 250 <= f < 673 and kf[f][0] <= 1750]
    print(fn, 'miss', len(miss), 'clean', len(clean))
    rows = []
    for k, n in sorted(names.items()):
        if k >= 44:
            continue
        a = sum(kfl[f][k] for f in miss if f in kfl and len(kfl[f]) > k) / max(1, len(miss)) / 100
        b = sum(kfl[f][k] for f in clean if f in kfl and len(kfl[f]) > k) / max(1, len(clean)) / 100
        if a > 0.15 or b > 0.15:
            rows.append((a - b, n, a, b))
    for d, n, a, b in sorted(rows, reverse=True):
        print('  %-26s miss %5.2f clean %5.2f  diff %+5.2f' % (n, a, b, d))
    for idx, n in [(4, 'phys'), (5, 'veh'), (8, 'scene'), (1, 'pre'), (2, 'work')]:
        a = sum(kf[f][idx] for f in miss) / max(1, len(miss)) / 100
        b = sum(kf[f][idx] for f in clean) / max(1, len(clean)) / 100
        print('  [%s] miss %.2f clean %.2f' % (n, a, b))
