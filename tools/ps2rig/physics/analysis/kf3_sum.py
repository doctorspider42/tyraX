"""kf3_sum.py LOG - scene labels around the hit (ms), plus the kf2 view."""
import re, sys, subprocess

text = open(sys.argv[1], encoding='utf-8', errors='replace').read().splitlines()
names = {}
for l in text:
    m = re.search(r'KFLAB (\d+) (\S+)', l)
    if m:
        names[int(m.group(1))] = m.group(2)
lab = {}
for l in text:
    if 'KFL ' not in l:
        continue
    for m in re.finditer(r' (\d+):([\d,]*)', l):
        lab[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x != '']
recs = {}
for l in text:
    if 'KF ' not in l or 'fields' in l:
        continue
    for m in re.finditer(r'(\d+):([\d,]+)', l):
        recs[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x != '']
fs = sorted(recs)
drop, hit = 0, None
for a, b in zip(fs, fs[1:]):
    if recs[a][9] - recs[b][9] > drop:
        drop, hit = recs[a][9] - recs[b][9], b
frames = list(range(hit - 2, hit + 8))
base = [f for f in range(hit - 30, hit - 5)]
print(f'hit f={hit}; columns: before(avg {hit-30}..{hit-6}) then frames {frames[0]}..{frames[-1]} (ms)')
print('%-26s %6s ' % ('label', 'before') + ' '.join('%6d' % f for f in frames))
for k in sorted(names):
    vals = [lab.get(f, [0] * 48)[k] / 100 if k < len(lab.get(f, [])) else 0 for f in frames]
    b = sum(lab[f][k] for f in base if f in lab and k < len(lab[f])) / 100 / len(base)
    if max(vals + [b]) < 0.15:
        continue
    print('%-26s %6.2f ' % (names[k], b) + ' '.join('%6.2f' % v for v in vals))
print('%-26s %6s ' % ('SCENE (renderScene)', '') + ' '.join('%6.2f' % (recs[f][8] / 100) for f in frames))
print('%-26s %6s ' % ('period', '') + ' '.join('%6.2f' % (recs[f][0] / 100) for f in frames))
