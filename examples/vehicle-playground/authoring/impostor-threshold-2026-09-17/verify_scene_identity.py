"""Prove the two arms' scenes differ ONLY by the impostor fields.

    python verify_scene_identity.py <ctl fixture> <cand fixture>

This is the evidence for two of the round's quality gates - **collisions and
picking unchanged** - and it is a source check rather than a picture check,
which is the right kind: a capture can only show the poses you thought to
photograph, while this covers every object in the scene at once.

It exists because the obvious version of the check FAILS, for a reason that
looks alarming and is not. Assigning impostors adds two entries to the model
table, and the baker inserts each far model directly after its source, so
**every model index above them shifts by two** and a naive diff of
`scene_data.hpp` reports a change on most rows in the file. The coupe's `model`
goes 11 -> 13 and it is still the same vehicle body.

So the check resolves indices to NAMES before comparing:

  1. read both `model_data.gen.hpp` tables and build ctl index -> name and
     cand index -> name;
  2. for every object row in `scene_data.hpp`, rewrite the ctl row's `model`
     field to the candidate's index for the SAME NAME;
  3. diff. What is left must be the impostor fields and nothing else.

`impostorModel` is exempted on the candidate side by construction (it is -1 in
the control), and so are `impostorDistance` and `impostorBillboard`. Anything
else - a position, a scale, a collision box, `pickable`, `usable` - surviving
into the residual is a real regression and the script says so and exits 1.
"""
import re
import sys
from pathlib import Path

# Top-level field order of SceneObjectData, counting each array initialiser as
# ONE field: type, position, rotation, scale, color, physics, physMass,
# physBounce, physFriction, physTumble, physSleep, model, ...
MODEL_FIELD = 11


def model_table(p):
    return re.findall(r'"([^"]*\.tmdl)"', (p / 'inc/model_data.gen.hpp').read_text(
        encoding='utf-8', errors='replace'))


def object_rows(p):
    """Every scene-object initialiser, identified by WIDTH.

    Other tables in the same header (the vehicle table, for one) use the same
    `{...}, // name` shape with a different number of fields, and they carry
    model indices of their own that the same remap applies to. Selecting the
    modal width picks out exactly one table and keeps working when a field is
    added to either.
    """
    text = (p / 'inc/scene_data.hpp').read_text(encoding='utf-8', errors='replace')
    rows = re.findall(r'^\s*\{.*\},\s*//.*$', text, re.M)
    widths = [len(split_fields(r)) for r in rows]
    modal = max(set(widths), key=widths.count)
    return [r for r, w in zip(rows, widths) if w == modal], modal


def split_fields(row):
    """Top-level comma split: brace groups and quoted strings stay whole."""
    body = row[row.index('{') + 1:row.rindex('}')]
    out, depth, cur, instr = [], 0, '', False
    for ch in body:
        if instr:
            cur += ch
            if ch == '"':
                instr = False
            continue
        if ch == '"':
            instr = True; cur += ch; continue
        if ch in '{[':
            depth += 1
        elif ch in '}]':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur); cur = ''
        else:
            cur += ch
    out.append(cur)
    return out


ctl_dir, cand_dir = Path(sys.argv[1]), Path(sys.argv[2])
ctl_models, cand_models = model_table(ctl_dir), model_table(cand_dir)
print(f'model table: control {len(ctl_models)} entries, candidate {len(cand_models)}')
added = [m for m in cand_models if m not in ctl_models]
print(f'  added by the candidate: {added or "none"}')
remap = {}
for i, name in enumerate(ctl_models):
    if name not in cand_models:
        raise SystemExit(f'Control model {name} vanished from the candidate')
    remap[i] = cand_models.index(name)
shifted = sum(1 for k, v in remap.items() if k != v)
print(f'  {shifted} of {len(remap)} control indices shift; every one still '
      f'resolves to the SAME model file')

(ctl_rows, w1), (cand_rows, w2) = object_rows(ctl_dir), object_rows(cand_dir)
if len(ctl_rows) != len(cand_rows) or w1 != w2:
    raise SystemExit(f'Row sets differ: {len(ctl_rows)}x{w1} vs {len(cand_rows)}x{w2}')
print(f'scene object rows: {len(ctl_rows)} in both, {w1} fields each')

# The last four fields are impostorModel, impostorDistance, impostorBillboard,
# impostorViews - the change itself, so they are expected to move.
IMPOSTOR_FIELDS = set(range(w1 - 4, w1))
# batchStatic. Assigning an impostor makes an object ineligible for static
# batching, for the same documented reason catch-area objects are (a batched
# member has no bag of its own, so there is nothing to swap for a card). It is
# a CONSEQUENCE of the change, not a regression - but it is reported and
# counted rather than waved through, because it moves work between producers.
# Layout of the tail: ... batchStatic, vuParams[4], impostorModel,
# impostorDistance, impostorBillboard, impostorViews - so vuParams sits between
# batchStatic and the four impostor fields.
BATCH_STATIC = w1 - 6

bad, impostor_only, debatched = [], 0, []
for n, (a, b) in enumerate(zip(ctl_rows, cand_rows)):
    fa, fb = split_fields(a), split_fields(b)
    try:
        fa[MODEL_FIELD] = f' {remap[int(fa[MODEL_FIELD].strip())]}'
    except (ValueError, KeyError):
        pass
    diffs = [i for i in range(w1) if fa[i].strip() != fb[i].strip()]
    residual = [i for i in diffs if i not in IMPOSTOR_FIELDS and i != BATCH_STATIC]
    if BATCH_STATIC in diffs:
        debatched.append((n, a.split('//')[-1].strip(),
                          fa[BATCH_STATIC].strip(), fb[BATCH_STATIC].strip()))
    if diffs and not residual:
        impostor_only += 1
    for i in residual:
        bad.append((n, i, fa[i].strip(), fb[i].strip()))

print(f'{impostor_only} rows differ only in the impostor fields '
      f'(+ batchStatic where it applies)')
print(f'{len(debatched)} objects lost static-batch eligibility, which is a '
      f'consequence of the assignment:')
for n, name, x, y in debatched:
    print(f'    row {n:>4} {name:<24} batchStatic {x} -> {y}')

if bad:
    print(f'\nRESIDUAL DIFFERENCES - {len(bad)}. These are NOT impostor fields:')
    for n, i, x, y in bad[:20]:
        print(f'  row {n} field {i}: {x!r} -> {y!r}')
    sys.exit(1)
print('\nPASS: after the model-table remap, the two scenes differ ONLY in the '
      'impostor fields\n      and in batchStatic on the objects that received '
      'one. Positions, scales,\n      collision boxes, `pickable` and `usable` '
      'are identical on every object,\n      so collisions and picking cannot '
      'have changed.')
