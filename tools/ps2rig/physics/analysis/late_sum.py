"""late_sum.py LOG - LATE events (game/late.py + engine_lateprobe) against KF misses."""
import re, sys, collections
recs = {}; ev = []
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    if 'LATE ' in line and 'count' not in line:
        ev += [tuple(int(x) for x in m.split(':')) for m in re.findall(r'\d+:\d+:\d+:\d+', line)]
    elif 'KF ' in line and 'fields' not in line:
        for m in re.finditer(r'(\d+):([\d,]+)', line):
            recs[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x]
miss = [f for f in sorted(recs) if recs[f][0] > 1750]
print('misses', len(miss))
agg = collections.defaultdict(lambda: [0, 0, 0])
for f, ph, k, t in ev:
    a = agg[(ph, k)]; a[0] += 1; a[1] += t; a[2] = max(a[2], t)
for key, (n, t, m) in sorted(agg.items()):
    print('phase %2d kind %d n %4d mean %.2f max %.2f' % (key[0], key[1], n, t / n / 100, m / 100))
byf = collections.defaultdict(list)
for f, ph, k, t in ev: byf[f].append((ph, k, t / 100))
expl = 0
for f in miss:
    e = byf.get(f, []) + byf.get(f - 1, [])
    big = [x for x in e if x[2] > 5]
    expl += bool(big)
    print(f, 'pre %.1f work %.1f stall %.1f' % (recs[f][1] / 100, recs[f][2] / 100, recs[f][3] / 100), 'f-1:', byf.get(f - 1), 'f:', byf.get(f))
print('misses with a >5 ms wait in f or f-1:', expl, '/', len(miss))
