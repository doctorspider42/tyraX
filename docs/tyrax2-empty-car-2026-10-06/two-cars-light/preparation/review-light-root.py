from pathlib import Path
import json, hashlib

lab=Path('F:/Projects/tyrax2-lab-20261001')
r=lab/'tyrax2-closeout-road-two-cars-light-v1'
h=lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
s=json.loads((r/'source-review.json').read_text())
old=json.loads((lab/'tyrax2-closeout-road-two-cars-v1/source-review.json').read_text())
changed={k for k,v in s['files'].items() if old['files'].get(k)!=v}
assert changed=={'game/src/terrain_game.cpp','tyra/engine/inc/debug/empty_witness.hpp','tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp','tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'}, changed
for name,digest in s['files'].items():
    p=r/name.replace('game/','empty-car/',1) if name.startswith('game/') else r/name
    assert h(p)==digest, name
n=json.loads((r/'native-proof.json').read_text())
assert n['sourceReviewSha256']==h(r/'source-review.json')
assert h(r/'empty-car/bin/empty-car.elf')==n['actualELFSha256']
assert h(r/'empty-car/bin/empty-car.elf.sym')==n['actualSymbolSha256']
inputs=[]
for role,rel in [('source-review','source-review.json'),('native-proof','native-proof.json'),('elf','empty-car/bin/empty-car.elf'),('symbol','empty-car/bin/empty-car.elf.sym'),('sample-config','empty-car/bin/closeout.cfg')]:
    p=r/rel
    inputs.append(dict(role=role,path=p.as_posix().replace('F:/','/mnt/f/'),sha256=h(p)))
d=dict(status='ROOT_REVIEWED_CLOSEOUT_INPUTS',rootReviewed=True,noAddedWaitsReviewed=True,
       ordinarySamplerOnlyReviewed=True,sourceFrozenByRoot=True,
       fixture=(r/'empty-car').as_posix().replace('F:/','/mnt/f/'),inputs=inputs,
       changedSources=sorted(changed),
       reviewScope='Same two-car fixture, controller and ambient; one public-API point light with live-dt sinusoidal orbit. Cold predicates precede added selection/effective tests; actual point enabled, local filtered spot and negative colour-program lane witnessed per synchronous body/wheel definition. No new clocks/waits. No pools/coronas/headlights/night/sky/audio enabled. Native hashes bound. Cold encoding is not final raster certification or every-frame activation.')
(r/'root-emulator-review.json').write_bytes((json.dumps(d,indent=2)+'\n').encode())
print('PASS root reviewed',len(s['files']),'source pins and four changed source scopes')
