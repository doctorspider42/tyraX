"""Validate A02a event accounting after the unchanged P06 validation gates."""
import copy
import csv
import json
import re
import subprocess
import sys
from pathlib import Path

fixture, root, log = map(Path, sys.argv[1:4])
subprocess.run([sys.executable, str(Path(__file__).with_name('tyrax2-road-trace-analyze.py')),
                str(fixture), str(root), str(log)], check=True, stdout=subprocess.DEVNULL)
header = (fixture/'tyra/engine/inc/debug/motion_probe.hpp').read_text()
timings = [s.strip() for s in re.search(r'enum Timing \{(.*?)\};', header, re.S)[1].split(',')][:-1]
counters = [s.strip() for s in re.search(r'enum Counter \{(.*?)\};', header, re.S)[1].split(',')][:-1]
with (root/'road-bags.csv').open() as stream:
    rows = [{k: int(v) for k,v in r.items()} for r in csv.DictReader(stream)]


def validate(rs):
    assert len(rs) == 2220
    assert all(all(v >= 0 for v in r.values()) for r in rs)
    for r in rs:
        assert r['bakeTicks'] + r['finalizeTicks'] <= r['elapsed']
        assert bool(r['bakeCalls']) == bool(r['bakeTicks'])
        assert bool(r['finalizeCalls']) == bool(r['finalizeTicks'])
        assert r['growthBlocks'] <= r['bakeCalls']
        assert r['arenaBytes'] % 16 == 0
        assert bool(r['arenaBytes']) == bool(r['arenaBuilds'])
        assert r['arenaBuilds'] <= r['finalizeCalls']
        if r['arenaBuilds']:
            assert r['arenaBytes'] == 16*(r['payloadQwords']+r['headerQwords'])
        assert r['t'+str(timings.index('CoreCalls'))] == 1
    assert sum(r['arenaBuilds'] for r in rs) > 0
    assert sum(r['payloadQwords'] for r in rs) > 0


validate(rows)
rejected = []
for label, key, value in [('scope exceeds bag', 'bakeTicks', 10**10),
                          ('allocation mismatch', 'arenaBytes', 1),
                          ('growth without calls', 'growthBlocks', 10**9),
                          ('lost Core call', 't'+str(timings.index('CoreCalls')), 0)]:
    broken = copy.deepcopy(rows)
    broken[0][key] = value
    try:
        validate(broken)
    except AssertionError:
        rejected.append(label)
    else:
        raise RuntimeError('Accepted '+label)
events = []
for r in rows:
    if r['arenaBuilds'] or r['elapsed']/294912 > .6:
        events.append(dict(offset=r['offset'], id=r['id'], view=r['view'],
            elapsedMs=r['elapsed']/294912, bakeMs=r['bakeTicks']/294912,
            finalizeMs=r['finalizeTicks']/294912,
            prefixMs={k:r['t'+str(timings.index(k))]/294912 for k in timings if k.startswith('Prefix')},
            counters={k:r['c'+str(i)] for i,k in enumerate(counters) if r['c'+str(i)]},
            build={k:r[k] for k in ('bakeCalls','payloadQwords','headerQwords','growthBlocks',
                                  'capacityAddedQwords','arenaBytes','arenaBuilds')}))
result = dict(scope='road first-build events; elapsed scopes overlap existing Core/prefix buckets',
              rows=len(rows), negativeControlsRejected=rejected, events=events)
(root/'first-build-analysis.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
