from pathlib import Path
import json,hashlib,shutil,ast
repo=Path('F:/Projects/tyra-editor');lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'strip-batch-trial-v2';dst=repo/'docs/tyrax2-strip-batch-2026-10-07';assert not dst.exists();dst.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def cp(p,rel):
 q=dst/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
old=repo/'docs/tyrax2-queue-audit-2026-10-06';refs=json.loads((old/'parent-source-references.json').read_text())['unchanged'];native=json.loads((r/'native-proof.json').read_text());references={};changed={}
for rel,v in native['sources'].items():
 f=r/'vehicle/game'/rel[5:]if rel.startswith('game/')else r/rel;assert sha(f)==v['sha256']
 parent=repo/refs[rel]['path']if rel in refs else old/'source'/rel
 if parent.is_file()and sha(parent)==v['sha256']:references[rel]=dict(path=parent.relative_to(repo).as_posix(),sha256=v['sha256'])
 else:cp(f,'source/'+rel);changed[rel]=v['sha256']
(dst/'source-binding.json').write_text(json.dumps(dict(parentCommit='28628d8d7',unchanged=references,changed=changed),indent=2)+'\n')
for rel in ['native-proof.json','source-review.json','root-review.json','native-build.log']:cp(r/rel,rel)
for name in ['prepare-batch.py','prove-batch.py','prove-batch-v2.py','analyze-batch.py','qualify-batch.py','archive-batch.py','pack-oracle.cpp','pack-oracle.result.txt','strip_pack.hpp','wrapper.cpp','repack.py','repack-report.json','meshstrip.cpp','meshstrip.hpp']:
 cp(lab/'strip-trial-v1'/name,'preparation/'+name)
for name in ['strip-inventory-all.py','strip-inventory-all.json','launch-closeout-physical.ps1']:cp(lab/name,'preparation/'+name)
for name in ['owned_emulator_common.py','launch-closeout-root.py','observe-closeout-root.py','stop-closeout-root.py']:cp(lab/'tyrax2-closeout-motion-v1'/name,'preparation/'+name)
emu=r/'emulator-v1'
for rel in ['owned-launch.json','launch-attempt.json','owned-stop.json','strict-result.json','raster-comparison.json','parser-controls.json','runtime/profile/PCSX2/inis/PCSX2.ini']:
 if(emu/rel).is_file():cp(emu/rel,'emulator/'+rel)
for label in ['capture-first','capture-control','completed']:
 for p in (emu/label).glob('*'):
  if p.is_file():cp(p,'emulator/'+label+'/'+p.name)
for rel in ['native-proof.json','root-review.json','emulator-v1/owned-launch.json','emulator-v1/owned-stop.json','emulator-v1/completed/runtime-emulator.log','emulator-v1/completed/bin-closeout-samples.txt','emulator-v1/capture-first/frame.png','emulator-v1/capture-control/frame.png','vehicle/game/src/terrain_game.cpp']:
 p=lab/'strip-batch-trial-v1'/rel
 if p.is_file():cp(p,'rejected-controls-card-v1/'+rel)
run=lab/'tyrax2-closeout-ps2-strip-batch-v2'
if run.is_dir():
 for name in ['launch-inputs.json','owned-launch.json','strict-result.json','root-qualification.json','completed.stdout','completed.log','completed-samples.txt','ps2client.stderr','poweroff-result.json']:
  if(run/name).is_file():cp(run/name,'physical/'+name)
for p in dst.rglob('*.py'):ast.parse(p.read_text())
print('ARCHIVED_STRIP_BATCH',len(references),len(changed))
