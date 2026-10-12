"""Qualify isolated A41 timer observations, never copy budget or game FPS."""
import argparse
import hashlib
import json
import re
import statistics
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('root', type=Path)
p.add_argument('--platform', choices=['emulator', 'physical'], required=True)
p.add_argument('--output', type=Path, help='Write reanalysis outside an immutable archive')
a = p.parse_args()
root = a.root.resolve()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
build = json.loads((root / 'build.json').read_text())
assert all(sha(root / n) == h for n, h in build['files'].items())
folder = root / a.platform
launch = json.loads((folder / ('launch.json' if a.platform == 'emulator' else 'editor-deploy.json')).read_text())
assert launch['elfSha256'] == sha(root / 'game/bin/vehicle-playground.elf')
log = folder / ('emulator.log' if a.platform == 'emulator' else 'console.log')
text = log.read_text(errors='replace')

def parse(text):
    assert len(re.findall(r'WINDOWBOOT version=1 mode=480p copy=OFF', text)) == 1
    own = re.findall(r'WINDOWOWN m0=(\w+) m1=(\w+) mask=(\w+) pending=(\w+) m2=(\w+) m3=(\w+)', text)
    assert len(own) == 1
    m0, m1, mask, pending, _, _ = [int(v, 16) for v in own[0]]
    assert m0 == m1 == 0 and not ((mask | pending) & ((1 << 9) | (1 << 10)))
    done = re.findall(r'WINDOWDONE reserved=1 starts=(\d+) ends=(\d+) errors=(\d+) crossings=(\d+) activeN=(\d+) activeLo=(\d+) activeHi=(\d+) blankN=(\d+) blankLo=(\d+) blankHi=(\d+) restored=(\d+) copy=OFF', text)
    assert len(done) == 1
    starts, ends, errors, crossings, an, alo, ahi, bn, blo, bhi, restored = map(int, done[0])
    assert starts == ends == 120 and errors == 0 and restored == 1
    assert an > 1000 and bn > 1000 and 0 < blo <= bhi < alo <= ahi < 65536
    assert ahi - alo <= 4 and bhi - blo <= 4
    edges = re.findall(r'EDGE kind=(\d+) index=(\d+) tick=(\d+) a=(\d+) b=(\d+) ma=(\w+) mb=(\w+)', text)
    assert len(edges) == 240
    periods = []
    for kind in (0, 1):
        rows = [[int(v, 16 if i >= 4 else 10) for i, v in enumerate(e[1:])]
                for e in edges if int(e[0]) == kind]
        assert [r[0] for r in rows] == list(range(120))
        assert all(r[4:] == [0x9e, 0xae] for r in rows)
        for _, _, age_a, age_b, _, _ in rows[3:]:
            near, far = (age_a, age_b) if kind == 0 else (age_b, age_a)
            assert near < 16 and far > near
            assert abs((far - near) - (alo if kind == 0 else blo)) <= 4
        periods.extend(((rows[i][1] - rows[i - 1][1]) & 0xffffffff) / 294912
                       for i in range(4, 120))
    assert all(16 < v < 18 for v in periods)
    return dict(activeTicks=[alo, ahi], blankTicks=[blo, bhi],
                blankMs=[blo / 576, bhi / 576], periodMedianMs=statistics.median(periods),
                edges=240, pollingStartCrossingsRejected=crossings,
                restored=True, timerPolarityObserved=True)

result = parse(text)
controls = {}
for name, bad in {
    'missing_done': text.replace('WINDOWDONE', 'REMOVED'),
    'mode_change': text.replace('ma=9e', 'ma=9f', 1),
    'missing_edge': re.sub(r'EDGE kind=0 index=3 .*', '', text, count=1),
    'wrong_epoch_counts': text.replace('starts=120', 'starts=119'),
    'timer_owned': text.replace('m0=00000000', 'm0=00000080'),
    'not_restored': text.replace('restored=1', 'restored=0'),
}.items():
    try:
        parse(bad)
        controls[name] = False
    except (AssertionError, ValueError):
        controls[name] = True
assert all(controls.values())
result.update(valid=True, platform=a.platform, elfSha256=launch['elfSha256'],
              logSha256=sha(log), controls=controls, copyBudgetQualified=False,
              limits=['Isolated runtime only, not full-game timer ownership.',
                      'No conversion packet submitted or copy WCET measured.',
                      'Polling ranges are observations, not universal video bounds.',
                      'Emulator intervals cannot qualify physical budget or FPS.'])
output = a.output or root / (a.platform + '-window.json')
output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
