"""Price qualified A35 steady moving frontend only; never mutate preflight evidence."""
import argparse
import copy
import hashlib
import importlib.util
import json
import math
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path


def contrasts(report):
    assert report['valid'] and report['platform'] == 'physical'
    assert report['worldFrontStage'] == 'early-Core-preflight-only'
    assert not report['performanceCandidate'] and 'contrasts' not in report
    assert report['originalHeads'] and report['commonWorldHot']
    assert report['groupRasterEqual'] and report['worldSourceDeliveryEqual']
    assert report['residualInputEqual'] and len(report['targetAdmission']) == 6
    assert len(report['actualWholeHeadOracle']) == 30
    assert len(report['negativeControls']) >= 52
    phases = report['phases']
    assert [p['phase'] for p in phases] == list(range(6))
    assert [p['mode'] for p in phases] == [0, 7, 0, 0, 7, 0]
    assert report['phaseRoles'] == ['baseline', 'candidate', 'baseline'] * 2
    assert [p['hour'] for p in phases] == [12, 12, 12, 0, 0, 0]
    for p in phases:
        assert p['pricedFrames'] > 0 and len(p['spatialMeanWorkMs']) == 8
        assert all(math.isfinite(n) and n > 0 for n in
                   [p['meanWorkMs'], p['p95WorkMs'], p['entryHz'], *p['spatialMeanWorkMs']])
    result = []
    for start in (0, 3):
        before, candidate, after = phases[start:start + 3]
        adjacent = [before['meanWorkMs'] - candidate['meanWorkMs'],
                    after['meanWorkMs'] - candidate['meanWorkMs']]
        spatial = [statistics.mean(control['spatialMeanWorkMs']) -
                   statistics.mean(candidate['spatialMeanWorkMs']) for control in (before, after)]
        row = dict(hour=candidate['hour'], savedMeanMs=statistics.mean(adjacent),
                   adjacentSavedMeanMs=adjacent, savedSpatialMs=statistics.mean(spatial),
                   adjacentSavedSpatialMs=spatial, candidateP95Ms=candidate['p95WorkMs'],
                   controlP95Ms=[before['p95WorkMs'], after['p95WorkMs']],
                   candidateEntryHz=candidate['entryHz'])
        row['firstOrderGate'] = (all(n > 0 for n in adjacent + spatial) and
                                 candidate['p95WorkMs'] <= max(row['controlP95Ms']) + .15)
        result.append(row)
    return result


def controls(report):
    contrasts(report)
    mutations = {
        'emulator-price': lambda r: r.update(platform='emulator'),
        'missing-admission': lambda r: r.update(targetAdmission=[]),
        'missing-heads': lambda r: r.update(actualWholeHeadOracle=[]),
        'raster-failure': lambda r: r.update(groupRasterEqual=False),
        'changed-source': lambda r: r.update(worldSourceDeliveryEqual=False),
        'changed-residual': lambda r: r.update(residualInputEqual=False),
        'missing-common-body': lambda r: r.update(commonWorldHot=False),
        'wrong-role': lambda r: r['phaseRoles'].__setitem__(1, 'baseline'),
        'wrong-mode': lambda r: r['phases'][1].update(mode=0),
        'invalid-work': lambda r: r['phases'][1].update(meanWorkMs=float('nan')),
        'incomplete-phases': lambda r: r['phases'].pop(),
        'wrong-hour': lambda r: r['phases'][4].update(hour=12),
    }
    for name, mutate in mutations.items():
        changed = copy.deepcopy(report)
        mutate(changed)
        try:
            contrasts(changed)
        except AssertionError:
            continue
        raise AssertionError('Net reader accepted negative control: ' + name)
    return list(mutations)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert not args.output.exists()
    tools = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('switch', tools / 'verify-world-front-switch.py')
    switch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(switch)
    qualification = switch.qualify(args.fixture)
    with tempfile.TemporaryDirectory() as folder:
        preflight = Path(folder) / 'preflight.json'
        run = subprocess.run([sys.executable, str(tools / 'tyrax2-world-front-preflight.py'),
                              str(args.fixture), '--physical', '--output', str(preflight)],
                             capture_output=True, text=True)
        if run.returncode:
            sys.stderr.write(run.stdout + run.stderr)
        run.check_returncode()
        report = json.loads(preflight.read_text())
    assert qualification['elfSha256'] == report['elfSha256']
    result = dict(valid=True, elfSha256=report['elfSha256'], logSha256=report['logSha256'],
                  qualification=qualification, preflight=report,
                  readerNegativeControls=controls(report), contrasts=contrasts(report),
                  bothOrdersQualified=False, promotionAllowed=False,
                  scope='net steady driving elapsed, including candidate producer rebuilds; not pure EE cycles',
                  limitations=['initial scene load/warm-up excluded', 'strip-only coverage',
                               'common A31 body and A30 shader in both arms', 'reverse order not qualified'])
    result['firstOrderGate'] = all(row['firstOrderGate'] for row in result['contrasts'])
    result['readerSha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'preflight'}, indent=2))
