"""kf_sum.py LOG - per-frame timeline from a patch_kf arm.
Finds the hit (largest one-frame speed drop), prints every frame from 10
before to 60 after with its terms, marks frames whose period > 17.5 ms, and
sums misses before/after the hit."""
import re, sys

recs = {}
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    if 'KF ' not in line or 'fields' in line:
        continue
    for m in re.finditer(r'(\d+):([\d,]+)', line):
        v = [int(x) for x in m.group(2).split(',')]
        recs[int(m.group(1))] = v  # period pre work stall phys veh dmg fx scene speed10
fs = sorted(recs)
# record at frame f holds the PREVIOUS frame's FrameProfile terms but this
# frame's... no: scopes are reset at loop start after recording, so both
# describe the frame that ended right before f.
drop, hit = 0, None
for a, b in zip(fs, fs[1:]):
    d = recs[a][9] - recs[b][9]
    if d > drop:
        drop, hit = d, b
print(f'{len(fs)} frames, hit at f={hit} (speed {recs[hit-1][9]/10:.1f} -> {recs[hit][9]/10:.1f})')
hdr = 'f      period   pre  work stall | phys   veh   dmg    fx scene | spd'
print(hdr)
for f in range(hit - 10, hit + 61):
    if f not in recs:
        continue
    p, pre, w, st, ph, ve, dm, fx, sc, sp = [x / 100 for x in recs[f][:9]] + [recs[f][9] / 10]
    mark = ' MISS' if p > 17.5 else ''
    print(f'{f:5d} {p:7.2f} {pre:5.2f} {w:5.2f} {st:5.2f} | {ph:5.2f} {ve:5.2f} {dm:5.2f} {fx:5.2f} {sc:5.2f} | {sp:5.1f}{mark}')


def misses(lo, hi):
    return sum(1 for f in fs if lo <= f < hi and recs[f][0] > 1750)


print(f'misses: 200 frames before hit {misses(hit-200, hit)}, 0-60 after {misses(hit, hit+60)}, '
      f'60-300 after {misses(hit+60, hit+300)}, rest {misses(hit+300, 1200)}')


def avg(lo, hi, k):
    v = [recs[f][k] / 100 for f in fs if lo <= f < hi]
    return sum(v) / max(len(v), 1)


h = recs[hit]
print(f'ONE hit_speed {recs[hit-1][9]/10:.1f} | before(100) pre {avg(hit-100, hit, 1):.2f} work {avg(hit-100, hit, 2):.2f} phys {avg(hit-100, hit, 4):.2f} '
      f'| hit pre {h[1]/100:.2f} work {h[2]/100:.2f} dmg {h[6]/100:.2f} scene {h[8]/100:.2f} '
      f'| after(1-10) work {avg(hit+1, hit+11, 2):.2f} | miss 0-60 {misses(hit, hit+60)} all {misses(200, 1200)}')
