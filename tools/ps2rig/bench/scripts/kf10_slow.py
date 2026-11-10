"""kf10_slow.py LOG... - R10-A render-work attribution (patch_k10.py +
engine_p10.py). For every frame over 16.7 ms (KF period > 1700), plus
frames 375-390 and 750-760, and the K10AVG normal-frame average, prints:
  work laps (beginFrame..endFrame) with the StaPip flush / direct renderNow
  time inside each lap, the endFrame internals, StaPip routes and tags,
  bakes / patch rebakes / refreshes, immutable streams and GPU waits, and
  the pre side's crash sub-steps. Units: ms unless marked n.
Options: -w (only the windows + slow frames, default) -a (every K10 frame)."""
import re, sys

LAPS = ['beginFrame', 'pipe+dayNight', 'sceneHead', 'sky', 'terrain+occl',
        'staticBatches', 'roads/proc', 'objects', 'wheels', 'debris',
        'anim..beams', 'highlights', 'particles', 'sceneTail', 'postScene',
        'hud/2d', 'endFrame(+stall)']
G = ['settleSB', 'settleTerr', 'dmgCapture', 'dentApply', 'collapse', 'detach',
     'spawnDebris', 'vehPrebake', 'paintMapAhead', 'vehObjDraw', 'vehEnvPass',
     'otherObjDraw', 'glass', 'skids', 'smoke', 'glow']
C = ['dmgCapture', 'dentApply', 'dentQueued', 'collapse', 'detach', 'debris',
     'vehPrebakeKind', 'patchBegin', 'patchEnd', 'lampBreak', 'paintReshade',
     'vehObjs', 'otherObjs', 'flags', 'c14', 'c15']
ROUTES = ['stamp', 'culled', 'directReplay', 'directBake', 'directPlain',
          'partialReplay', 'partial/pkgs', 'other']
TAGS = ['other', 'sky', 'terrain', 'staticBatch', 'roads/proc', 'objects',
        'carObjs', 'wheels', 'fx', 'postObj']
KIND = {0: '-', 1: 'body', 2: 'shine', 3: 'paintmap'}
NL, NG, NE = 17, 16, 96


def parse(fn):
    kf, a, b, avg, mask = {}, {}, {}, None, None
    for ln in open(fn, encoding='utf-8', errors='replace'):
        if ln.startswith('LOG: KF ') and 'fields' not in ln:
            for f, vals in re.findall(r'(\d+):([\d,]+)', ln):
                kf[int(f)] = [int(x) for x in vals.split(',')]
            continue
        m = re.search(r'mask (\d{96})', ln)
        if m and 'K10 fields' in ln:
            mask = m.group(1)
        m = re.match(r'LOG: K10A (\d+):([\d,|]+)', ln)
        if m:
            grp = [[int(x) for x in g.split(',')] for g in m.group(2).strip().split('|')]
            a[int(m.group(1))] = grp
            continue
        m = re.match(r'LOG: K10B (\d+):([\d,]+)', ln)
        if m:
            b[int(m.group(1))] = [int(x) for x in m.group(2).split(',')]
            continue
        m = re.match(r'LOG: K10AVG n=(\d+) \(x100\):([\d,]+)', ln)
        if m:
            avg = (int(m.group(1)), [int(x) / 100.0 for x in m.group(2).split(',')])
    return kf, a, b, avg, mask


