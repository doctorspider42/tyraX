from pathlib import Path
import hashlib,json,shutil,ast
root=Path(__file__).parent;lab=root.parent;repo=Path('F:/Projects/tyra-editor');dst=repo/'docs/tyrax2-queue-audit-2026-10-06';assert not dst.exists();dst.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def cp(src,rel):
 target=dst/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,target)
parent=repo/'docs/tyrax2-render-audit-2026-10-06/qualified-v2/source';review=json.loads((root/'vehicle/source-review.json').read_text());references={};changed={}
for rel,h in review['files'].items():
 src=root/'vehicle/game'/rel[5:]if rel.startswith('game/')else root/rel
 assert sha(src)==h,rel
 old=parent/rel
 if old.is_file()and sha(old)==h:references[rel]=dict(path=old.relative_to(repo).as_posix(),sha256=h)
 else:cp(src,'source/'+rel);changed[rel]=h
(dst/'parent-source-references.json').write_text(json.dumps(dict(status='DELTA_SOURCE_BINDING_WITH_IMMUTABLE_PARENT',parentCommit='f91f587b0',unchanged=references,changed=changed),indent=2)+'\n')
for name in ['prove-native.py','root-release.py','review-queue-coverage.py','analyze-day-drive.py','analyze-balance.py','analyze-queue.py','parser-controls.py','parser-queue-controls.py','freeze-physical.py','qualify-queue-physical.py','archive-queue-audit.py']:
 cp(root/name,'preparation/'+name)
for name in ['prepare-queue-audit.py','prepare-queue-audit-v2.py','queue-audit-header.hpp','launch-closeout-physical.ps1']:cp(lab/name,'preparation/'+name)
for rel in ['root-review.json','vehicle/source-review.json','vehicle/native-proof.json','vehicle/queue-coverage-review.json','vehicle/native-build-v1.log']:cp(root/rel,rel)
emu=root/'emulator-v1'
for rel in ['owned-launch.json','launch-attempt.json','strict-result.json','strict-result.balance.json','strict-result.balance.base.json','completed/runtime-emulator.log','completed/runtime-stdout.log','completed/bin-closeout-samples.txt','completed/frame.png','completed/observe-proof.json','runtime/profile/PCSX2/inis/PCSX2.ini','queue-controls/controls.json','balance-controls/controls.json','owned-stop.json']:
 src=emu/rel
 if src.exists():cp(src,'emulator/'+rel)
for label in ['queue-controls','balance-controls']:
 for src in (emu/label).glob('*.stderr'):cp(src,'emulator/'+label+'/'+src.name)
bad=lab/'tyrax2-queue-audit-v1'
for rel in ['vehicle/native-build-v1.log','vehicle/source-review.json','tyra/engine/inc/debug/render_audit.hpp']:cp(bad/rel,'rejected-size-guard-v1/'+rel)
run=lab/'tyrax2-closeout-ps2-queue-audit-v1'
for rel in ['launch-inputs.json','owned-launch.json','strict-result.json','strict-result.balance.json','strict-result.balance.base.json','root-qualification.json','bin/closeout.cfg']:
 src=run/rel
 if src.exists():cp(src,'physical/'+rel)
for src in (run/'completed-prefix').glob('*'):cp(src,'physical/completed-prefix/'+src.name)
for p in dst.rglob('*.py'):ast.parse(p.read_text(),filename=str(p))
print('QUEUE_EVIDENCE_ARCHIVED',dst)
