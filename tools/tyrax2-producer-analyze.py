"""Validate producer ledgers before reporting attribution; emulator is correctness only."""
import csv
import hashlib
import json
import math
import re
import statistics
import sys
from pathlib import Path

PRODUCERS = 'other terrain roads batches objects vehicles effects sky procedural'.split()
VIEWS = 'main reflection secondary unassigned'.split()
FLOATS = 'x y z speed yaw dt damage eyeX eyeY eyeZ lookX lookY lookZ'.split()
CLOCKS = 'coreTicks coreCalls waitTicks waitCalls'.split()

def read(path):
    with path.open() as stream:
        return [{k: float(v) if k in FLOATS else int(v) for k, v in r.items()}
                for r in csv.DictReader(stream)]

def analyze(rows, bins, log):
    metrics = list(rows[0])[29:]
    assert metrics[0] == 'bags' and metrics[-4:] == CLOCKS
    assert {r['phase'] for r in rows} == set(range(20))
    assert all(math.isfinite(v) for r in rows for v in r.values())
    records = re.findall(r'PRODUCERPHASE phase=(\d+) workload=(\d+) mode=(\d+) valid=(\d+) rows=(\d+)', log)
    assert len(records) == 20 and 'PRODUCERDONE valid=1' in log
    phases, detail = [], []
    for p in range(20):
        rs = [r for r in rows if r['phase'] == p]
        bs = [r for r in bins if r['phase'] == p]
        mode, drive = p % 10, int(p >= 10)
        assert tuple(map(int, records[p])) == (p, drive, mode, 1, len(rs))
        warm = 1200 if drive or p == 0 else 360
        assert [r['offset'] for r in rs] == list(range(warm, warm+len(rs)))
        assert all(r['mode'] == mode and r['workload'] == drive and r['errors'] == 0 for r in rs)
        assert all(r['whole'] == r['update']+r['render'] and 0 <= r['pacing'] <= r['whole'] and r['period'] > 0 for r in rs)
        assert all(0 < r['dt'] < .2 and r['vehicles'] == (3 if drive else 0) for r in rs)
        assert all(0 <= r['waitTicks'] <= r['coreTicks'] <= r['render'] for r in rs)
        if drive:
            assert all(r['driver'] >= 0 and abs(r['x']) < 2 and r['damage'] < .001 for r in rs)
            assert rs[-1]['z'] >= 108 and rs[-2]['z'] < 108
            assert [r['segment'] for r in rs] == [0]*120+[1]*(len(rs)-120)
            assert all(r['angle'] == 0 for r in rs)
            assert all(abs(r['speed']) < .5 and abs(r['z']+74.3035) < 1 for r in rs[:120])
        else:
            assert len(rs) == 600
            assert [r['angle'] for r in rs] == [0]*120+list(range(360))+[0]*120
            assert [r['segment'] for r in rs] == [0]*120+[1]*360+[2]*120
            assert all(r['driver'] == -1 and abs(r['eyeZ']+74.3035) < .001 for r in rs)
        def key(r):
            if not drive:
                return 0 if r['segment'] == 0 else 13 if r['segment'] == 2 else 1+r['angle']//30
            return 0 if r['segment'] == 0 else 1+max(0, min(11, int((r['z']+80)/20)))
        assert len(bs) == 36*len({key(r) for r in rs})
        assert len({(b['bin'], b['producer'], b['view']) for b in bs}) == len(bs)
        for b in bs:
            assert 0 <= b['producer'] < 9 and 0 <= b['view'] < 4
            group = [r for r in rs if key(r) == b['bin']]
            assert b['rows'] == len(group) > 0
            assert all(b[k] >= 0 for k in metrics)
            if b['snapshotBytes']:
                assert (b['producer'], b['view']) == (0, 3)
            assert b['deferredPush'] == b['deferredPop'], ('Deferred owner mismatch', p, b)
            if 3 <= mode <= 8 and b['coreCalls']:
                selected = {3: {1}, 4: {2}, 5: {4}, 6: {5}, 7: {3}, 8: {0, 6, 7, 8}}[mode]
                assert b['producer'] in selected and b['view'] != 3
            if any(b[k] for k in metrics):
                detail.append(dict(phase=p, bin=b['bin'], producer=PRODUCERS[b['producer']],
                    view=VIEWS[b['view']], rows=b['rows'],
                    means={k: b[k]/b['rows']/(294912 if k.endswith('Ticks') else 1) for k in metrics}))
        for b in {key(r) for r in rs}:
            for k in metrics:
                assert sum(r[k] for r in rs if key(r) == b) == sum(r[k] for r in bs if r['bin'] == b), (p, b, k)
        if mode == 1:
            assert sum(r['bags'] for r in rs) > 0
            assert all(all(r[k] == 0 for k in CLOCKS) for r in rs)
            reasons = 'missNew missBBox missContent missPrim missStreams missProgram missDepth missSize'.split()
            assert all(sum(r[k] for k in reasons) == r['bakeMiss'] for r in rs)
            assert all(r['replay'] <= r['direct'] and r['outside']+r['direct']+r['partial'] <= r['bags'] for r in rs)
            assert all(r['deferredPush'] == r['deferredPop'] for r in rs)
            if drive:
                assert sum(b['bags'] for b in bs if b['producer'] == 5) > 0
                assert sum(b['bags'] for b in bs if b['view'] == 1) > 0
            else:
                assert sum(b['bags'] for b in bs if b['producer'] == 5 or b['view'] == 1) == 0
        elif 3 <= mode <= 8:
            assert all(all(r[k] == 0 for k in metrics[:-4]) for r in rs)
        else:
            assert all(all(r[k] == 0 for k in metrics) for r in rs)
        segments = []
        for seg in sorted({r['segment'] for r in rs}):
            group = [r for r in rs if r['segment'] == seg]
            work = [(r['whole']-r['pacing'])/294912 for r in group]
            segments.append(dict(segment=seg, rows=len(group), workMeanMs=statistics.mean(work),
                workP95Ms=sorted(work)[int(.95*(len(work)-1))],
                loopHz=294912000/statistics.mean(r['period'] for r in group),
                coreMs=statistics.mean(r['coreTicks'] for r in group)/294912,
                explicitWaitMs=statistics.mean(r['waitTicks'] for r in group)/294912))
        phases.append(dict(phase=p, workload='drive' if drive else 'rotation', mode=mode, segments=segments))
    movement = []
    for p in range(20):
        rs = [r for r in rows if r['phase'] == p and r['segment'] == 1]
        bs = [b for b in bins if b['phase'] == p and b['bin'] not in (0, 13)]
        for producer in range(9):
            for view in range(4):
                selected = [b for b in bs if b['producer'] == producer and b['view'] == view]
                totals = {k: sum(b[k] for b in selected) for k in metrics}
                if any(totals.values()):
                    movement.append(dict(phase=p, producer=PRODUCERS[producer], view=VIEWS[view],
                        rows=len(rs), means={k: v/len(rs)/(294912 if k.endswith('Ticks') else 1) for k, v in totals.items()}))
        phases[p]['bins'] = []
        for b in sorted({key_for_report(r, p >= 10) for r in rs}):
            group = [r for r in rs if key_for_report(r, p >= 10) == b]
            phases[p]['bins'].append(dict(bin=b, rows=len(group),
                workMs=statistics.mean(r['whole']-r['pacing'] for r in group)/294912,
                coreMs=statistics.mean(r['coreTicks'] for r in group)/294912))
    return dict(phases=phases, attribution=detail, movement=movement,
        units='Ticks converted to ms at 294912/ms. Core elapsed is not pure EE. Emulator timing is not hardware performance.')

def key_for_report(r, drive):
    return 1+max(0, min(11, int((r['z']+80)/20))) if drive else 1+r['angle']//30

if __name__ == '__main__':
    root, logfile = Path(sys.argv[1]), Path(sys.argv[2])
    result = analyze(read(root/'producer-probe.csv'), read(root/'producer-bins.csv'), logfile.read_text(errors='replace'))
    result['sha256'] = {n: hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ('producer-probe.csv', 'producer-bins.csv')}
    (root/'producer-analysis.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result['phases'], indent=2))