def show(l, fl, dl, g, c, e, w, work=None, stall=None):
    """All time inputs in 0.01 ms (floats allowed), counts as is."""
    ms = lambda v: v / 100.0
    out = []
    # ---- work laps
    laps = []
    for i, n in enumerate(LAPS):
        if l[i] < 2 and fl[i] < 2 and dl[i] < 2:
            continue
        extra = []
        if fl[i] >= 2: extra.append('fl %.2f' % ms(fl[i]))
        if dl[i] >= 2: extra.append('dir %.2f' % ms(dl[i]))
        laps.append('%s %.2f%s' % (n, ms(l[i]), (' (' + ' '.join(extra) + ')') if extra else ''))
    lapSum = sum(l[:16])
    out.append('    work laps (sum L0..L15 %.2f + endFrame %.2f):' % (ms(lapSum), ms(l[16])))
    for k in range(0, len(laps), 4):
        out.append('      ' + ' | '.join(laps[k:k + 4]))
    ef = e[77:82]
    efs = sum(ef)
    tail = 'drain %.2f' % ms(w)
    if stall is not None:
        tail += ' stall %.2f resid %.2f' % (ms(stall), ms(l[16] - efs - w - stall - e[82]))
    if work is not None:
        tail += ' | work chk %+.2f' % ms(work - lapSum - efs - w)
    out.append('    endFrame: fence %.2f vifDrain %.2f wrap %.2f postFx %.2f endRec %.2f | %s | onFrameEnd(after present) %.2f' % (
        tuple(ms(x) for x in ef) + (tail, ms(e[82]))))
    # ---- StaPip
    flush = e[0]
    stampT = e[3] - e[6]
    out.append('    StaPip: flush %.2f (n %d, draws %d) pass %.2f [stamps %d culls %d, stamp-only %.2f] '
               'renderNow in flush %.2f (n %d) all %.2f (n %d)' % (
                   ms(flush), e[1], e[2], ms(e[3]), e[4], e[5], ms(stampT), ms(e[6]), e[7], ms(e[83]), e[84]))
    rt = ['%s %d/%.2f' % (ROUTES[r], e[16 + r], ms(e[8 + r])) for r in range(8) if e[16 + r]]
    out.append('      routes (n/ms): ' + ' '.join(rt))
    tg = ['%s %d/%.2f+%ds' % (TAGS[t], e[34 + t], ms(e[24 + t]), e[44 + t]) for t in range(10)
          if e[34 + t] or e[44 + t]]
    out.append('      tags (renderNow n/ms + pass stamps): ' + ' '.join(tg))
    out.append('      bakes: lookup %.2f (n %d) | refresh %d patch %d churnSkip %d rebuild %d new %d | '
               'patchBaked %.2f partialBuild %.2f (n %d) | prebakeBag %.2f (n %d; refresh %d patch %d '
               'churn %d rebuild %d new %d, patched %.2f built %.2f n %d)' % (
                   ms(e[54]), e[55], e[56], e[57], e[58], e[59], e[60], ms(e[61]), ms(e[62]), e[63],
                   ms(e[64]), e[65], e[86], e[87], e[88], e[89], e[90], ms(e[91]), ms(e[92]), e[93]))
    out.append('      immutable: reg %d unreg %d (%.2f) retire %d | bag release %d (%.2f) bag retire %d | '
               'GPU waits: ringFull %.2f waitFor %.2f drainSpin %.2f drains %d' % (
                   e[66], e[67], ms(e[68]), e[69], e[70], ms(e[71]), e[72], ms(e[73]), ms(e[74]),
                   ms(e[75]), e[76]))
    # ---- game side
    gs = ' '.join('%s %.2f' % (G[i], ms(g[i])) for i in range(16) if g[i] >= 1)
    cs = ' '.join('%s %s' % (C[i], KIND.get(int(c[i]), str(c[i])) if i == 6 else
                             (('%.1f' % c[i]) if isinstance(c[i], float) else str(c[i])))
                  for i in range(14) if c[i])
    out.append('    game: ' + gs)
    out.append('    cnt : ' + cs)
    return out


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('-')]
    every = '-a' in sys.argv
    for fn in args:
        kf, a, b, avg, mask = parse(fn)
        slow = [f for f, v in sorted(kf.items()) if v[0] > 1700]
        print('%s: %d KF frames, %d K10 frames, over 16.7 ms: %s' % (
            fn, len(kf), len(a), ' '.join('f%d' % f for f in slow) or '-'))
        if avg:
            n, v = avg
            o = 0
            l = v[o:o + NL]; o += NL
            fl = v[o:o + NL]; o += NL
            dl = v[o:o + NL]; o += NL
            g = v[o:o + NG]; o += NG
            c = v[o:o + NG]; o += NG
            e = v[o:o + NE]; o += NE
            w = v[o]
            norm = [x for f, x in kf.items() if x[0] <= 1700 and f >= 210 and
                    not (375 <= f <= 390 or 750 <= f <= 760) and x[2] <= 1200]
            if norm:
                mean = lambda i: sum(x[i] for x in norm) / len(norm) / 100.0
                print('  NORMAL avg (K10 n=%d, KF n=%d): pre %.2f work %.2f stall %.2f | phys %.2f veh %.2f dmg %.2f fx %.2f scene %.2f' % (
                    n, len(norm), mean(1), mean(2), mean(3), mean(4), mean(5), mean(6), mean(7), mean(8)))
                st = sum(x[3] for x in norm) / len(norm)
            else:
                st = None
            for ln in show(l, fl, dl, g, c, e, w, work=(mean(2) * 100 if norm else None), stall=st):
                print(ln)
        frames = sorted(a) if every else sorted(
            set(slow) | {f for f in a if 375 <= f <= 390 or 750 <= f <= 760})
        for f in frames:
            v = kf.get(f)
            if v:
                print('  f%4d period %5.2f pre %5.2f work %5.2f stall %5.2f | phys %.2f veh %.2f dmg %.2f fx %.2f scene %.2f spd %.1f%s' % (
                    (f,) + tuple(x / 100.0 for x in v[:9]) + (v[9] / 10.0, '  <== MISSED' if v[0] > 1700 else '')))
            if f not in a or f not in b:
                print('    (no K10 line)')
                continue
            l, fl, dl, g, c, w = a[f][0], a[f][1], a[f][2], a[f][3], a[f][4], a[f][5][0]
            flags = []
            if c[13] & 1: flags.append('presentInEndFrame')
            if c[13] & 2: flags.append('vif1BusyAtBegin')
            for ln in show(l, fl, dl, g, c, b[f], w, work=v[2] if v else None, stall=v[3] if v else None):
                print(ln)
            if flags:
                print('    flags: ' + ' '.join(flags))


if __name__ == '__main__':
    main()
