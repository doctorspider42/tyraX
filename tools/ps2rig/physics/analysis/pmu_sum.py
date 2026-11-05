import re, sys
for fn in sys.argv[1:]:
    t=open(fn,errors='replace').read()
    rs={int(m.group(1)):[int(x) for x in m.group(2).split()] for m in re.finditer(r'KFRSUM (\d+)((?: \d+)+)',t)}
    pm=re.search(r'KFRPMU (\d+) (\d+) (\d+)',t)
    print(fn, 'frame I/D miss %.0f/%.0f' % (int(pm.group(2))/int(pm.group(1)), int(pm.group(3))/int(pm.group(1))) if pm else '')
    for r,v in sorted(rs.items()):
        if not v[0] or len(v)<29 or r==10: continue
        n=v[0]
        print('  r%d n/f %.1f us/bag %.1f I %.0f D %.0f | ms/f %.2f' % (r, n/900, v[2]/n/294.912, v[27]/n, v[28]/n, v[2]/294912/900))
