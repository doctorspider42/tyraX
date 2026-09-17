"""Summarize the `bounds` and package-creation/classification splits, per pose.

Reads the frame-attrib.csv / frame-cost.csv pair written by
instrument-frame-cost.py --attribute with TYRA_STAPIP_ATTRIB 1, and prints
THREE tables that must each close:

  bounds   = bdSize + bdXform + bdCache + bdPlanes + bdMain + residual
  dispatch = dsRetain + dsDirect + dsCreate + dsRender + dsFlush + residual
  cacher   = hits + recalcs + fresh  (against spCalls, the bags submitted)

plus the per-bag and per-package rates, which are what a millisecond figure
has to be divided by before it can be compared with anything.

Usage:  python summarize_bounds.py NAME=<dir with both CSVs> [NAME2=<dir> ...]
"""
import csv
import math
import statistics
import sys
from pathlib import Path

POSES = ['garage-day', 'garage-night', 'outer-day', 'outer-night']


def load(path, expect=960):
    rows = list(csv.DictReader(Path(path).open()))
    if len(rows) != expect:
        print(f'WARN {path}: {len(rows)} rows (expected {expect})',
              file=sys.stderr)
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


BOUNDS_PARTS = ['bdSize_ms', 'bdXform_ms', 'bdCache_ms', 'bdPlanes_ms',
                'bdMain_ms']
DISPATCH_PARTS = ['dsRetain_ms', 'dsDirect_ms', 'dsCreate_ms', 'dsRender_ms',
                  'dsFlush_ms']
NESTED = ['packet_included_ms', 'dma_included_ms', 'vif_wait_included_ms',
          'spGifWait_ms']


def derived(m):
    d = {}
    b = m.get('bounds_included_ms', 0.0)
    parts = sum(m.get(k, 0.0) for k in BOUNDS_PARTS)
    d['bounds'] = b
    d['  bounds parts sum'] = parts
    d['  bounds residual'] = b - parts
    d['    bdSize: program'] = m.get('bdProg_ms', 0.0)
    d['    bdSize: arithmetic'] = m.get('bdSizeCalc_ms', 0.0)
    d['    bdSize: pin+store'] = (m.get('bdSize_ms', 0.0)
                                 - m.get('bdProg_ms', 0.0)
                                 - m.get('bdSizeCalc_ms', 0.0))
    disp = m.get('dispatch_included_ms', 0.0)
    dparts = sum(m.get(k, 0.0) for k in DISPATCH_PARTS)
    nested = sum(m.get(k, 0.0) for k in NESTED)
    d['dispatch'] = disp
    d['  dispatch parts sum'] = dparts
    d['  dispatch residual'] = disp - dparts
    d['  nested packet/dma/wait'] = nested
    # The bucket the previous round named "package creation and
    # classification": dispatch minus the four nested buckets.
    d['  pkg create+classify'] = disp - nested
    # ...and what it is actually made of, now that the EE share of each
    # route is separable from the packet work reached from inside it.
    d['    of which classify'] = m.get('dsClassify_ms', 0.0)
    d['    of which create-rest'] = m.get('dsCreate_ms', 0.0) - m.get(
        'dsClassify_ms', 0.0)
    d['    of which direct EE'] = m.get('dsDirect_ms', 0.0)
    d['    of which render EE'] = m.get('dsRender_ms', 0.0)
    d['    of which flush EE'] = m.get('dsFlush_ms', 0.0)
    d['    of which retain'] = m.get('dsRetain_ms', 0.0)
    # cacher
    hits = m.get('bbHit', 0.0)
    rec = m.get('bbRecalc', 0.0)
    fresh = m.get('bbFresh', 0.0)
    d['cacher calls'] = hits + rec + fresh
    d['  hits'] = hits
    d['  recalcs'] = rec
    d['  fresh'] = fresh
    d['  entries held'] = m.get('bbEntries', 0.0)
    d['  probes'] = m.get('bbProbe', 0.0)
    if hits + rec:
        d['  probes per lookup'] = m.get('bbProbe', 0.0) / (hits + rec)
    d['  frame-end scan ms'] = m.get('bbFrameEnd_ms', 0.0)
    # The split that says whether bdCache is the LOOKUP or the RECOMPUTE.
    rt = m.get('bbRecalcT_ms', 0.0)
    d['  recompute ms'] = rt
    d['  pure lookup ms'] = m.get('bdCache_ms', 0.0) - rt
    if rec + fresh:
        d['  us per recompute'] = rt / (rec + fresh) * 1000.0
    if hits + rec + fresh:
        d['  us per lookup'] = (m.get('bdCache_ms', 0.0) - rt) / (
            hits + rec + fresh) * 1000.0
    # rates
    calls = m.get('spCalls', 0.0)
    pkgs = m.get('dsPackages', 0.0)
    if calls:
        d['us per bag: bounds'] = b / calls * 1000.0
        d['us per bag: bdCache'] = m.get('bdCache_ms', 0.0) / calls * 1000.0
        d['us per bag: bdMain'] = m.get('bdMain_ms', 0.0) / calls * 1000.0
        d['us per bag: dispatch'] = disp / calls * 1000.0
    if pkgs:
        d['us per pkg: classify'] = m.get('dsClassify_ms', 0.0) / pkgs * 1000.0
        d['us per pkg: create'] = m.get('dsCreate_ms', 0.0) / pkgs * 1000.0
        d['merge parts per pkg'] = m.get('dsMergeParts', 0.0) / pkgs
        d['mask calls per pkg'] = m.get('dsMaskCalls', 0.0) / pkgs
    d['bags submitted'] = calls
    d['bags culled at bbox'] = m.get('spCulled', 0.0)
    d['bags direct route'] = m.get('dsDirectBags', 0.0)
    d['bags partial route'] = m.get('dsPartialBags', 0.0)
    d['packages created'] = pkgs
    return d


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

    raw = (['submit_ms', 'bounds_included_ms', 'prepare_included_ms',
            'dispatch_included_ms', 'spRender_ms', 'spCalls', 'spCulled']
           + BOUNDS_PARTS + DISPATCH_PARTS
           + ['dsClassify_ms', 'dsDirectBags', 'dsPartialBags', 'dsPackages',
              'dsMergeParts', 'dsMaskCalls',
              'bbHit', 'bbRecalc', 'bbFresh', 'bbProbe', 'bbEntries',
              'bbFrameEnd_ms', 'bbRecalcT_ms']
           + NESTED + ['finish_ms', 'flushes', '_n'])

    for i, pose in enumerate(POSES):
        print(f'\n== {pose}')
        print(f'{"metric":28}' + ''.join(f'{n:>13}' for n in arms))
        for c in raw:
            if not any(c in arms[n][i] for n in arms):
                continue
            print(f'{c:28}' + ''.join(
                (f'{arms[n][i][c]:>13.3f}' if c in arms[n][i] else f'{"-":>13}')
                for n in arms))
        d = {n: derived(arms[n][i]) for n in arms if arms[n][i]}
        if d:
            print('  -- derived --')
            for k in next(iter(d.values())):
                print(f'{k:28}' + ''.join(
                    (f'{d[n][k]:>13.3f}' if n in d and k in d[n]
                     else f'{"-":>13}') for n in arms))
