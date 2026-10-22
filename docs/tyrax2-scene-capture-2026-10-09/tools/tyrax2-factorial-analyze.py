"""Verify same-source roads x night cells before reporting elapsed contrasts."""
import copy
import csv
import hashlib
import json
import sys
from pathlib import Path

roots=[Path(p) for p in sys.argv[1:5]]
assert len(roots)==4, 'Order: day/no-roads, day/roads, night/no-roads, night/roads'
allow_height_shift='--allow-road-height-shift' in sys.argv
models=[]; rows=[]; records=[]; assets=[]
height_shifts=[]
for i,root in enumerate(roots):
    night=i>=2;roads=bool(i%2)
    model=json.loads((root/'game/vehicle-playground.tyra').read_text(encoding='utf-8-sig'))
    ids=model['scenes'][0]['objects']
    road_ids=[oid for oid in ids if json.loads((root/'game/objects'/(oid+'.json')).read_text())['type']=='road']
    assert len(road_ids)==7
    fixture=json.loads((root/'source-manifest.json').read_text())
    if i==3:
        assert fixture['nightRoads']
    else:
        assert fixture['night']==night and fixture['roads']==roads
    if not roads:
        assert 'if(scene==0) return;' in (root/'game/src/gen/game_vehicles.gen.cpp').read_text()
    mood=next(v for v in model['saveValues'] if v['name']=='district-night')
    assert mood['default']==int(night)
    normalized=copy.deepcopy(model)
    normalized['scenes'][0]['objects']=[oid for oid in ids if oid not in road_ids]
    next(v for v in normalized['saveValues'] if v['name']=='district-night')['default']=0
    models.append(normalized)
    raw=list(csv.DictReader((root/'physical/bin/drive-probe.csv').open()))
    assert len(raw)==2520
    rows.append(raw)
    analysis=json.loads((root/'physical/bin/turn-analysis.json').read_text())
    assert [p['mode'] for p in analysis['phases']]==[0,6,0]
    records.append(dict(fixture=str(root),night=night,roads=roads,
        elfSha256=hashlib.sha256((root/'game/bin/vehicle-playground.elf').read_bytes()).hexdigest(),
        phases=[p['turn'] for p in analysis['phases']]))
    launch=json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig'))
    assets.append({k:v for k,v in launch.items() if k!='vehicle-playground.elf'})
assert all(m==models[0] for m in models), 'Uncontrolled authored settings'
assert all(a==assets[0] for a in assets), 'Uncontrolled staged assets'
invariants=['game/inc/shadow_data.gen.hpp',
    'tyra/engine/inc/renderer/3d/pipeline/static/core/bag/stapip_texture_bag.hpp',
    'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp',
    'tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tce_vu1.vclpp',
    'tyra/engine/src/renderer/3d/pipeline/static/core/programs/clip/stapip_clip_tc_vu1.vclpp']
for path in invariants:
    assert all((r/path).read_bytes()==(roots[0]/path).read_bytes() for r in roots),path
for i,root in enumerate(roots):
    source_model=json.loads((roots[0]/'game/vehicle-playground.tyra').read_text())
    for oid in source_model['scenes'][0]['objects']:
        path=Path('game/objects')/(oid+'.json')
        assert json.loads((root/path).read_text())==json.loads((roots[0]/path).read_text()),oid
    shift=float(rows[i][0]['y'])-float(rows[0][0]['y'])
    height_shifts.append(shift)
    # Road omission also removes its 0.12-unit contact surface. This is a
    # separately labelled natural intervention, never the strict pose oracle.
    expected_shift=.12 if allow_height_shift and i%2 else 0
    assert abs(shift-expected_shift)<.00002,(i,shift,expected_shift)
    assert all(abs(float(r[k])-float(q[k])-(expected_shift if k in ('y','eyeY','lookY') else 0))<.00002
               for r,q in zip(rows[i],rows[0])
               for k in ('x','y','z','yaw','eyeX','eyeY','eyeZ','lookX','lookY','lookZ'))

contrasts={}
for phase,name in [(0,'eeBefore'),(1,'vuPaint'),(2,'eeAfter')]:
    means=[r['phases'][phase]['workMs'] for r in records]
    contrasts[name]=dict(roadsDayMs=means[1]-means[0],roadsNightMs=means[3]-means[2],
        nightNoRoadsMs=means[2]-means[0],nightRoadsMs=means[3]-means[1],
        interactionMs=means[3]-means[2]-means[1]+means[0])
result=dict(cells=records,contrasts=contrasts,identicalStagedNonElfFiles=len(assets[0]),
            sameNormalizedManifest=True,commonMainObjects=len(source_model['scenes'][0]['objects']),
            identicalSourceInvariants=invariants,
            strictIdenticalWorldPose=not allow_height_shift,
            verifiedRoadHeightShifts=height_shifts,residualPoseTolerance=.00002,
            limitation='Separate fixtures; road contrasts include the 0.12-unit ride/camera height change. Not pure draw or EE/VU/GS costs.')
Path(sys.argv[5]).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(contrasts,indent=2))
