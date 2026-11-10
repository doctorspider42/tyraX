"""kf12_sum.py LOG... - R12-0 scene-1 attribution summary (BR12A:
patch_k12.py + engine_p12.py on the 2.13.0 engine).

For each log, and for all logs pooled (weighted by frame count), prints for
NORMAL frames (K12AVG: f >= 210, not slow, not 375..390 / 750..760) and the
DRIVE/IMPACT window (K12WIN: frames 250..450):
  - the components' ms, I and D misses a frame and their miss share at the
    fitted prices (I 51 cycles, D 16 cycles, 294.912 MHz): the whole frame
    without the present (vsync wait + flip), the game's pre-beginFrame update
    and its physics, the StaPip flush, the stamp pass (net of its general
    draws and crossing emits), the crossing pass (pre-pass + emits) and the
    renderNow remainder;
  - counts (stamps, crossing draws, flushes, renderNow calls);
  - renderNow by route and by game tag;
  - the GPU waits;
then every frame over 16.7 ms (KF period > 1700) with its own split.
Options: -a  also one line per frame for 250..450."""
import re, sys

NL, NG, NC, NP, NE = 17, 16, 16, 8, 118
NS = NL + NG + NC + 1 + NP + NE
OL, OG, OC, OW, OP, OE = 0, NL, NL + NG, NL + NG + NC, NL + NG + NC + 1, NL + NG + NC + 1 + NP
CYC_MS = 294912.0
PI, PD = 51.0, 16.0
ROUTES = ['stampHit(rn)', 'culled', 'wholeReplay', 'directBake', 'directPlain',
          'partialReplay', 'partialPkgs', 'other']
TAGS = ['other', 'sky', 'terrain', 'staticBatch', 'roads/proc', 'objects',
        'carObjs', 'wheels', 'fx', 'postObj']
LAPS = ['beginFrame', 'pipe+dayNight', 'sceneHead', 'sky', 'terrain+occl',
        'staticBatches', 'roads/proc', 'objects', 'wheels', 'debris',
        'anim..beams', 'highlights', 'particles', 'sceneTail', 'postScene',
        'hud/2d', 'endFrame(+present)']


def parse(fn):
    kf, a, b, sets = {}, {}, {}, {}
    for ln in open(fn, encoding='utf-8', errors='replace'):
        if 'KF ' in ln and 'fields' not in ln and 'K12' not in ln and 'KFL' not in ln:
            for f, vals in re.findall(r'(\d+):([\d,]+)', ln):
                v = [int(x) for x in vals.split(',') if x]
                if len(v) >= 10:
                    kf[int(f)] = v
            continue
        m = re.search(r'K12A (\d+):([\d,|]+)', ln)
        if m:
            a[int(m.group(1))] = [[int(x) for x in g.split(',') if x]
                                  for g in m.group(2).strip().split('|')]
            continue
        m = re.search(r'K12B (\d+):([\d,]+)', ln)
        if m:
            b[int(m.group(1))] = [int(x) for x in m.group(2).split(',') if x]
            continue
        m = re.search(r'(K12AVG|K12WIN) n=(\d+) \(x100\):([\d,]+)', ln)
        if m:
            v = [int(x) / 100.0 for x in m.group(3).split(',') if x]
            if len(v) == NS:
                sets[m.group(1)] = (int(m.group(2)), v)
    return kf, a, b, sets


def frame_vec(a, b, f):
    if f not in a or f not in b:
        return None
    g = a[f]
    if len(g) != 5 or len(b[f]) != NE:
        return None
    return [float(x) for x in g[0] + g[1] + g[2] + g[3] + g[4] + b[f]]


def kf_sel(kf, which):
    if which == 'K12AVG':
        return [f for f, x in kf.items() if 200 <= f < 1200 and f >= 210 and x[0] <= 1700 and
                x[2] <= 1200 and not (375 <= f <= 390 or 750 <= f <= 760)]
    return [f for f in kf if 250 <= f <= 450]


def kf_mean(kf, fs, i):
    return sum(kf[f][i] for f in fs) / len(fs) / 100.0 if fs else float('nan')


def share(ms, i, d):
    mi, md = i * PI / CYC_MS, d * PD / CYC_MS
    tot = (mi + md) / ms * 100.0 if ms > 0 else 0.0
    return mi, md, tot


def comp_line(name, ms, i, d):
    mi, md, tot = share(ms, i, d)
    return '    %-34s %6.2f ms  I %7.0f (%5.2f ms)  D %7.0f (%5.2f ms)  misses %5.1f %%' % (
        name, ms, i, mi, d, md, tot)


