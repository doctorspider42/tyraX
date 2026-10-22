from pathlib import Path
import hashlib,json
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-buildings-v1'
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
s=json.loads((r/'source-review.json').read_text(encoding='utf8'))
old=json.loads((lab/'tyrax2-closeout-road-two-cars-light-model-v1/source-review.json').read_text(encoding='utf8'))
changed={k for k,v in s['files'].items() if old['files'].get(k)!=v}
expected={'game/src/terrain_game.cpp','game/src/gen/game_scene.gen.cpp','game/src/gen/game_collision.gen.cpp','game/src/gen/scene_objects.gen.cpp','game/inc/model_data.gen.hpp','game/inc/scene_data.hpp','tyra/engine/inc/debug/empty_witness.hpp','game/empty-car.tyra'}
assert changed==expected,changed
for name,digest in s['files'].items():
 p=r/name.replace('game/','empty-car/',1) if name.startswith('game/') else r/name
 assert h(p)==digest,name
n=json.loads((r/'native-proof.json').read_text(encoding='utf8'))
assert n['sourceReviewSha256']==h(r/'source-review.json')
assert h(r/'empty-car/bin/empty-car.elf')==n['actualELFSha256']
assert h(r/'empty-car/bin/empty-car.elf.sym')==n['actualSymbolSha256']
assets=json.loads((r/'bin-assets-source-pins.json').read_text(encoding='utf8'))
for rel,digest in assets['files'].items(): assert h(r/'empty-car/bin'/rel)==digest,rel
b=json.loads((r/'building-assets-proof.json').read_text(encoding='utf8'))
assert b['instanceBudgetVertices']==11328 and b['instancesPerModel']==4
for name,m in b['models'].items():
 assert h(Path(m['publicPrebake']))==m['publicPrebakeSha256']==h(r/'empty-car/bin/models/kenney'/ (name+'.tmdl'))
 for part in m['parts']: assert h(r/'empty-car/bin'/part['texture'])==part['textureSha256']
inputs=[]
for role,rel in [('source-review','source-review.json'),('native-proof','native-proof.json'),('elf','empty-car/bin/empty-car.elf'),('symbol','empty-car/bin/empty-car.elf.sym'),('sample-config','empty-car/bin/closeout.cfg')]:
 p=r/rel;inputs.append(dict(role=role,path=p.as_posix().replace('F:/','/mnt/f/'),sha256=h(p)))
d=dict(status='ROOT_REVIEWED_CLOSEOUT_INPUTS',rootReviewed=True,noAddedWaitsReviewed=True,ordinarySamplerOnlyReviewed=True,sourceFrozenByRoot=True,fixture=(r/'empty-car').as_posix().replace('F:/','/mnt/f/'),inputs=inputs,changedSources=sorted(changed),reviewScope='Eight facade instances appended to prior two-car/moving-point/pine fixture. Native/source bindings verified. Root reviewed solo owner scope and stable shown-member static batch registration; table scans occur only at six off-clock cold frames. Four vehicle light arrays remain guarded. Accepted building carriers counted once with membership bits, preserving public batching; mixed carriers rejected. No new clocks/waits, same route/controller and ordinary sampler. Accepted input vertices are not postclip emitted triangles or per-owner raster proof. Common disabled observer footprint remains unpriced.')
(r/'root-emulator-review.json').write_bytes((json.dumps(d,indent=2)+'\n').encode())
print('PASS root source/native input review',len(s['files']),sorted(changed))
