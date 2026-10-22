from pathlib import Path
import json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');src=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1';r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-assets-v2';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
d=json.loads((src/'root-emulator-review.json').read_text(encoding='utf8'))
n=json.loads((src/'native-proof.json').read_text(encoding='utf8'))
assert h(r/'empty-car/bin/empty-car.elf')==n['actualELFSha256']
p=r/'empty-car/bin/hud/flare-glow.png';public=Path('F:/Projects/tyra-editor/examples/vehicle-playground/res/hud/flare-glow.png')
assert p.is_file() and h(p)==h(public)
for item in d['inputs']:
 if item['role']in ('elf','symbol','sample-config'):
  filename={'elf':'empty-car.elf','symbol':'empty-car.elf.sym','sample-config':'closeout.cfg'}[item['role']]
  path=r/'empty-car/bin'/filename
  assert h(path)==item['sha256'],item['role']
  item['path']=path.as_posix().replace('F:/','/mnt/f/')
assets=[]
for path in sorted((r/'empty-car/bin').rglob('*')):
 if path.is_file():assets.append(dict(path=path.relative_to(r/'empty-car/bin').as_posix(),sha256=h(path)))
proof=dict(status='ROOT_VERIFIED_SAME_ELF_COMPLETE_SHADOW_RUNTIME_ASSETS_V2',sourceFixture=str(src),nativeProofSha256=h(src/'native-proof.json'),elfSha256=n['actualELFSha256'],addedPublicResource=str(public),addedResourceSha256=h(p),files=assets)
(r/'root-assets-review.json').write_bytes((json.dumps(proof,indent=2)+'\n').encode())
d['fixture']=(r/'empty-car').as_posix().replace('F:/','/mnt/f/')
d['inputs'].append(dict(role='runtime-assets-review',path=(r/'root-assets-review.json').as_posix().replace('F:/','/mnt/f/'),sha256=h(r/'root-assets-review.json')))
d['reviewScope']+=' Rejected first attempt for missing default flare-glow.png; this separate runtime closure supplies the original public asset, retaining identical compiled source/ELF and all prior assets.'
(r/'root-emulator-review.json').write_bytes((json.dumps(d,indent=2)+'\n').encode())
print('PASS root corrected runtime closure; unchanged ELF',n['actualELFSha256'])
