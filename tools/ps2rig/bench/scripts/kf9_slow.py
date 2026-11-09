"""kf9_slow.py LOG... - R9-A: the KF ring's slow frames (period > 17 ms or
pre > 3 ms) with the K9 pre attribution, plus the K9AVG normal-frame baseline.
Units: ms. tPre ~= loop laps + out + pp (the 'chk' column is the residual)."""
import re, sys

L = ['clk/in/menu', 'player', 'scripts', 'stream', 'phys', 'veh+fx', 'portal',
     'particles', 'audio', 'cam/lights', 'settleSB', 'settleTerr', 'tail']
S = ['physPrebake', 'leave', 'promote', 'vehPrebake', 'smoke', 'skids', 'debris',
     'sbScan+rejoin', 'sbRebuild', 'sbPrebake', 'terrUpdate', 'terrPrebake',
     'scriptsOnly', 'streamOnly', 'vehCamera', 'lights', 'renderRebuild(work)',
     'releaseImm']
W = ['beginFrame', 'render', 'endFrame', 'extrap']
C = ['leaves', 'promotes', 'physPrebakes', 'vehPrebakes', 'rejoined', 'sbRebuilt',
     'sbPrebakes', 'terrBuilt', 'terrPrebakes', 'renderRebuilds', 'renderDemotes',
     'releaseImm', 'flags']
FLAGS = {1: 'vifBusy@L0', 2: 'vifBusy@B', 4: 'presentInEndFrame',
         8: 'presentInOut/pre', 16: 'presentInRender(split)'}


def ms(v):
    return v / 100.0


def nz(names, vals, scale=True, thr=0.05):
    out = []
    for n, v in zip(names, vals):
        x = ms(v) if scale else v
        if (scale and x >= thr) or (not scale and v):
            out.append('%s %s' % (n, ('%.2f' % x) if scale else str(v)))
    return ' '.join(out)


for fn in [a for a in sys.argv[1:] if a != "-a"]:
    kf, k9, avg = {}, {}, None
    for ln in open(fn, encoding='utf-8', errors='replace'):
        if ln.startswith('LOG: KF ') and 'fields' not in ln:
            for f, vals in re.findall(r'(\d+):([\d,]+)', ln):
                kf[int(f)] = [int(x) for x in vals.split(',')]
        m = re.match(r'LOG: K9 (\d+):([\d,]+)\|([\d,]+)\|([\d,]+)\|([\d,]+)\|(-?\d+),(\d+)', ln)
        if m:
            k9[int(m.group(1))] = ([int(x) for x in m.group(2).split(',')],
                                   [int(x) for x in m.group(3).split(',')],
                                   [int(x) for x in m.group(4).split(',')],
                                   [int(x) for x in m.group(5).split(',')],
                                   int(m.group(6)), int(m.group(7)))
        m = re.match(r'LOG: K9AVG n=(\d+) \(x100\):([\d,]+)', ln)
        if m:
            avg = (int(m.group(1)), [int(x) / 100.0 for x in m.group(2).split(',')])
    slow = [f for f, v in sorted(kf.items()) if v[0] > 1700 or v[1] > 300]
    print('%s: %d KF frames, %d K9 frames, %d slow' % (fn, len(kf), len(k9), len(slow)))
    if avg:
        n, a = avg
        nl, ns, nw = len(L), len(S), len(W)
        print('  AVG normal (n=%d): loopPre %.2f out %.2f pp+ %.2f' % (
            n, ms(sum(a[:nl])), ms(a[nl + ns + nw + 1]), ms(a[nl + ns + nw])))
        print('    laps: ' + nz(L, a[:nl], thr=0.01))
        print('    subs: ' + nz(S, a[nl:nl + ns], thr=0.01))
        print('    work: ' + nz(W, a[nl + ns:nl + ns + nw], thr=0.01))
    for f in slow:
        v = kf[f]
        print('  f%4d period %5.2f pre %5.2f work %5.2f stall %5.2f | phys %.2f veh %.2f dmg %.2f fx %.2f scene %.2f spd %d' % (
            (f,) + tuple(ms(x) for x in v[:9]) + (v[9],)))
        if f not in k9:
            print('        (no K9 line)')
            continue
        l, s, w, c, pp, out = k9[f]
        loop = sum(l)
        print('        loopPre %.2f out %.2f pp %.2f -> sum %.2f (chk %+.2f)' % (
            ms(loop), ms(out), ms(pp), ms(loop + out + pp), ms(v[1] - loop - out - pp)))
        print('        laps: ' + nz(L, l))
        print('        subs: ' + nz(S, s))
        print('        work: ' + nz(W, w) + ' (scene %.2f)' % ms(v[8]))
        flags = ' '.join(t for b, t in FLAGS.items() if c[12] & b)
        print('        cnt : ' + nz(C[:12], c[:12], scale=False) + ' | ' + flags)
    if '-a' in sys.argv:
        for f in sorted(k9):
            l, s, w, c, pp, out = k9[f]
            v = kf.get(f, [0] * 10)
            print('  f%4d per %5.2f pre %5.2f loop %5.2f out %4.2f pp %5.2f work %5.2f | %s' % (
                f, ms(v[0]), ms(v[1]), ms(sum(l)), ms(out), ms(pp), ms(v[2]), nz(L, l, thr=0.3)))
