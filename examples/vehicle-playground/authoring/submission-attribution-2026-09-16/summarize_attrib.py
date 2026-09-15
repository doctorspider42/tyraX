"""Summarize one or more frame-attrib.csv / frame-cost.csv arms, per pose."""
import csv
import math
import statistics
import sys
from pathlib import Path

POSES = ['garage-day', 'garage-night', 'outer-day', 'outer-night']


def load(path, expect=960):
    rows = list(csv.DictReader(Path(path).open()))
    if len(rows) != expect:
        print(f'WARN {path}: {len(rows)} rows (expected {expect})', file=sys.stderr)
    out = []
    for phase in range(4):
        rr = [r for r in rows if int(r['phase']) == phase]
        if not rr:
            out.append({})
            continue
        keys = [k for k in rr[0] if k not in ('frame', 'phase')]
        for r in rr:
            for k in keys:
                v = float(r[k])
                assert math.isfinite(v) and v >= 0, f'bad {k}={v} in {path}'
        out.append({k: statistics.mean(float(r[k]) for r in rr) for k in keys})
        out[-1]['_n'] = len(rr)
    return out


if __name__ == '__main__':
    arms = {}
    for spec in sys.argv[1:]:
        name, path = spec.split('=', 1)
        d = Path(path)
        a = load(d / 'frame-attrib.csv')
        c = load(d / 'frame-cost.csv')
        for i in range(4):
            if c[i]:
                a[i].update({k: v for k, v in c[i].items() if k != '_n'})
        arms[name] = a
    order = ['submit_ms', 'update_ms', 'finish_ms',
             'dmHud_ms', 'dmPost_ms',
             'rsTotal_ms', 'rsHead_ms', 'rsEnvProbe_ms', 'rsPortal_ms',
             'rsSky_ms', 'rsTerrain_ms', 'rsTerrainDraw_ms', 'rsBatches_ms',
             'rsProc_ms', 'rsObjects_ms',
             'rsObjSubmit_ms', 'rsWheels_ms', 'rsWheelSubmit_ms', 'rsAnim_ms',
             'rsDecorate_ms',
             'rsLightFx_ms', 'rsHighlight_ms', 'rsParticles_ms',
             'spRender_ms', 'spHead_ms', 'spTail_ms', 'spCalls', 'spCulled',
             'spPkgr_ms', 'spTex_ms', 'spProg_ms', 'spLight_ms', 'spBlss_ms',
             'spObjData_ms', 'spGifWait_ms',
             'bounds_included_ms', 'prepare_included_ms',
             'dispatch_included_ms', 'packet_included_ms', 'dma_included_ms',
             'vif_wait_included_ms', 'flushes', 'triangles', 'reuploads', '_n']
    for i, pose in enumerate(POSES):
        print(f'\n== {pose}')
        print(f'{"metric":24}' + ''.join(f'{n:>13}' for n in arms))
        for c in order:
            if not any(c in arms[n][i] for n in arms):
                continue
            print(f'{c:24}' + ''.join(
                (f'{arms[n][i][c]:>13.3f}' if c in arms[n][i] else f'{"-":>13}')
                for n in arms))
        # derived
        for n in arms:
            pass
        d = {}
        for n in arms:
            m = arms[n][i]
            if 'rsTotal_ms' not in m:
                continue
            phases = sum(m.get(k, 0) for k in (
                'rsHead_ms', 'rsPortal_ms', 'rsSky_ms', 'rsTerrain_ms',
                'rsBatches_ms', 'rsProc_ms', 'rsObjects_ms', 'rsWheels_ms',
                'rsAnim_ms', 'rsDecorate_ms', 'rsLightFx_ms', 'rsHighlight_ms',
                'rsParticles_ms'))
            d[n] = {
                'scene_block': m['submit_ms'] - m['dmPost_ms'] - m['dmHud_ms'],
                'beginFrame+blss': m['submit_ms'] - m['dmPost_ms'] - m['dmHud_ms'] - m['rsTotal_ms'],
                'rs_phase_sum': phases,
                'rs_residual': m['rsTotal_ms'] - phases,
                'objLoop_tests': m['rsObjects_ms'] - m['rsObjSubmit_ms'],
                'wheel_rebake': m.get('rsWheels_ms', 0) - m.get('rsWheelSubmit_ms', 0),
                'terrain_stream': m.get('rsTerrain_ms', 0) - m.get('rsTerrainDraw_ms', 0),
                'head_minus_probe': m.get('rsHead_ms', 0) - m.get('rsEnvProbe_ms', 0),
                'game_ee_in_rs': m['rsTotal_ms'] - m.get('spRender_ms', 0),
                'sp_brackets': m.get('bounds_included_ms', 0) + m.get('prepare_included_ms', 0) + m.get('dispatch_included_ms', 0),
                'sp_uncovered': m.get('spRender_ms', 0) - (m.get('bounds_included_ms', 0) + m.get('prepare_included_ms', 0) + m.get('dispatch_included_ms', 0)),
                'prep_sum': sum(m.get(k, 0) for k in ('spPkgr_ms', 'spTex_ms', 'spProg_ms', 'spLight_ms', 'spBlss_ms', 'spObjData_ms')),
                'dispatch_resid': m.get('dispatch_included_ms', 0) - sum(m.get(k, 0) for k in ('packet_included_ms', 'dma_included_ms', 'vif_wait_included_ms', 'spGifWait_ms')),
                'submit_minus_sp3': m['submit_ms'] - (m.get('bounds_included_ms', 0) + m.get('prepare_included_ms', 0) + m.get('dispatch_included_ms', 0)),
            }
        if d:
            print('  -- derived --')
            for k in next(iter(d.values())):
                print(f'{k:24}' + ''.join(
                    (f'{d[n][k]:>13.3f}' if n in d else f'{"-":>13}') for n in arms))
