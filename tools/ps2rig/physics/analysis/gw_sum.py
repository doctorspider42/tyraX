"""gw_sum.py LOG - per-frame EE time vs GPU waits (game/gpuwait.py + engine_gpuwait)."""
import re, sys
kf, kfl = {}, {}
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    t = kfl if 'KFL ' in line else (kf if ('KF ' in line and 'fields' not in line) else None)
    if t is None: continue
    for m in re.finditer(r'(\d+):([\d,]+)', line): t[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x]
kfl = {f: v for f, v in kfl.items() if len(v) >= 40}
cols = [('pre', lambda f: kf[f][1]), ('work', lambda f: kf[f][2]), ('phys', lambda f: kf[f][4]), ('scene', lambda f: kf[f][8]),
        ('vifWait', lambda f: kfl[f][37]), ('gsTail', lambda f: kfl[f][39]), ('ringFull', lambda f: kfl[f][36])]
def agg(fs):
    n = len(fs) or 1
    return ' '.join('%s %.2f' % (k, sum(v(f) for f in fs) / n / 100) for k, v in cols)
drive = [f for f in kf if 340 <= f <= 560 and f in kfl]
sets = [('miss', [f for f in drive if kf[f][0] > 1750]), ('clean', [f for f in drive if kf[f][0] <= 1750]),
        ('idle', [f for f in kf if 200 <= f < 250 and f in kfl]), ('park', [f for f in kf if f >= 900 and f in kfl])]
for n, fs in sets: print('%-5s %3d %s' % (n, len(fs), agg(fs)))
print('max gsTail %.2f vifWait %.2f' % (max(kfl[f][39] for f in kfl) / 100, max(kfl[f][37] for f in kfl) / 100))
