"""Validate and summarize one frame-cost.csv arm (960 rows, 240 per pose)."""
import csv
import json
import math
import statistics
import sys
from pathlib import Path

POSES = ['garage-day', 'garage-night', 'outer-day', 'outer-night']


def load(path):
    rows = list(csv.DictReader(Path(path).open()))
    assert len(rows) == 960, f'{path}: {len(rows)} rows'
    assert len({r['frame'] for r in rows}) == 960, 'duplicate frames'
    out = []
    for phase in range(4):
        rr = [r for r in rows if int(r['phase']) == phase]
        assert len(rr) == 240, f'phase {phase}: {len(rr)}'
        keys = [k for k in rr[0] if k not in ('frame', 'phase')]
        for r in rr:
            for k in keys:
                v = float(r[k])
                assert math.isfinite(v) and v >= 0, f'bad {k}={v}'
        means = {k: statistics.mean(float(r[k]) for r in rr) for k in keys}
        means['work_ms'] = means['update_ms'] + means['submit_ms'] + means['finish_ms']
        out.append(means)
    return out


if __name__ == '__main__':
    arms = {}
    for spec in sys.argv[1:]:
        name, path = spec.split('=', 1)
        arms[name] = load(path)
    cols = ['work_ms', 'update_ms', 'submit_ms', 'finish_ms', 'present_ms',
            'bounds_included_ms', 'prepare_included_ms', 'dispatch_included_ms',
            'packet_included_ms', 'dma_included_ms', 'vif_wait_included_ms',
            'flushes', 'triangles', 'reuploads']
    for i, pose in enumerate(POSES):
        print(f'\n== {pose}')
        print(f'{"metric":26}' + ''.join(f'{n:>14}' for n in arms))
        for c in cols:
            print(f'{c:26}' + ''.join(f'{arms[n][i][c]:>14.3f}' for n in arms))
    Path('arm-summary.json').write_text(json.dumps(
        {n: [{k: round(v, 4) for k, v in p.items()} for p in a] for n, a in arms.items()},
        indent=2), encoding='utf-8')