def show(v, frame_ms=None, stall_ms=None, work_ms=None, pre_kf=None):
    """v: one vector in the record's units (times 0.01 ms)."""
    E = v[OE:OE + NE]
    P = v[OP:OP + NP]
    t = lambda x: x / 100.0
    out = []
    if frame_ms is not None:
        out.append(comp_line('frame without present', frame_ms, P[0] - E[114], P[1] - E[115]))
    if stall_ms is not None:
        out.append(comp_line('present (vsync wait + flip)', stall_ms, E[114], E[115]))
    out.append(comp_line('pre: update before beginFrame', t(P[2]), P[3], P[4]))
    out.append(comp_line('  of it physics', t(P[5]), P[6], P[7]))
    if work_ms is not None:
        out.append('    %-34s %6.2f ms' % ('work (beginFrame..present, KF)', work_ms))
    out.append(comp_line('StaPip flush (all)', t(E[0]), E[1], E[2]))
    out.append(comp_line('  stamp pass, net', t(E[5] - E[25] - E[16]), E[6] - E[26] - E[17],
                         E[7] - E[27] - E[18]))
    out.append(comp_line('  crossing pass (pre-pass + emits)', t(E[11] + E[16]), E[12] + E[17],
                         E[13] + E[18]))
    out.append(comp_line('    pre-pass (crossClassify)', t(E[11]), E[12], E[13]))
    out.append(comp_line('    emits (crossEmit)', t(E[16]), E[17], E[18]))
    out.append(comp_line('  general draws from the pass', t(E[25]), E[26], E[27]))
    out.append(comp_line('renderNow remainder (all)', t(E[21]), E[22], E[23]))
    out.append('    renderNow: n %.1f | from the pass %.1f (%.2f ms) | in a flush, not the pass %.1f '
               '(%.2f ms) | outside any flush %.1f (%.2f ms) | tryStamp misses %.1f (%.2f ms)' % (
                   E[24], E[28], t(E[25]), E[30], t(E[29]), E[24] - E[28] - E[30],
                   t(E[21] - E[25] - E[29]), E[32], t(E[31])))
    out.append('    counts: flushes %.1f (draws %.1f) | pass calls %.1f stamps %.1f culls %.1f | '
               'crossing bags classified %.1f, crossing draws emitted %.1f, declined %.1f | '
               'prebakeBag %.1f (%.2f ms)' % (E[3], E[4], E[8], E[9], E[10], E[15], E[19], E[20],
                                              E[117], t(E[116])))
    if E[9] > 0:
        out.append('    per draw: stamp %.1f us (net pass / stamps)' % (
            (E[5] - E[25] - E[16]) * 10.0 / E[9]))
    if E[19] > 0:
        out.append('    per crossing draw: %.1f us (pre-pass + emit) / emitted' % (
            (E[11] + E[16]) * 10.0 / E[19]))
    rt = []
    for r in range(8):
        n = E[41 + r]
        if n <= 0.0:
            continue
        ms = t(E[33 + r])
        mi, md, tot = share(ms, E[49 + r], E[57 + r])
        rt.append('      %-14s n %5.1f  %5.2f ms  %6.1f us/draw  I %6.0f D %6.0f  misses %5.1f %%' % (
            ROUTES[r], n, ms, ms * 1000.0 / n, E[49 + r], E[57 + r], tot))
    out.append('    renderNow by route:')
    out.extend(rt)
    tg = ['%s %.1f/%.2f+%.1fs+%.1fx' % (TAGS[k], E[75 + k], t(E[65 + k]), E[85 + k], E[95 + k])
          for k in range(10) if E[75 + k] or E[85 + k] or E[95 + k]]
    out.append('    by tag (renderNow n/ms + pass stamps + crossing draws):')
    for k in range(0, len(tg), 5):
        out.append('      ' + '  '.join(tg[k:k + 5]))
    out.append('    GPU waits: ringFull %.3f waitFor %.3f drainSpin %.3f (drains %.1f) | GS FINISH '
               'waits %.3f (n %.1f) | pipelined present: waitFor %.3f + GS tail %.3f (n %.1f) | '
               'fairness drain %.3f ms' % (
                   t(E[105]), t(E[106]), t(E[107]), E[108], t(E[109]), E[110], t(E[111]),
                   t(E[112]), E[113], t(v[OW])))
    laps = ['%s %.2f' % (LAPS[k], t(v[OL + k])) for k in range(NL) if v[OL + k] >= 2]
    out.append('    laps:')
    for k in range(0, len(laps), 6):
        out.append('      ' + ' | '.join(laps[k:k + 6]))
    return out


