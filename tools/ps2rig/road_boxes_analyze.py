"""Validate the private road100 experiment; distinguish admission from timing."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import math
import statistics


def analyze(root, output, admission=None):
    meta = json.loads((root/'metadata.json').read_text(encoding='utf8'))
    assert meta.get('road_boxes_comparison') and meta.get('normal_clock')
    errors, metrics, hashes = [], [], {}
    shadow = meta['shadow_enabled']
    log = (root/'hardware.log').read_text(encoding='utf8', errors='replace')

    def check(condition, message):
        if not condition:
            errors.append(message)

    def rows(name):
        p = root/name
        hashes[name] = hashlib.sha256(p.read_bytes()).hexdigest()
        with p.open(encoding='utf8', newline='') as f:
            return list(csv.DictReader(f))

    check(hashlib.sha256((root/'vehicle-playground.elf').read_bytes()).hexdigest() == meta['elf_sha256'], 'ELF mismatch')
    check(meta['expected_phases'] == [0, 1, 2, 3], 'wrong phase configuration')
    check('CB DONE phases=4' in log and 'FIXEDSTEP' not in log, 'incomplete or fixed-step run')
    check('VEHCONTACTMODE isolate=0 multiplier=1' in log, 'wrong scenario')
    check(f'VUSAT mode={"verify" if shadow else "timing"}' in log, 'wrong verification mode')
    for bad in ['CB ERROR', 'VUSATDIFF ', 'WORLDGRIDDIFF ', 'Assertion failed', 'EE exception', 'Failed to load']:
        check(bad not in log, 'log diagnostic: '+bad)
    check('VUSATCHECK cases=2048 failures=0 ' in log, 'missing SAT startup')
    check('VUSATPRECISION cases=50000 failures=0' in log, 'missing precision startup')
    for image in [0, 1]:
        check(f'VUSHARED replay image={image} accepted=1' in log, f'missing shared replay {image}')
        check(any(f'VUSHARED image={image} ' in line and 'cases=48000' in line and 'failures=0' in line for line in log.splitlines()), f'missing shared corpus {image}')
        check(f'VUSATREPLAY reference={image} saved=1 raw=1 hit=1 accepted=1' in log, f'missing late replay {image}')
    if not shadow:
        check(admission is not None, 'unshadowed run needs same-ELF admission')
        if admission:
            admitted = json.loads(admission.read_text(encoding='utf8'))
            check(admitted['qualified'] and admitted['metadata']['shadow_enabled']
                  and admitted['metadata']['elf_sha256'] == meta['elf_sha256']
                  and admitted['metadata']['road_layout'] == meta['road_layout'], 'admission mismatch')
    for phase, arm in enumerate([0, 1, 1, 0]):
        data = rows(f'collision-phase-{phase:02}.csv')
        vehicle = rows(f'vehicle-contacts-{phase}.csv')
        patch = rows(f'box-patch-{phase}.csv')
        placement = rows(f'road-placement-{phase}.csv')
        check([int(r['frame']) for r in data] == list(range(150, 750)), f'{phase}: measured coverage')
        check([int(r['frame']) for r in vehicle] == list(range(750)), f'{phase}: shared coverage')
        check([int(r['frame']) for r in patch] == list(range(750)), f'{phase}: patch coverage')
        check([int(r['object']) for r in placement] == list(range(135, 235)), f'{phase}: placement coverage')
        check(f'ROAD100 phase={phase} placed=100 initial_prop_overlaps=0 ' in log, f'{phase}: missing placement admission')
        gap = math.inf
        for i, a in enumerate(placement):
            check(all(math.isfinite(float(a[k])) for k in ['x', 'y', 'z', 'half_x', 'half_y', 'half_z']), f'{phase}: nonfinite placement')
            check(all(float(a[k]) > 0 for k in ['half_x', 'half_y', 'half_z']), f'{phase}: invalid shape')
            for b in placement[i+1:]:
                dx = abs(float(a['x'])-float(b['x']))-float(a['half_x'])-float(b['half_x'])
                dz = abs(float(a['z'])-float(b['z']))-float(a['half_z'])-float(b['half_z'])
                gap = min(gap, max(dx, dz))
        check(gap > 0, f'{phase}: initial overlapping props')
        for r in data:
            check(int(r['added']) == 100 and int(r['baseline']) == 1-arm, f'{phase}: wrong population/arm')
            check(r['generated_contacts'] == r['contacts'] and int(r['dropped_contacts']) == int(r['push_overflow']) == 0, f'{phase}: omitted contacts')
            check(int(r['pushes']) == int(r['push_candidates']) == int(r['push_tested']) == 0, f'{phase}: legacy bumper ran')
            check(all(math.isfinite(float(r[k])) for k in ['car_z', 'car_speed', 'velocity_energy', 'max_depth']), f'{phase}: nonfinite physics')
        for r in vehicle:
            check(int(r['enabled']) == int(r['full_steps']) == 1, f'{phase}: shared step invariant')
            check(1 <= int(r['vehicle_steps']) <= 4 and int(r['cars']) == 3, f'{phase}: model steps/car coverage')
            check(int(r['errors']) == int(r['missing']) == 0, f'{phase}: vehicle diagnostics')
            check(all(math.isfinite(float(r[k])) for k in ['max_depth', 'max_momentum_ratio', 'max_shape_ratio', 'car_correction']), f'{phase}: nonfinite shared state')
            if shadow:
                check(int(r['reference_pairs']) > 0 and int(r['shape_checks']) == 24, f'{phase}: missing reference checks')
                check(float(r['max_momentum_ratio']) <= 1 and float(r['max_shape_ratio']) <= 1, f'{phase}: numerical budget')
            else:
                check(int(r['reference_pairs']) == int(r['shape_checks']) == 0, f'{phase}: shadow work in timing')
        for r in patch:
            check(int(r['active']) == arm, f'{phase}: wrong patch arm')
            check(int(r['tested']) == int(r['patches'])+int(r['fallback']), f'{phase}: patch accounting')
        check(sum(int(r['contacts']) for r in vehicle) > 0 and sum(float(r['car_correction']) for r in vehicle) > 0, f'{phase}: no car/prop response')
        if arm:
            check(sum(int(r['patches']) for r in patch) > 0, f'{phase}: no face-patch coverage')
        micro, classes = rows(f'vu0-phase-{phase:02}.csv'), rows(f'vu0-class-{phase}.csv')
        check(len(micro) == len(classes) == 1, f'{phase}: micro accounting coverage')
        if micro and classes:
            check(int(micro[0]['validation_errors']) == int(micro[0]['missed_reference_hits']) == 0, f'{phase}: micro errors')
            check(sum(int(classes[0][k]) for k in ['proved_negative', 'confirmed_negative', 'confirmed_ccd', 'confirmed_tie', 'confirmed_parallel', 'confirmed_positive']) == int(micro[0]['jobs']) == int(classes[0]['jobs']), f'{phase}: incomplete micro results')
        states = {}
        for state in ['park', 'drive', 'settle']:
            window = [r for r in data if r['state'] == state]
            frames = {int(r['frame']) for r in window}
            shared = [r for r in vehicle if int(r['frame']) in frames]
            periods = sorted(int(r['period_ticks'])/294912 for r in window)
            states[state] = dict(frames=len(window), fps=1000/statistics.mean(periods),
                                 p50_period_ms=statistics.median(periods),
                                 p95_period_ms=periods[math.ceil(.95*len(periods))-1],
                                 long_periods_over_25ms=sum(p > 25 for p in periods),
                                 mean_frame_work_ms=statistics.mean(int(r['work_ticks'])/294912 for r in window),
                                 mean_shared_vehicle_physics_ms=statistics.mean(int(r['combined_ticks'])/294912 for r in shared),
                                 vehicle_model_substeps={str(n): sum(int(r['vehicle_steps']) == n for r in shared) for n in range(1, 5)},
                                 mean_physics_ms=statistics.mean(int(r['physics_ticks'])/294912 for r in window),
                                 mean_solver_ms=statistics.mean(int(r['solver_ticks'])/294912 for r in window),
                                 awake_max=max(int(r['awake']) for r in window),
                                 contacts_max=max(int(r['contacts']) for r in window),
                                 max_contact_depth=max(float(r['max_depth']) for r in window))
        travel = max(float(r['car_z']) for r in data)-min(float(r['car_z']) for r in data)
        check(travel > 2, f'{phase}: car did not drive')
        metrics.append(dict(phase=phase, arm='face-patch' if arm else 'corner-manifold',
                            min_initial_horizontal_gap=gap, car_travel_z=travel,
                            shared_contacts=sum(int(r['contacts']) for r in vehicle),
                            wakes=sum(int(r['woken']) for r in vehicle),
                            clipped_pairs=sum(int(r['patches']) for r in patch), states=states))
    result = dict(metadata=meta, qualified=not errors,
                  normal_clock_fps_qualified=not shadow and not errors,
                  production_integration_qualified=False, equal_workload_speedup_qualified=False,
                  errors=sorted(set(errors)), metrics=metrics, raw_sha256=hashes,
                  log_sha256=hashlib.sha256((root/'hardware.log').read_bytes()).hexdigest(),
                  scope='Private3-car/100-separated-prop scenario, both arms use the common shared solver. Face manifolds change trajectories. Admission timings include exhaustive shadow checks. Unshadowed FPS still includes120-frame renderer diagnostic output and stage instrumentation; it is not quiet-production FPS. Contact depth is pre-solve, not a final overlap metric. No stack/sleep,13-car traffic or production qualification.')
    output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf8')
    print(json.dumps({k: result[k] for k in ['qualified', 'normal_clock_fps_qualified', 'errors']}))
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--admission', type=Path)
    a = p.parse_args()
    report = analyze(a.root, a.output, a.admission)
    raise SystemExit(0 if report['qualified'] else 1)
