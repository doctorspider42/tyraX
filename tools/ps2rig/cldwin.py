#!/usr/bin/env python3
"""cldwin.py <log> [skipcycles]: per arm, per state and WINDOW INDEX within the
state (parked w0,w1; driving w0..w2), the median work / p50 / late, so drive
windows (acceleration vs cruise) are compared like with like."""
import sys, re, collections, statistics
rows = collections.defaultdict(list)
last = 0
skip = int(sys.argv[2]) if len(sys.argv) > 2 else 0
idx = collections.Counter()
for line in open(sys.argv[1], encoding='latin-1'):
    m = re.search(r'CLD (win|end) (.*)', line)
    if not m:
        continue
    kv = dict(re.findall(r'(\w+)=(\d+)', m.group(2)))
    f = int(kv['f'])
    ok = (f <= 2400) if last == 0 else (0 < f - last <= 1200)
    if not ok:
        continue
    last = f
    if m.group(1) != 'win':
        continue
    key = (int(kv['cyc']), int(kv['st']))
    w = idx[key]; idx[key] += 1
    if int(kv['cyc']) < skip:
        continue
    rows[(int(kv['st']), w, int(kv['arm']))].append((int(kv['work']), int(kv['p50']), int(kv['late'])))
for (st, w, arm), r in sorted(rows.items()):
    print(f'st {st} w{w} arm {arm}: n={len(r)} work med {statistics.median([x[0] for x in r])/1000:.3f} '
          f'p50 med {statistics.median([x[1] for x in r])/1000:.3f} late {sum(x[2] for x in r)}')
