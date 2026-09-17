"""Join what each object COSTS with what it is SEEN to contribute.

    python analyze_visibility.py --captures <dir>
                                 [--inventory <frame-inventory.csv>]
                                 [--scene <project.tyra>] [--phase 0]
                                 [--csv visibility.csv]

`--captures` is a probe-visibility.ps1 output directory: `ctl-N.png`, one
`oIDX-N.png` per hidden object, the `pAB-N.png` occluder pairs, and `ctl2-N.png`
re-taken at the end of the run. `--inventory` is the counts arm's per-object
rows, which supply the triangle/package/bag columns.

THE ORDER IS THE POINT AND IT IS NOT NEGOTIABLE. Every claim this round makes is
a claim about a ZERO, and a zero is exactly what a broken instrument also
produces. So three things are established before a single verdict is printed:

  1. WITHIN each probe - repeat 1 against every later repeat. A probe whose own
     repeats differ cannot support a between-probe number.
  2. THE CONTROL AGAINST ITSELF - `ctl` against `ctl2`, taken at opposite ends
     of the same boot. A run whose control drifts is a dead run, not a caveat.
  3. THE POSITIVE CONTROL - each `pAB` pair against the single-object probe of
     its occluder. A lone zero cannot distinguish "the object was invisible"
     from "the hide never reached that index"; this can, and it is the check
     the round would have been worthless without.

     Note that comparing a pair against the CONTROL proves nothing here: the
     pair and the occluder-alone probe change the SAME screen region, so they
     report the same count of different pixels. The comparison has to be
     pair-against-occluder.

The verdict column is the round's product:

  SEEN      pixels changed when the object was removed. Occlusion culling
            cannot touch it; the levers are LOD and impostors.
  OCCLUDED  it submitted triangles and NOT ONE PIXEL of the frame depended on
            them. Free to cull, with no quality cost to argue about.

and so is `tri/px`, triangles per visible pixel, which is what actually ranks
the population - see this round's README.

Needs PIL and numpy.
"""
import argparse
import csv
import json
import os
from pathlib import Path

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)


def diff(a, b):
    d = np.abs(a - b).max(axis=2)
    return int((d > 0).sum()), int(d.max())


