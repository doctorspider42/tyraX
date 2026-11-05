import re, sys
def load(path):
    text = open(path, encoding='utf-8', errors='replace').read().splitlines()
    names = {}
    for l in text:
        m = re.search(r'KFLAB (\d+) (\S+)', l)
        if m: names[int(m.group(1))] = m.group(2)
    lab = {}
    for l in text:
        if 'KFL ' not in l: continue
        for m in re.finditer(r' (\d+):([\d,]*)', l):
            lab[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x != '']
    rows = {}
    fr = [f for f in lab if 500 <= f < 1100]
    for k, n in names.items():
        rows[n] = sum(lab[f][k] for f in fr if k < len(lab[f])) / 100 / max(len(fr), 1)
    return rows
a = load(sys.argv[1]); b = load(sys.argv[2])
for n in sorted(set(a) | set(b), key=lambda n: -(b.get(n, 0) - a.get(n, 0))):
    if max(a.get(n, 0), b.get(n, 0)) < 0.05: continue
    print('%-22s %6.2f %6.2f  %+6.2f' % (n, a.get(n, 0), b.get(n, 0), b.get(n, 0) - a.get(n, 0)))
