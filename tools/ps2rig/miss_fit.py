#!/usr/bin/env python3
"""miss_fit.py IMISS_LOG INSTR_LOG [skip_cycles=1] - the price of a miss in
the StaPip/frame code, fitted from two cld_probe logs of the SAME scene:
one from an arm built with --pmu (counter 0 = I-cache misses) and one built
with --pmu --count-instr (counter 0 = instructions completed); counter 1 is
D-cache misses in both (architecture loop R2-0, docs/architecture-loop.md).

For every (state, arm, scope) and every kept window it builds a row
    cycles = c0 * instructions + pI * Imiss + pD * Dmiss
from the window's own counters and the other log's mean for the counter it
did not count, and fits c0, pI, pD by least squares (relative weights).
Scopes nest, so 'frame' rows (CLDP) are left out of the fit; r.vsync and
q.flushrec are left out too (a register poll counts as a D miss but costs
no cache refill). Then per scope: ms, IPC, misses and the fitted miss share
(pI*I + pD*D) / cycles - the number the goal's 30 % line is judged on.
"""
import re, sys, statistics
from collections import defaultdict

MHZ = 294.912
# G.endFrame holds the present (the vsync poll); scopes with no PMU rows
# (counters read 0) are dropped below.
SKIP_SCOPES = {'r.vsync', 'q.flushrec', 'G.endFrame'}


def parse(fn, skip):
    out = defaultdict(list)  # (st, arm, scope) -> [(us, c0, dm)]
    cur = None
    for ln in open(fn, encoding='utf-8', errors='replace'):
        m = re.match(r'CLD win arm=(\d+) cyc=(\d+) st=(\d+)', ln)
        if m:
            a, c, st = map(int, m.groups())
            cur = (st, a) if c >= skip else None
            continue
        m = re.match(r'CLDS \d+ (\S+) (\d+) \d+ im=(\d+) dm=(\d+)', ln)
        if m and cur and m.group(1) not in SKIP_SCOPES:
            out[cur + (m.group(1),)].append(tuple(int(x) for x in m.groups()[1:]))
    return out


def solve(A, b):
    n = len(b)
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c]))
        M[c], M[p] = M[p], M[c]
        if abs(M[c][c]) < 1e-30:
            return None
        for r in range(n):
            if r != c:
                f = M[r][c] / M[c][c]
                for k in range(c, n + 1):
                    M[r][k] -= f * M[c][k]
    return [M[i][n] / M[i][i] for i in range(n)]


def lsq(rows):
    k = len(rows[0][0])
    A = [[0.0] * k for _ in range(k)]
    b = [0.0] * k
    for x, y, w in rows:
        for i in range(k):
            b[i] += w * w * x[i] * y
            for j in range(k):
                A[i][j] += w * w * x[i] * x[j]
    return solve(A, b)


if len(sys.argv) < 3:
    sys.exit(__doc__)
skip = int(sys.argv[3]) if len(sys.argv) > 3 else 1
mi, ni = parse(sys.argv[1], skip), parse(sys.argv[2], skip)
avg = lambda v, i: statistics.mean(x[i] for x in v)
keys = [k for k in mi if k in ni and avg(mi[k], 0) >= 50 and avg(ni[k], 1) > 0 and avg(mi[k], 2) > 0]
rows = []
for k in keys:
    Ibar, Nbar = avg(mi[k], 1), avg(ni[k], 1)
    for us, I, D in mi[k]:
        if us:
            rows.append(((Nbar, I, D), us * MHZ, 1.0 / (us * MHZ)))
    for us, N, D in ni[k]:
        if us:
            rows.append(((N, Ibar, D), us * MHZ, 1.0 / (us * MHZ)))
fit = lsq(rows)
if not fit:
    sys.exit('singular fit')
c0, pI, pD = fit
res = (sum(((y - sum(a * b for a, b in zip(fit, x))) * w) ** 2 for x, y, w in rows) / len(rows)) ** 0.5
print(f'fit over {len(rows)} rows, {len(keys)} scopes: c0 {c0:.3f} cyc/instr, pI {pI:.1f} cyc/I-miss, '
      f'pD {pD:.1f} cyc/D-miss (rms relative residual {100*res:.1f} %)')
two = lsq([((x[0], x[1] + x[2]), y, w) for x, y, w in rows])
if two:
    print(f'2-price fit: c0 {two[0]:.3f}, one miss price {two[1]:.1f} cyc')
for st, arm in sorted({k[:2] for k in keys}):
    print(f'== state {st} arm {arm}')
    print(f'   {"scope":28s} {"ms/f":>6s} {"instr/f":>8s} {"IPC":>5s} {"I/f":>6s} {"D/f":>6s} {"miss%":>6s} {"resid%":>7s}')
    sk = sorted((k for k in keys if k[:2] == (st, arm)), key=lambda k: -avg(mi[k], 0))
    for k in sk[:14]:
        C = (avg(mi[k], 0) * len(mi[k]) + avg(ni[k], 0) * len(ni[k])) / (len(mi[k]) + len(ni[k])) * MHZ
        N, I = avg(ni[k], 1), avg(mi[k], 1)
        D = (avg(mi[k], 2) * len(mi[k]) + avg(ni[k], 2) * len(ni[k])) / (len(mi[k]) + len(ni[k]))
        miss = pI * I + pD * D
        print(f'   {k[2]:28s} {C/MHZ/1000:6.2f} {N:8.0f} {N/C:5.2f} {I:6.0f} {D:6.0f} {100*miss/C:5.0f}% '
              f'{100*(C-c0*N-miss)/C:+6.0f}%')
