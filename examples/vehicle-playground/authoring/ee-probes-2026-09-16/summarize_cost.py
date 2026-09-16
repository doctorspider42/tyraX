"""Per-pose means of one or more frame-cost.csv arms, plus deltas vs the first.

`frame-cost.csv` is the primary instrument for these probes: 240 recorded rows
in each of four parked poses, after 120 warm-up frames, written once at frame
1440 with no host I/O during sampling.

Usage:
    python summarize_cost.py name=<resultsDir> [name=<resultsDir> ...]

The FIRST arm named is the baseline every delta is taken against. Quote the
repeatability floor from two controls on one ELF before reading any delta.

Note the buckets OVERLAP and none of them may be inverted into FPS:
`work = update + submit + finish`, and `bounds`/`prepare`/`dispatch` are
included in `submit` rather than additional to it.
"""
import csv
import statistics
import sys
from pathlib import Path

POSES = ['garage-day', 'garage-night', 'outer-day', 'outer-night']
SHOW = ['total_ms', 'update_ms', 'submit_ms', 'finish_ms', 'present_ms',
        'bounds_included_ms', 'prepare_included_ms', 'dispatch_included_ms',
        'packet_included_ms', 'dma_included_ms', 'vif_wait_included_ms',
        'flushes', 'triangles', 'reuploads']


def load(path, expect=240):
    rows = list(csv.DictReader(Path(path).open()))
    out = []
    for phase in range(4):
        rr = [r for r in rows if int(r['phase']) == phase]
        if len(rr) != expect:
            print(f'WARN {path} pose {phase}: {len(rr)} rows (expected {expect})',
                  file=sys.stderr)
        keys = [k for k in rr[0] if k not in ('frame', 'phase')]
        m = {k: statistics.mean(float(r[k]) for r in rr) for k in keys}
        m['work_ms'] = m['update_ms'] + m['submit_ms'] + m['finish_ms']
        m['_n'] = len(rr)
        out.append(m)
    return out


if __name__ == '__main__':
    arms = {}
    for spec in sys.argv[1:]:
        name, path = spec.split('=', 1)
        arms[name] = load(Path(path) / 'frame-cost.csv')
    base = list(arms)[0]
    for i, pose in enumerate(POSES):
        print(f'\n== {pose}   (baseline: {base})')
        print(f'{"metric":24}' + ''.join(f'{n:>12}' for n in arms) +
              ''.join(f'{"d " + n:>12}' for n in list(arms)[1:]))
        for c in ['work_ms'] + SHOW:
            if c not in arms[base][i]:
                continue
            line = f'{c:24}' + ''.join(f'{arms[n][i][c]:>12.3f}' for n in arms)
            line += ''.join(f'{arms[n][i][c] - arms[base][i][c]:>+12.3f}'
                            for n in list(arms)[1:])
            print(line)
