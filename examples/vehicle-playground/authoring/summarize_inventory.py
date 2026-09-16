"""Read a fixture's bin/frame-inventory.csv and print the per-producer table.

    python summarize_inventory.py <fixture> [--phase 0] [--objects 20]

`<fixture>` is the instrumented benchmark project (inventory-frame.py wrote the
CSV into its bin/). Model names come from that fixture's own
inc/scene_data.hpp, so the object rows group by what they actually are.

Every column is PER FRAME of the 240 recorded in that pose, except `per_hit`,
which divides by the number of frames the producer actually ran - the shared
reflection probe runs on every SECOND frame, so its per-frame and per-hit
triangle counts differ by 2x and both are worth reading.
"""
import argparse
import csv
import re
from pathlib import Path

PHASES = ['garage day', 'garage night', 'outer day', 'outer night']
# SceneObjectData::type, from the generated scene_data.hpp comment block.
TYPES = {0: 'box', 1: 'sphere', 2: 'cylinder', 3: 'cone', 4: 'spawn',
         5: 'model', 6: 'player', 7: 'emitter', 8: 'sound', 9: 'light',
         10: 'save', 11: 'empty', 12: 'plane', 13: 'decal', 14: 'camera',
         15: 'mirror', 16: 'portal', 17: 'area', 18: 'scatter',
         19: 'scroller', 20: 'comment'}

p = argparse.ArgumentParser(description=__doc__,
                            formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('fixture', type=Path)
p.add_argument('--phase', type=int, default=0, choices=[0, 1, 2, 3])
p.add_argument('--objects', type=int, default=20,
               help='how many object rows to list (0 = none)')
a = p.parse_args()

rows = list(csv.DictReader((a.fixture / 'bin/frame-inventory.csv').open()))
if not rows:
    raise SystemExit('Empty inventory; reject this run')

models = []
sd = a.fixture / 'inc/model_data.gen.hpp'
if sd.exists():
    text = sd.read_text(encoding='utf-8', errors='replace')
    m = re.search(r'\bMODEL_PATHS\[[^\]]*\]\s*=\s*\{(.*?)\};', text, re.S)
    if m:
        models = re.findall(r'"([^"]*)"', m.group(1))


def num(r, k):
    return int(r[k])


sel = [r for r in rows if int(r['phase']) == a.phase]
frames = max((num(r, 'frames') for r in sel), default=0)
if frames == 0:
    raise SystemExit('No recorded frames in that phase; reject this run')

prod = [r for r in sel if r['kind'] == 'producer']
tot = {k: sum(num(r, k) for r in prod)
       for k in ('triangles', 'packages', 'packages_outside', 'vertices',
                 'flushes', 'bags')}

print(f'{a.fixture}  pose {a.phase} ({PHASES[a.phase]}), {frames} frames\n')
print(f'{"producer":<17}{"tri/frame":>10}{"%":>7}{"pkg":>8}{"bags":>7}'
      f'{"flush":>7}{"verts":>8}{"pkgOut":>8}{"hits":>6}{"tri/hit":>9}')
print('-' * 87)
for r in sorted(prod, key=lambda r: -num(r, 'triangles')):
    tri = num(r, 'triangles')
    if not tri and not num(r, 'bags') and not num(r, 'packages_outside'):
        continue
    hits = num(r, 'hits')
    share = 100.0 * tri / tot['triangles'] if tot['triangles'] else 0.0
    print(f'{r["producer"]:<17}{tri / frames:>10.1f}{share:>6.1f}%'
          f'{num(r, "packages") / frames:>8.1f}{num(r, "bags") / frames:>7.1f}'
          f'{num(r, "flushes") / frames:>7.1f}'
          f'{num(r, "vertices") / frames:>8.0f}'
          f'{num(r, "packages_outside") / frames:>8.1f}'
          f'{hits / frames:>6.2f}{(tri / hits if hits else 0):>9.1f}')
print('-' * 87)
print(f'{"TOTAL":<17}{tot["triangles"] / frames:>10.1f}{100.0:>6.1f}%'
      f'{tot["packages"] / frames:>8.1f}{tot["bags"] / frames:>7.1f}'
      f'{tot["flushes"] / frames:>7.1f}{tot["vertices"] / frames:>8.0f}'
      f'{tot["packages_outside"] / frames:>8.1f}')

def model_name(r):
    mi = int(r['model'])
    if mi < 0:
        return '-'
    return models[mi] if mi < len(models) else f'#{mi}'


def object_tables(kind, title):
    objs = [r for r in sel if r['kind'] == kind]
    if not objs:
        return
    print(f'\n{title} - top {a.objects} by triangles ({len(objs)} drew at all)\n')
    print(f'{"idx":>5}  {"type":<9}{"model":<44}{"tri/frame":>10}{"pkg":>7}'
          f'{"bags":>6}{"hits":>6}')
    print('-' * 87)
    for r in sorted(objs, key=lambda r: -num(r, 'triangles'))[:a.objects]:
        print(f'{r["object"]:>5}  {TYPES.get(int(r["type"]), r["type"]):<9}'
              f'{model_name(r)[-43:]:<44}{num(r, "triangles") / frames:>10.1f}'
              f'{num(r, "packages") / frames:>7.1f}'
              f'{num(r, "bags") / frames:>6.1f}'
              f'{num(r, "hits") / frames:>6.2f}')
    print('\nGrouped by model')
    groups = {}
    for r in objs:
        key = (TYPES.get(int(r['type']), r['type']), model_name(r))
        g = groups.setdefault(key, [0, 0, 0, 0])
        g[0] += 1
        g[1] += num(r, 'triangles')
        g[2] += num(r, 'packages')
        g[3] += num(r, 'bags')
    print(f'{"type":<9}{"model":<44}{"n":>4}{"tri/frame":>11}{"pkg":>7}'
          f'{"bags":>6}')
    print('-' * 87)
    for (t, name), g in sorted(groups.items(), key=lambda kv: -kv[1][1]):
        print(f'{t:<9}{name[-43:]:<44}{g[0]:>4}{g[1] / frames:>11.1f}'
              f'{g[2] / frames:>7.1f}{g[3] / frames:>6.1f}')


if a.objects:
    object_tables('object', 'Solo objects in the main view (object_submit)')
    object_tables('probe_object',
                  'Objects drawn INTO the reflection probe (env_probe_objs)')
