"""settle_sum.py LOG - per-frame settle / batch / skid labels (game/settlelap.py)."""
import re, sys
kfl, kf = {}, {}
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    t = kfl if 'KFL ' in line else (kf if ('KF ' in line and 'fields' not in line) else None)
    if t is None:
        continue
    for m in re.finditer(r'(\d+):([\d,]+)', line):
        t[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x]
print('frame pre work stall | 4batch 23part 38skid | 40settle 41prebake 42ok')
chg = []
for f in sorted(kfl):
    l = kfl[f]; k = kf.get(f, [0] * 9)
    if len(l) < 44:
        continue
    if l[40]:
        chg.append(l[4])
    if l[40] or k[0] > 1750 or 482 <= f <= 490:
        print(f, k[1], k[2], k[3], '|', l[4], l[23], l[38], '|', l[40], l[41], l[42])
if chg:
    print('batch label on change frames: mean %.2f ms over %d' % (sum(chg) / len(chg) / 100, len(chg)))
