import re, sys
for fn in sys.argv[1:]:
    kf = {}
    for line in open(fn, encoding='utf-8', errors='replace'):
        if 'KF ' in line and 'fields' not in line:
            for m in re.finditer(r'(\d+):([\d,]+)', line):
                kf[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x]
    miss = [f for f in sorted(kf) if kf[f][0] > 1750]
    w = sorted(kf[f][2] for f in kf)
    print(fn, 'frames', len(kf), 'misses', len(miss), miss[:30], 'work median %.2f p95 %.2f' % (w[len(w)//2]/100, w[len(w)*95//100]/100) if w else '')
