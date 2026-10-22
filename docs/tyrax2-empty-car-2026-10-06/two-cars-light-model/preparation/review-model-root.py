from pathlib import Path
import json, hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-model-v1'
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
s=json.loads((r/'source-review.json').read_text(encoding='utf8'))
old=json.loads((lab/'tyrax2-closeout-road-two-cars-light-v1/source-review.json').read_text(encoding='utf8'))
changed={k for k,v in s['files'].items() if old['files'].get(k)!=v}
assert changed=={'game/src/terrain_game.cpp','game/src/gen/game_scene.gen.cpp','game/src/gen/scene_objects.gen.cpp','game/inc/model_data.gen.hpp','game/inc/scene_data.hpp','tyra/engine/inc/debug/empty_witness.hpp','game/empty-car.tyra'},changed
for name,digest in s['files'].items():
    p=r/name.replace('game/','empty-car/',1) if name.startswith('game/') else r/name
    assert h(p)==digest,name
n=json.loads((r/'native-proof.json').read_text(encoding='utf8'))
assert n['sourceReviewSha256']==h(r/'source-review.json')
assert h(r/'empty-car/bin/empty-car.elf')==n['actualELFSha256']
assert h(r/'empty-car/bin/empty-car.elf.sym')==n['actualSymbolSha256']
assets=json.loads((r/'static-model-assets-proof.json').read_text(encoding='utf8'))
assert assets['parts'][0]['vertices']==96 and assets['parts'][0]['triangles']==32
for rel,pin in assets['runtimeFiles'].items():
    assert h(Path(pin['source']))==pin['sha256']==h(r/'empty-car/bin'/rel),rel
inputs=[]
for role,rel in [('source-review','source-review.json'),('native-proof','native-proof.json'),('elf','empty-car/bin/empty-car.elf'),('symbol','empty-car/bin/empty-car.elf.sym'),('sample-config','empty-car/bin/closeout.cfg')]:
    p=r/rel;inputs.append(dict(role=role,path=p.as_posix().replace('F:/','/mnt/f/'),sha256=h(p)))
d=dict(status='ROOT_REVIEWED_CLOSEOUT_INPUTS',rootReviewed=True,noAddedWaitsReviewed=True,
       ordinarySamplerOnlyReviewed=True,sourceFrozenByRoot=True,
       fixture=(r/'empty-car').as_posix().replace('F:/','/mnt/f/'),inputs=inputs,changedSources=sorted(changed),
       reviewScope='Same two cars, short controller and moving point; one static public tree with one textured96-vertex material at x5.6,z8, collision/shadows off. Authored/generated model tables rebuilt coherently; vehicle slots shifted and cold identities remain actual definition-based. Synchronous actual owner4 scope sets carrier5 only at cold frames; car/model/road accepted sums close. Four car light hooks and sampler unchanged. No new waits/clocks/FX/audio. Native and three original model runtime asset hashes checked. Sparse acceptance is not raster or every-frame proof.')
(r/'root-emulator-review.json').write_bytes((json.dumps(d,indent=2)+'\n').encode())
print('PASS root source pins',len(s['files']),'native and static runtime asset closure')
