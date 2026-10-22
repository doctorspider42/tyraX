from pathlib import Path
import hashlib,json
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1'
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
s=json.loads((r/'source-review.json').read_text(encoding='utf8'))
old=json.loads((lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1/source-review.json').read_text(encoding='utf8'))
changed={k for k,v in s['files'].items() if old['files'].get(k)!=v}
expected={'game/src/terrain_game.cpp','game/src/gen/game_collision.gen.cpp','game/inc/batch_trial.hpp'}
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
b=json.loads((lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-v1/building-assets-proof.json').read_text(encoding='utf8'))
assert b['instanceBudgetVertices']==70800 and b['instancesPerModel']==25
for name,m in b['models'].items():
 assert h(Path(m['publicPrebake']))==m['publicPrebakeSha256']==h(r/'empty-car/bin/models/kenney'/ (name+'.tmdl'))
 for part in m['parts']: assert h(r/'empty-car/bin'/part['texture'])==part['textureSha256']
inputs=[]
for role,rel in [('source-review','source-review.json'),('native-proof','native-proof.json'),('elf','empty-car/bin/empty-car.elf'),('symbol','empty-car/bin/empty-car.elf.sym'),('sample-config','empty-car/bin/closeout.cfg')]:
 p=r/rel;inputs.append(dict(role=role,path=p.as_posix().replace('F:/','/mnt/f/'),sha256=h(p)))
d=dict(status='ROOT_REVIEWED_CLOSEOUT_INPUTS',rootReviewed=True,noAddedWaitsReviewed=True,ordinarySamplerOnlyReviewed=True,sourceFrozenByRoot=True,fixture=(r/'empty-car').as_posix().replace('F:/','/mnt/f/'),inputs=inputs,changedSources=sorted(changed),reviewScope='Root reviewed three source changes: retained vector<short> membership and batch buffers, OFF solo routing plus batch skip, ON original membership, neutral pad, phase-local identical light trajectory. No added waits or clocks; ordinary sampler only. Expected runtime exact 80187 vertices and 37/313 carriers. Parser must verify corresponding camera/car/light words; runtime/image qualification pending. Contrast is net representation cost, common observer unpriced.')
(r/'root-emulator-review.json').write_bytes((json.dumps(d,indent=2)+'\n').encode())
print('PASS root source/native input review',len(s['files']),sorted(changed))
