"""ab_sum.py LOG... - per log: misses, work median/p95 (KF), StaPip route
ms/frame and sends/frame (KFRSUM, window 900 frames), KFRCLS."""
import re, sys
T = 294912.0
for fn in sys.argv[1:]:
    txt = open(fn, encoding='utf-8', errors='replace').read()
    kf = {}
    for m in re.finditer(r'KF((?: \d+:[\d,]+)+)', txt):
        for mm in re.finditer(r'(\d+):([\d,]+)', m.group(1)):
            kf[int(mm.group(1))] = [int(x) for x in mm.group(2).split(',') if x]
    w = sorted(v[2] for f, v in kf.items() if f >= 300)
    sc = sorted(v[8] for f, v in kf.items() if f >= 300)
    miss = sum(1 for f, v in kf.items() if v[0] > 1750)
    rs = {}
    for m in re.finditer(r'KFRSUM (\d+)((?: \d+)+)', txt):
        rs[int(m.group(1))] = [int(x) for x in m.group(2).split()]
    tot = sum(v[2] for v in rs.values()) / T / 900
    sends = sum(v[19] for v in rs.values() if len(v) > 19) / 900
    sendt = sum(v[20] for v in rs.values() if len(v) > 20) / T / 900
    cls = re.search(r'KFRCLS (\d+) (\d+)', txt)
    arm = re.search(r'ARMSEL (\d+)', txt)
    print('%s arm %s frames %d miss %d work p50 %.2f p95 %.2f scene p50 %.2f | stapip %.2f ms sends %.1f (%.2f ms) | %s' % (
        fn, arm.group(1) if arm else '?', len(kf), miss,
        w[len(w)//2]/100 if w else 0, w[len(w)*95//100]/100 if w else 0, sc[len(sc)//2]/100 if sc else 0,
        tot, sends, sendt,
        ' '.join('r%d %.2f/%.1f' % (r, v[2]/T/900, v[0]/900) for r, v in sorted(rs.items()) if v[0])))
    if cls: print('   classify %.2f us/call, %d calls/frame' % (int(cls.group(1))/int(cls.group(2))/294.912, int(cls.group(2))/900))
