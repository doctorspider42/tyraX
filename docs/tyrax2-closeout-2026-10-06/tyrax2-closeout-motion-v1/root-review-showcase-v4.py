from pathlib import Path
import hashlib,json
base=Path('F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-motion-v1/quiet-v4/showcase')
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=json.loads((base/'source-review.json').read_text())
native=json.loads((base/'native-proof.json').read_text())
assert native['status']=='PASS_PRIVATE_QUIET_V4_ONE_ARM_NATIVE_SOURCE_OBJECT_LINK_BINDING'
assert native['sourceReviewSha256']==digest(base/'source-review.json')
for rel,h in source['files'].items():
 p=base.parent/rel if rel.startswith('tyra/') else base/rel
 assert digest(p)==h,rel
elf=base/'game/bin/showcase.elf'
assert digest(elf)==native['actualELFSha256']
roles=[('native-proof',base/'native-proof.json'),('source-review',base/'source-review.json'),('elf',elf),('symbol',Path(str(elf)+'.sym')),('sample-config',base/'game/bin/closeout.cfg')]
linux=lambda p:'/mnt/f/'+p.as_posix()[3:]
review=dict(status='ROOT_REVIEWED_CLOSEOUT_INPUTS',rootReviewed=True,noAddedWaitsReviewed=True,ordinarySamplerOnlyReviewed=True,fixture=linux(base/'game'),inputs=[dict(role=r,path=linux(p),sha256=digest(p))for r,p in roles],sourceFrozenByRoot=True,reviewScope='Quiet one-arm Players showcase portal route. Independently verified all source hashes and native source/ELF binding. Reviewed controller, cold witnesses and rare successful-teleport increments. Emulator correctness only; hardware price and observer footprint pending.')
(base/'root-runtime-review-quiet-showcase.json').write_text(json.dumps(review,indent=2)+'\n')
print('PASS_ROOT_SHOWCASE_SOURCE_HASH_REVIEW')
