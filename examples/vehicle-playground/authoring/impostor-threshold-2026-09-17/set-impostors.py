"""Assign the baked impostor assets to every instance of the named models, and
set the switch distance that IS the change under test.

    python set-impostors.py <project> --distance 100 [--models district-tower,district-loft]

The two arms differ in **one authored number**. Both objects get the same
`impostor`, `impostorBillboard` and `impostorViews` fields; only
`impostorDistance` changes, and `--distance 0` disables replacement ("zero
disables", docs/impostors.md) and is the CONTROL.

**What that control actually is, verified in the generated output rather than
assumed.** At distance 0 the baker drops the far models from the model table
altogether - `.res-baked` still holds the `-far.tmdl` files, but no scene
object references them and `model_data.gen.hpp` does not list them. So the
control is not "impostor assets present but idle"; it is **the frame exactly as
it ships today**, and the candidate's delta therefore includes the two extra
resident models the change brings with it rather than hiding them in a shared
baseline. That is the more honest of the two controls, but it does mean the
arms differ in the loaded model set as well as in the threshold, and a RAM
claim cannot be read from this pair.

WHERE THE NUMBER COMES FROM. It is not a guess and not a multiple of the model
size. The 2026-09-17 visibility round measured what each of these objects is
SEEN to contribute from the garage pose:

    Tower block 03   dist  48   33 385 visible px
    Tower block 04   dist  48   35 074 visible px
    Loft block 05    dist  82      610 visible px
    Tower block 06   dist 117        0 visible px   (fully occluded)
    Loft block 07    dist 118      128 visible px

The threshold is placed in the gap between the furthest object the frame is
measured to SEE (82) and the nearest one it is measured not to (117). Anything
inside that gap is defensible; 100 is its middle, and it leaves the hysteresis
band (90% of the threshold, i.e. 90..100) clear of every object in the list.

Billboard eligibility is CHECKED rather than assumed: upright (no X/Z
rotation), positive scale and equal X/Z scale, or the runtime silently falls
back to the full model and the arm would measure nothing while looking fine.
"""
import argparse
import json
from pathlib import Path


def eligible(o):
    r = o.get('rotation', [0.0, 0.0, 0.0])
    s = o.get('scale', [1.0, 1.0, 1.0])
    if abs(r[0]) > 1e-6 or abs(r[2]) > 1e-6:
        return False, f'X/Z rotation {r}'
    if min(s) <= 0.0:
        return False, f'non-positive scale {s}'
    if abs(s[0] - s[2]) > 1e-6:
        return False, f'X/Z scale mismatch {s}'
    return True, ''


ap = argparse.ArgumentParser(description=__doc__,
                             formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('project', type=Path)
ap.add_argument('--distance', type=float, required=True,
                help='world units; 0 disables replacement (the control arm)')
ap.add_argument('--models', default='district-tower,district-loft')
ap.add_argument('--views', type=int, default=8, choices=(4, 8, 16))
a = ap.parse_args()

manifest = a.project / 'vehicle-playground.tyra'
ids = json.loads(manifest.read_text(encoding='utf-8'))['scenes'][0]['objects']
wanted = [m.strip() for m in a.models.split(',') if m.strip()]

touched, skipped = 0, []
for idx, oid in enumerate(ids):
    p = a.project / 'objects' / (oid + '.json')
    if not p.exists():
        continue
    o = json.loads(p.read_text(encoding='utf-8'))
    model = o.get('model', '')
    stem = Path(model).stem if model else ''
    if stem not in wanted:
        continue
    far = f'res/models/impostors/{stem}-far.obj'
    if not (a.project / far).exists():
        raise SystemExit(f'No baked impostor at {far} - run --bake-impostor first')
    ok, why = eligible(o)
    if not ok:
        skipped.append((idx, o.get('name', ''), why))
        continue
    o['impostor'] = far
    o['impostorBillboard'] = True
    o['impostorViews'] = a.views
    o['impostorDistance'] = a.distance
    p.write_text(json.dumps(o, indent=2), encoding='utf-8')
    touched += 1
    print(f'  {idx:>4} {o.get("name",""):<24} {stem:<16} distance={a.distance}')

print(f'{touched} objects assigned {a.views}-view impostors at distance {a.distance}'
      + (' (CONTROL: replacement disabled)' if a.distance == 0 else ''))
for idx, name, why in skipped:
    print(f'  SKIPPED {idx} {name}: {why} - the runtime would fall back silently')
if skipped:
    raise SystemExit('Refusing to continue: an ineligible object would make the '
                     'arm measure less than it claims')
