"""Compare two arms' frame inventories in PACKAGES first, triangles second.

    python compare_packages.py ctl=<dir> cand=<dir> [--us-day 19.5 --us-night 31.8]

WHY PACKAGES AND NOT TRIANGLES. The console measured one VU1 package at about
**19.5 us in garage day and 31.8 us at night** end to end - eight times the
2.362 us that packet construction alone had priced it at, so the whole
`dispatch` bracket follows the package count. In the round that measured it,
triangles ROSE by 1 807 while the frame got 0.449 ms FASTER: the triangle
column can point the wrong way entirely. See
docs/ee-submission-rearchitecture.md, "What one VU1 package costs".

So this script leads with packages, prints triangles beside them as a
secondary column, and **labels every millisecond it prints as a CONVERSION**
rather than a measurement. Nothing here has been on hardware.

The per-pose split matters as much as the total: an impostor threshold is a
distance rule, so which objects it acts on changes completely between the
garage poses (buildings at 117 and 118 units) and the outer-road poses (the
same buildings at 32 and 44). A total would hide that.
"""
import argparse
import csv
from pathlib import Path

POSES = {'0': 'garage day', '1': 'garage night', '2': 'outer day', '3': 'outer night'}


def read(path):
    """-> {(kind, phase, key): {...}} averaged per frame."""
    out = {}
    for r in csv.DictReader(open(path)):
        f = max(1, int(r['frames']))
        key = (r['kind'], r['phase'],
               r['producer'] if r['kind'] == 'producer' else r['object'])
        out[key] = {'tri': int(r['triangles']) / f,
                    'pkg': int(r['packages']) / f,
                    'bags': int(r['bags']) / f,
                    'out': int(r['packages_outside']) / f,
                    'flush': int(r['flushes']) / f}
    return out


ap = argparse.ArgumentParser(description=__doc__,
                             formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('arms', nargs=2, help='ctl=<dir> cand=<dir>')
ap.add_argument('--us-day', type=float, default=19.5)
ap.add_argument('--us-night', type=float, default=31.8)
a = ap.parse_args()
names, dirs = zip(*[s.split('=', 1) for s in a.arms])
ctl, cand = (read(Path(d) / 'frame-inventory.csv') for d in dirs)

print('FRAME TOTAL, per pose. Packages lead; triangles are secondary.')
print(f'{"pose":<14}{"pkg ctl":>9}{"pkg cand":>10}{"dpkg":>8}{"%":>8}'
      f'{"tri ctl":>10}{"tri cand":>10}{"dtri":>9}{"bags d":>8}')
print('-' * 86)
totals = {}
for ph, label in POSES.items():
    c = [v for (k, p, _), v in ctl.items() if k == 'producer' and p == ph]
    d = [v for (k, p, _), v in cand.items() if k == 'producer' and p == ph]
    if not c or not d:
        continue
    cp, dp = sum(x['pkg'] for x in c), sum(x['pkg'] for x in d)
    ct, dt = sum(x['tri'] for x in c), sum(x['tri'] for x in d)
    cb, db = sum(x['bags'] for x in c), sum(x['bags'] for x in d)
    totals[ph] = (cp, dp, ct, dt)
    pct = (100.0 * (dp - cp) / cp) if cp else 0.0
    print(f'{label:<14}{cp:>9.1f}{dp:>10.1f}{dp-cp:>8.1f}{pct:>7.1f}%'
          f'{ct:>10.0f}{dt:>10.0f}{dt-ct:>9.0f}{db-cb:>8.1f}')

print('\nCONVERTED to milliseconds. THIS IS A CONVERSION, NOT A MEASUREMENT.')
print(f'  rate: {a.us_day} us/package (day), {a.us_night} us/package (night), '
      'measured on the console by the wheel-strip round on a change that\n'
      '  removed packages AND vertices together - which this one also does.')
for ph, label in POSES.items():
    if ph not in totals:
        continue
    cp, dp, _, _ = totals[ph]
    us = a.us_night if 'night' in label else a.us_day
    print(f'  {label:<14}{dp-cp:>8.1f} packages  ->  {(dp-cp)*us/1000.0:>7.3f} ms (converted)')

print('\nBY PRODUCER, garage day (phase 0) - where the change acts')
print(f'{"producer":<18}{"pkg ctl":>9}{"pkg cand":>10}{"dpkg":>8}'
      f'{"tri ctl":>10}{"tri cand":>10}{"dtri":>9}')
print('-' * 74)
prods = sorted({k[2] for k in ctl if k[0] == 'producer' and k[1] == '0'})
for p in prods:
    c = ctl.get(('producer', '0', p))
    d = cand.get(('producer', '0', p))
    if not c or not d or (c['pkg'] == d['pkg'] and c['tri'] == d['tri']):
        continue
    print(f'{p:<18}{c["pkg"]:>9.1f}{d["pkg"]:>10.1f}{d["pkg"]-c["pkg"]:>8.1f}'
          f'{c["tri"]:>10.0f}{d["tri"]:>10.0f}{d["tri"]-c["tri"]:>9.0f}')

print('\nBY OBJECT, garage day - every object whose packages moved')
print(f'{"idx":>5}{"pkg ctl":>9}{"pkg cand":>10}{"dpkg":>8}'
      f'{"tri ctl":>10}{"tri cand":>10}{"dtri":>9}')
print('-' * 61)
objs = sorted({k[2] for k in list(ctl) + list(cand) if k[0] == 'object' and k[1] == '0'},
              key=lambda s: int(s))
z = {'tri': 0.0, 'pkg': 0.0, 'bags': 0.0, 'out': 0.0, 'flush': 0.0}
for o in objs:
    c = ctl.get(('object', '0', o), z)
    d = cand.get(('object', '0', o), z)
    if abs(c['pkg'] - d['pkg']) < 1e-9 and abs(c['tri'] - d['tri']) < 1e-9:
        continue
    print(f'{o:>5}{c["pkg"]:>9.1f}{d["pkg"]:>10.1f}{d["pkg"]-c["pkg"]:>8.1f}'
          f'{c["tri"]:>10.0f}{d["tri"]:>10.0f}{d["tri"]-c["tri"]:>9.0f}')
