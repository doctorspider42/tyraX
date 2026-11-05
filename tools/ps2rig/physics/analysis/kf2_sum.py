"""kf2_sum.py LOG - the game/kf2.py timeline around the hit (0.01 ms units)."""
import re, sys

recs = {}
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    if 'KF ' not in line or 'fields' in line:
        continue
    for m in re.finditer(r'(\d+):([\d,]+)', line):
        recs[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x != '']
fs = sorted(recs)
drop, hit = 0, None
for a, b in zip(fs, fs[1:]):
    if recs[a][9] - recs[b][9] > drop:
        drop, hit = recs[a][9] - recs[b][9], b
print(f'hit at f={hit} speed {recs[hit-1][9]/10:.1f} -> {recs[hit][9]/10:.1f}')
print('  f  period   pre  work | veh   dmg: cap  dent  coll   det(n) | scene  cars | rebake patch refresh')
for f in range(hit - 3, hit + 12):
    r = recs.get(f)
    if not r:
        continue
    ms = lambda k: r[k] / 100
    print(f'{f:4d} {ms(0):6.2f} {ms(1):5.2f} {ms(2):5.2f} | {ms(5):5.2f} {ms(6):5.2f}: {ms(10):4.2f} {ms(11):5.2f} {ms(12):5.2f} {ms(13):5.2f}({r[14]}) '
          f'| {ms(8):5.2f} {ms(15):5.2f} | {r[16]:3d} {r[17]:3d} {r[18]:3d}{"  MISS" if r[0] > 1750 else ""}')
before = [recs[f] for f in fs if hit - 60 <= f < hit - 5]
print('before (avg 55 frames): scene %.2f cars %.2f rebake/f %.2f' % (
    sum(r[8] for r in before) / 100 / len(before), sum(r[15] for r in before) / 100 / len(before),
    sum(r[16] for r in before) / len(before)))
print('misses 0-60 after:', sum(1 for f in fs if hit <= f < hit + 60 and recs[f][0] > 1750))
