#!/usr/bin/env python3
"""Summarise a rig log. usage: cld_sum.py <log> [scope ...]
Per arm and state (1 parked, 2 driving), per window index in the state:
mean work / p50 / p95 and late counts over all cycles, then CLDS scope means
(ms) per arm/state for the scopes named."""
import sys, re, collections
log = open(sys.argv[1], encoding='utf-8', errors='replace').read().splitlines()
scopes = sys.argv[2:] or ['V.step', 'V.buildCol', 'Light_pools', 'Light_beams',
                          'Static_batches', 'Objects', 'Roads', 'Terrain', 'G.scene', 'G.update', 'core']
win = collections.defaultdict(list)       # (arm, st, k) -> [(work,p50,p95,late,n)]
sc = collections.defaultdict(list)        # (arm, st, scope) -> [us]
cur = None; k = collections.Counter(); lastcyc = {}
for ln in log:
    m = re.search(r'CLD (\w+) arm=(\d+) cyc=(\d+) st=(\d) f=\d+ n=(\d+) work=(\d+) p50=(\d+) p95=(\d+) max=\d+ late=(\d+)', ln)
    if m:
        why, arm, cyc, st, n, work, p50, p95, late = m.groups()
        arm, cyc, st = int(arm), int(cyc), int(st)
        key = (cyc, st)
        if lastcyc.get('k') != key:
            lastcyc['k'] = key; k[key] = 0
        idx = k[key]; k[key] += 1
        if cyc == 0:
            cur = None; continue  # first cycle: warm-up, skipped
        win[(arm, st, idx)].append((int(work), int(p50), int(p95), int(late), int(n)))
        cur = (arm, st)
        continue
    m = re.match(r'.*CLDS (\d) (\S+) (\d+) ', ln)
    if m and cur:
        st, name, us = int(m.group(1)), m.group(2), int(m.group(3))
        if name in scopes:
            sc[(cur[0], st, name)].append(us)
arms = sorted({a for a, _, _ in win})
print('arm st win  cycles  work_us  p50_us  p95_us  late/n')
for st in (1, 2):
    for idx in range(6):
        for a in arms:
            v = win.get((a, st, idx))
            if not v: continue
            n = len(v)
            print(f'{a:3d} {st:2d} {idx:3d} {n:7d} {sum(x[0] for x in v)/n:8.0f} {sum(x[1] for x in v)/n:7.0f} '
                  f'{sum(x[2] for x in v)/n:7.0f}  {sum(x[3] for x in v)}/{sum(x[4] for x in v)}')
print()
print('scope means (us/frame), per arm, state')
for st in (1, 2):
    for s in scopes:
        row = []
        for a in arms:
            v = sc.get((a, st, s))
            row.append(f'{sum(v)/len(v):7.0f}' if v else '      -')
        print(f'st{st} {s:16s}' + ' '.join(row))