ap = argparse.ArgumentParser(description=__doc__,
                             formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--captures', type=Path, required=True)
ap.add_argument('--inventory', type=Path)
ap.add_argument('--scene', type=Path)
ap.add_argument('--phase', default='0')
ap.add_argument('--csv', type=Path, help='write the joined table here')
a = ap.parse_args()

shots = {}
for f in sorted(a.captures.glob('*.png')):
    shots.setdefault(f.name.rsplit('-', 1)[0], []).append((f.name, load(f)))
if 'ctl' not in shots:
    raise SystemExit('No ctl-*.png in the capture directory')
ctl = shots['ctl'][0][1]
h, w = ctl.shape[0], ctl.shape[1]

print('1. WITHIN each probe - repeat 1 against every later repeat')
bad = 0
for name, lst in sorted(shots.items()):
    for fname, img in lst[1:]:
        px, worst = diff(lst[0][1], img)
        if px:
            bad += 1
            print(f'   NOT REPEATABLE {name}: {fname} {px} px, worst {worst}')
print(f'   all {len(shots)} probes bit-identical across repeats' if not bad else
      '   STOP - no between-probe number can be read from this run.')

print('\n2. CONTROL DRIFT - ctl at the start vs ctl2 at the end of the same boot')
if 'ctl2' in shots:
    px, worst = diff(ctl, shots['ctl2'][0][1])
    print(f'   {px} pixels, worst {worst}' +
          ('' if px == 0 else '  <- THE RUN IS DEAD; its zeros mean nothing'))
else:
    print('   no ctl2 capture - this run has no drift check')

print('\n3. POSITIVE CONTROL - each pair against the probe of its occluder')
pairs = sorted(n for n in shots if n.startswith('p') and n[1:].isdigit())
if not pairs:
    print('   none in this run - every zero below is unconfirmed')
for p in pairs:
    occ = 'o' + p[1:3]
    if occ not in shots:
        continue
    px, _ = diff(shots[occ][0][1], shots[p][0][1])
    print(f'   {occ} vs {p}: {px} px  -> the hidden object IS reachable and '
          f'paints {px} px once its occluder is gone'
          if px else
          f'   {occ} vs {p}: 0 px  -> UNCONFIRMED: the hide may never have '
          f'reached that index')

# ---- the per-object submission rows ----------------------------------------
tri, pkg, bags, frames = {}, {}, {}, 1
if a.inventory and a.inventory.exists():
    for r in csv.DictReader(open(a.inventory)):
        if r['kind'] != 'object' or r['phase'] != str(a.phase):
            continue
        f = max(1, int(r['frames']))
        frames = f
        i = int(r['object'])
        tri[i], pkg[i], bags[i] = (int(r['triangles']) / f,
                                   int(r['packages']) / f, int(r['bags']) / f)

names = {}
if a.scene and a.scene.exists():
    d = json.load(open(a.scene))
    base = a.scene.parent / 'objects'
    for i, oid in enumerate(d['scenes'][0]['objects']):
        p = base / (oid + '.json')
        if p.exists():
            o = json.load(open(p))
            m = o.get('model', '')
            names[i] = (o.get('name', ''),
                        os.path.splitext(os.path.basename(m))[0] if m
                        else o.get('type', ''))

print(f'\n4. BETWEEN - each probe against the control '
      f'({w}x{h} = {w*h} px; inventory over {frames} frames)')
print(f'{"idx":>5} {"object":<22}{"model":<22}'
      f'{"tri/f":>8}{"pkg":>6}{"bags":>6}{"vis px":>9}{"%scr":>8}{"tri/px":>9}  verdict')
print('-' * 106)
rows = []
for name in shots:
    if not (name.startswith('o') and name[1:].isdigit()):
        continue
    i = int(name[1:])
    px, _ = diff(ctl, shots[name][0][1])
    rows.append((tri.get(i, 0.0), i, px))
rows.sort(reverse=True)
T = P = B = PX = 0.0
oT = oP = oB = 0.0
out = []
for t, i, px in rows:
    nm, md = names.get(i, ('', ''))
    verdict = 'OCCLUDED' if px == 0 else ('marginal' if px < 200 else 'SEEN')
    ratio = '' if px == 0 else f'{t/px:.2f}'
    T += t; P += pkg.get(i, 0); B += bags.get(i, 0); PX += px
    if px == 0:
        oT += t; oP += pkg.get(i, 0); oB += bags.get(i, 0)
    print(f'{i:>5} {nm[:21]:<22}{md[:21]:<22}{t:>8.0f}{pkg.get(i,0):>6.0f}'
          f'{bags.get(i,0):>6.0f}{px:>9}{100.0*px/(w*h):>7.2f}%{ratio:>9}  {verdict}')
    out.append([i, nm, md, round(t), round(pkg.get(i, 0)), round(bags.get(i, 0)),
                px, round(100.0*px/(w*h), 4), ratio, verdict])
print('-' * 106)
print(f'{"":>5} {"TOTAL":<22}{"":<22}{T:>8.0f}{P:>6.0f}{B:>6.0f}{PX:>9.0f}')
if T:
    print(f'\nSTRICTLY OCCLUDED (0 visible pixels): {oT:.0f} triangles, '
          f'{oP:.0f} packages, {oB:.0f} bags')
    print(f'  = {100.0*oT/T:.1f}% of the probed (solo-object) triangles')
    print('  Quote it against the frame total the counts arm measured, not '
          'against an older published one.')

if a.csv:
    with open(a.csv, 'w', newline='') as fh:
        wr = csv.writer(fh)
        wr.writerow(['object', 'name', 'model', 'triangles_per_frame', 'packages',
                     'bags', 'visible_pixels', 'pct_of_screen',
                     'triangles_per_visible_pixel', 'verdict'])
        wr.writerows(out)
    print('wrote', a.csv)
