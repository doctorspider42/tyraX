"""kf_slow.py LOG... - the frames of the KF ring (frames 200..1199) whose
period went over one 60 Hz frame, with their breakdown (ms):
period pre work stall phys veh dmg fx scene speed."""
import re, sys
for fn in sys.argv[1:]:
    fr = {}
    for ln in open(fn, encoding='utf-8', errors='replace'):
        if not ln.startswith('LOG: KF ') or 'fields' in ln:
            continue
        for f, vals in re.findall(r'(\d+):([\d,]+)', ln):
            v = [int(x) for x in vals.split(',')]
            fr[int(f)] = v
    slow = [(f, v) for f, v in sorted(fr.items()) if v[0] > 1700]
    print('%s: %d frames, %d over 17 ms' % (fn, len(fr), len(slow)))
    for f, v in slow:
        print('  f%4d period %5.2f pre %5.2f work %5.2f stall %5.2f | phys %5.2f veh %5.2f dmg %5.2f fx %5.2f scene %5.2f spd %d' % (
            (f,) + tuple(x / 100 for x in v[:9]) + (v[9] if len(v) > 9 else 0,)))
