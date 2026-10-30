"""Validate and summarize the normal-clock traffic scaling console probe."""
import argparse
import csv
import hashlib
import json
import math
import re
from pathlib import Path
import statistics

HZ = 294912000
POPULATIONS = [1, 3, 5, 10, 13, 13, 10, 5, 3, 1]


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered)-1, math.ceil(len(ordered)*fraction)-1)]


def analyze(root):
    errors, phases = [], []
    log_path = root/'console.log'
    log = log_path.read_text(encoding='utf8', errors='replace') if log_path.exists() else ''
    config = re.search(r'TRAFFICBENCH CONFIG detail=(\d+) normal_clock=1'
                       r'(?: start_phase=(\d+))?(?: drop_cache=(\d+))?', log)
    starts = re.findall(r'TRAFFICBENCH START phase=(\d+) hq=(\d+) civ=(\d+) initial_box_overlaps=(\d+)', log)
    expected = [(str(i), str(min(n, 5)), str(max(n-5, 0)), '0') for i, n in enumerate(POPULATIONS)]
    if starts != expected:
        errors.append('missing/invalid population startup or initial box overlap checks')
    if 'TRAFFICBENCH DONE phases=10' not in log or 'TRAFFICBENCH ERROR' in log:
        errors.append('missing completion or explicit benchmark error')
    if 'TRAFFICBENCH CONFIG detail=0 normal_clock=1' not in log:
        errors.append('lightweight normal-clock configuration unconfirmed')
    for phase, total in enumerate(POPULATIONS):
        path = root/f'traffic-bench-{phase:02d}.csv'
        if not path.exists():
            errors.append(f'phase {phase}: missing CSV')
            continue
        with path.open(encoding='utf8', newline='') as f:
            rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]
        measured = [r for r in rows if 2 <= r['elapsed'] < 5]
        failures = set()
        if len(measured) < 30:
            errors.append(f'phase {phase}: insufficient driving frames')
            continue
        for r in rows:
            if not all(math.isfinite(v) for v in r.values()):
                failures.add('nonfinite CSV value')
            if r['phase'] != phase or r['hq'] != min(total, 5) or r['civ'] != max(total-5, 0):
                failures.add('population/phase mismatch')
            if r['errors'] or r['kinematic_calls']:
                failures.add('numeric error or kinematic bypass')
            if r['full_physics_steps'] != 1:
                failures.add('full physics step count differs from one')
            if not 1 <= r['model_substeps'] <= 4:
                failures.add('vehicle substep count outside 1..4')
            if r['full_model_calls'] != total*r['model_substeps']:
                failures.add('not every active car ran the complete model')
            if r['period_ticks'] <= 0 or r['body_ticks'] > r['period_ticks']:
                failures.add('invalid period/body clock')
        if any(r['hq_lod0'] != min(total, 5) or r['civ_lod0'] != max(total-5, 0) for r in measured):
            failures.add('near mesh coverage lost during driving')
        for failure in sorted(failures):
            errors.append(f'phase {phase}: {failure}')
        ms = lambda key: [r[key]/HZ*1000 for r in measured]
        periods = ms('period_ticks')
        phases.append(dict(
            phase=phase, hq=min(total, 5), civilians=max(total-5, 0),
            csv_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            frames=len(measured), average_fps=1000/statistics.mean(periods),
            median_period_ms=statistics.median(periods), p95_period_ms=percentile(periods, .95),
            max_period_ms=max(periods), over_20_ms=sum(t > 20 for t in periods),
            driver_travel_z=measured[-1]['car_z']-measured[0]['car_z'],
            mean_ms={key: statistics.mean(ms(key)) for key in measured[0]
                     if key.endswith('_ticks')},
            max_non_driver_paint_grants=max(r['non_driver_paint_grants'] for r in measured),
            min_hq_front=min(r['hq_front'] for r in measured),
            min_civilian_front=min(r['civ_front'] for r in measured),
            min_hq_submission_attempts=min(r['hq_submitted'] for r in measured),
            min_civilian_submission_attempts=min(r['civ_submitted'] for r in measured),
            max_camera_distance=max(r['max_camera_distance'] for r in measured)))
    return dict(probe_gates_passed=not errors, errors=errors, phases=phases,
                initial_phase=int(config[2]) if config and config[2] is not None else None,
                reload_cache_diagnostic=bool(int(config[3])) if config and config[3] is not None else None,
                console_log_sha256=hashlib.sha256(log_path.read_bytes()).hexdigest() if log_path.exists() else None,
                target_60fps_qualified=False,
                scope='Short straight-road normal-clock scaling diagnosis. '
                'Submission attempts are not GS visibility. Nested CPU counters must not be added. '
                'No turning/impact, visual-quality or production-transparency qualification.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = analyze(args.root)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf8')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['probe_gates_passed'] else 1)
