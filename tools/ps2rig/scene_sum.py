#!/usr/bin/env python3
"""scene_sum.py LOG [skip_cycles=1] - the architecture loop's scene-table row
(docs/architecture-loop.md) from a cld_probe log, per (state, arm).

Prints, averaged over the windows after the warm-up cycles:
- work mean / p50 / p95 and late frames (CLD rows; microseconds -> ms);
- the GPU timeline (CLDGT, per mille of the whole frame: VU1, VU1 kick, GIF,
  VIF1, idle) when the arm was built with --gputl;
- the EE's waits on the GPU (q.waitFor, q.drain, q.ringfull scopes, --gpuwait)
  and the renderer stall (period - work);
- per scope: ms a frame, I and D misses a frame, and the miss-time fraction
  (I x 50 + D x 42 cycles over the scope's cycles, EE 294.912 MHz). Needs a
  PMU build (not --lean). Scopes nest (core sits inside G.endFrame and
  q.flushrec), so read them as subsystems, not as a partition.
- the whole frame's misses (CLDP) against the work.
"""
import re, sys, statistics
from collections import defaultdict

CYC_I, CYC_D, MHZ = 50.0, 42.0, 294.912
# Lower bound: 20 cycles a miss. Misses overlap in real code and every
# uncached access (a hardware register poll) counts as a D miss, so the
# 50/42 prices overcount; the share is printed as a range.
CYC_LO = 20.0
log = sys.argv[1]
skip = int(sys.argv[2]) if len(sys.argv) > 2 else 1
txt = open(log, encoding='utf-8', errors='replace').read().splitlines()

wins = []  # one dict per CLD win row, with the rows that follow it
cur = None
for ln in txt:
    m = re.match(r'CLD win arm=(\d+) cyc=(\d+) st=(\d+) f=\d+ n=(\d+) work=(\d+) p50=(\d+) p95=(\d+) max=\d+ late=(\d+) period=(\d+)', ln)
    if m:
        a, c, st, n, w, p50, p95, late, per = map(int, m.groups())
        cur = dict(arm=a, cyc=c, st=st, n=n, work=w, p50=p50, p95=p95, late=late, period=per, scopes={}, gt=None, frame=None)
        wins.append(cur)
        continue
    if cur is None:
        continue
    m = re.match(r'CLDS \d+ (\S+) (\d+) (\d+) im=(\d+) dm=(\d+)', ln)
    if m:
        cur['scopes'][m.group(1)] = tuple(int(x) for x in m.groups()[1:])
        continue
    m = re.match(r'CLDGT n=\d+ vu=(\d+) kick=(\d+) gif=(\d+) vif=(\d+) idle=(\d+)', ln)
    if m:
        cur['gt'] = tuple(int(x) for x in m.groups())
        continue
    m = re.match(r'CLDP frame im=(\d+) dm=(\d+)', ln)
    if m:
        cur['frame'] = (int(m.group(1)), int(m.group(2)))

groups = defaultdict(list)
for w in wins:
    if w['cyc'] >= skip:
        groups[(w['st'], w['arm'])].append(w)

STATE = {1: 'parked', 2: 'drive'}
for (st, arm), ws in sorted(groups.items()):
    mean = lambda k: statistics.mean(w[k] for w in ws) / 1000
    print(f'== state {st} ({STATE.get(st, "?")}) arm {arm}: {len(ws)} windows, {sum(w["n"] for w in ws)} frames')
    print(f'   work mean {mean("work"):.2f} p50 {statistics.median(w["p50"] for w in ws)/1000:.2f} '
          f'p95 {statistics.median(w["p95"] for w in ws)/1000:.2f} ms, period {mean("period"):.2f} ms, '
          f'late {sum(w["late"] for w in ws)}/{sum(w["n"] for w in ws)}, stall (period-work) {mean("period")-mean("work"):.2f} ms')
    gts = [w['gt'] for w in ws if w['gt']]
    if gts:
        g = [statistics.mean(x[i] for x in gts) / 10 for i in range(5)]
        print(f'   GPU timeline: VU1 {g[0]:.1f}% + kick {g[1]:.1f}%, GIF {g[2]:.1f}%, VIF1 {g[3]:.1f}%, idle {g[4]:.1f}%')
    names = defaultdict(list)
    for w in ws:
        for k, v in w['scopes'].items():
            names[k].append(v)
    waits = {k: statistics.mean(v[0] for v in names[k]) / 1000 for k in ('q.waitFor', 'r.gstail', 'q.drain', 'q.ringfull') if k in names}
    if waits:
        print(f'   EE waits on the GPU: {sum(waits.values()):.3f} ms a frame (' + ', '.join(f'{k} {v:.3f}' for k, v in waits.items()) + ')')
    rows = []
    for k, vs in names.items():
        us = statistics.mean(v[0] for v in vs)
        im = statistics.mean(v[2] for v in vs)
        dm = statistics.mean(v[3] for v in vs)
        if us < 100:
            continue
        frac = (im * CYC_I + dm * CYC_D) / (us * MHZ) if us else 0
        rows.append((us, k, im, dm, frac))
    for us, k, im, dm, frac in sorted(rows, reverse=True)[:12]:
        lo = (im + dm) * CYC_LO / (us * MHZ) if us else 0
        pmu = f'I {im:7.0f} D {dm:7.0f}  misses {100*lo:5.1f}-{100*frac:5.1f} %' if (im or dm) else '(no PMU)'
        print(f'   {k:28s} {us/1000:6.2f} ms  {pmu}')
    fr = [w['frame'] for w in ws if w['frame']]
    if fr and any(fr[0]):
        im = statistics.mean(x[0] for x in fr); dm = statistics.mean(x[1] for x in fr)
        # The present's vsync wait polls a register; each poll counts as a D
        # miss. Take its misses out (older logs: the q.flushrec scope that
        # holds it).
        stall = 'r.vsync' if 'r.vsync' in names else 'q.flushrec' if 'q.flushrec' in names else None
        if stall:
            im -= statistics.mean(v[2] for v in names[stall]); dm -= statistics.mean(v[3] for v in names[stall])
        ms = (im * CYC_I + dm * CYC_D) / MHZ / 1000
        print(f'   whole frame without {stall}: I {im:.0f} D {dm:.0f} a frame = {ms:.2f} ms of misses '
              f'against work {mean("work"):.2f} ms ({100*ms/mean("work"):.0f} %)')