def pooled(items):
    """items: [(n, vec)] -> weighted mean vec, total n."""
    n = sum(x[0] for x in items)
    if not n:
        return 0, None
    return n, [sum(x[0] * x[1][i] for x in items) / n for i in range(NS)]


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('-')]
    every = '-a' in sys.argv
    pool = {'K12AVG': [], 'K12WIN': []}
    poolkf = {'K12AVG': [], 'K12WIN': []}
    for fn in args:
        kf, a, b, sets = parse(fn)
        slow = [f for f, v in sorted(kf.items()) if v[0] > 1700]
        print('%s: %d KF frames, %d K12 frames, over 16.7 ms: %s' % (
            fn, len(kf), len(a), ' '.join('f%d' % f for f in slow) or '-'))
        for which, label in (('K12AVG', 'NORMAL frames'), ('K12WIN', 'DRIVE/IMPACT window 250..450')):
            if which not in sets:
                print('  %s: no %s line' % (label, which))
                continue
            n, v = sets[which]
            fs = kf_sel(kf, which)
            per = kf_mean(kf, fs, 0)
            st = kf_mean(kf, fs, 3)
            print('  %s (K12 n=%d, KF n=%d): period %.2f pre %.2f work %.2f present %.2f | phys %.2f '
                  'veh %.2f dmg %.2f fx %.2f scene %.2f' % (
                      label, n, len(fs), per, kf_mean(kf, fs, 1), kf_mean(kf, fs, 2), st,
                      kf_mean(kf, fs, 4), kf_mean(kf, fs, 5), kf_mean(kf, fs, 6), kf_mean(kf, fs, 7),
                      kf_mean(kf, fs, 8)))
            for ln in show(v, frame_ms=per - st, stall_ms=st, work_ms=kf_mean(kf, fs, 2)):
                print(ln)
            pool[which].append((n, v))
            poolkf[which].extend((kf[f][0] - kf[f][3], kf[f][3], kf[f][2]) for f in fs)
        for f in slow:
            x = kf[f]
            print('  f%4d period %5.2f pre %5.2f work %5.2f present %5.2f | phys %.2f veh %.2f dmg %.2f '
                  'fx %.2f scene %.2f spd %.1f  <== MISSED' % (
                      (f,) + tuple(y / 100.0 for y in x[:9]) + (x[9] / 10.0,)))
            v = frame_vec(a, b, f)
            if v is None:
                print('    (no K12 line)')
                continue
            for ln in show(v, frame_ms=(x[0] - x[3]) / 100.0, stall_ms=x[3] / 100.0,
                           work_ms=x[2] / 100.0):
                print(ln)
            c13 = int(v[OC + 13])
            if c13:
                print('    flags: ' + ' '.join(s for bit, s in ((1, 'presentInEndFrame'),
                                                                  (2, 'vif1BusyAtBegin')) if c13 & bit))
        if every:
            print('  per frame 250..450: f period work | flush stampNet cross(pre+emit) rnAll | '
                  'stamps xDraws rnN | frameI frameD')
            for f in range(250, 451):
                v = frame_vec(a, b, f)
                if v is None or f not in kf:
                    continue
                E, P = v[OE:], v[OP:OP + NP]
                print('    f%d %5.2f %5.2f | %5.2f %5.2f %5.2f %5.2f | %3d %3d %3d | %6d %6d' % (
                    f, kf[f][0] / 100.0, kf[f][2] / 100.0, E[0] / 100.0,
                    (E[5] - E[25] - E[16]) / 100.0, (E[11] + E[16]) / 100.0, E[21] / 100.0,
                    E[9], E[19], E[24], P[0] - E[114], P[1] - E[115]))
    if len(args) > 1:
        for which, label in (('K12AVG', 'NORMAL frames'), ('K12WIN', 'DRIVE/IMPACT window 250..450')):
            n, v = pooled(pool[which])
            k = poolkf[which]
            if not n or not k:
                continue
            fr = sum(x[0] for x in k) / len(k) / 100.0
            st = sum(x[1] for x in k) / len(k) / 100.0
            wk = sum(x[2] for x in k) / len(k) / 100.0
            print('ALL %d logs, %s (K12 n=%d, KF n=%d):' % (len(args), label, n, len(k)))
            for ln in show(v, frame_ms=fr, stall_ms=st, work_ms=wk):
                print(ln)


if __name__ == '__main__':
    main()
