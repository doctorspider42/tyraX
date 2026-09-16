import csv, sys, statistics, os
base = sys.argv[1]
def load(p):
    with open(p, newline='') as f:
        return list(csv.DictReader(f))
phases = {0: 'garage day', 1: 'garage night', 2: 'outer day', 3: 'outer night'}
for arm in ['control-after-boot1', 'control-after-boot2', 'attrib-after']:
    a = os.path.join(base, arm + '-frame-attrib.csv')
    c = os.path.join(base, arm + '-frame-cost.csv')
    if not os.path.exists(a):
        continue
    ra, rc = load(a), load(c)
    print('==', arm)
    for ph, nm in phases.items():
        pa = [r for r in ra if int(r['phase']) == ph]
        pc = [r for r in rc if int(r['phase']) == ph]
        if not pa:
            continue
        def m(rows, k):
            return statistics.mean(float(r[k]) for r in rows)
        pkgs = m(pa, 'dsPackages')
        if pkgs == 0:
            print(f'  {nm:14s} (attribution counters compiled out)')
            continue
        db, pb = m(pa, 'dsDirectBags'), m(pa, 'dsPartialBags')
        calls, culled = m(pa, 'spCalls'), m(pa, 'spCulled')
        fl, tri = m(pc, 'flushes'), m(pc, 'triangles')
        print(f'  {nm:14s} n={len(pa):4d} packages={pkgs:8.1f} flushes={fl:6.1f} '
              f'GSprims={tri:9.1f} prims/pkg={tri/pkgs:6.2f} '
              f'bags direct/partial={db:5.1f}/{pb:5.1f} calls={calls:5.1f} culled={culled:5.1f} '
              f'implied verts@72={pkgs*72:9.0f}')
