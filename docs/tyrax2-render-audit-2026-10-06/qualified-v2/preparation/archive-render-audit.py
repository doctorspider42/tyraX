from pathlib import Path
import json,hashlib,shutil
root=Path(__file__).parent;lab=root.parent
dst=Path('F:/Projects/tyra-editor/docs/tyrax2-render-audit-2026-10-06')
assert not dst.exists();dst.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def copy(src,target):
 target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,target)
for fixture,name in [(root,'qualified-v2'),(lab/'tyrax2-render-audit-v1','rejected-v1')]:
 pin=json.loads((fixture/'vehicle/source-review.json').read_text())
 for rel,h in pin['files'].items():
  src=fixture/'vehicle/game'/rel[5:]if rel.startswith('game/')else fixture/rel
  assert sha(src)==h,rel;copy(src,dst/name/'source'/rel)
 for src in fixture.glob('*.py'):copy(src,dst/name/'preparation'/src.name)
 for rel in ['root-review.json','vehicle/source-review.json','vehicle/native-proof.json','vehicle/coverage-review.json','vehicle/render-coverage-review.json','vehicle/v2-correction-review.json','vehicle/native-build-v1.log']:
  src=fixture/rel
  if src.exists():copy(src,dst/name/rel)
 emu=fixture/'emulator-v1'
 for rel in ['owned-launch.json','strict-result.json','strict-result.balance.json','strict-result.balance.base.json','rejection.stderr','completed/runtime-emulator.log','completed/runtime-stdout.log','completed/bin-closeout-samples.txt','completed/frame.png','completed/observe-proof.json','render-controls/controls.json','balance-controls/controls.json']:
  src=emu/rel
  if src.exists():copy(src,dst/name/'emulator'/rel)
 for label in ['render-controls','balance-controls']:
  for src in (emu/label).glob('*.stderr'):copy(src,dst/name/'emulator'/label/src.name)
 for src in (emu/'runtime/profile/inis').glob('PCSX2.ini'):copy(src,dst/name/'emulator/PCSX2.ini')
run=lab/'tyrax2-closeout-ps2-render-audit-v1'
for rel in ['launch-inputs.json','owned-launch.json','strict-result.json','strict-result.balance.json','strict-result.balance.base.json','root-qualification.json','closeout.cfg']:
 src=run/rel
 if src.exists():copy(src,dst/'physical'/rel)
for src in (run/'completed-prefix').glob('*'):copy(src,dst/'physical/completed-prefix'/src.name)
copy(lab/'prepare-render-audit.py',dst/'preparation/prepare-render-audit-v1.py')
copy(lab/'launch-closeout-physical.ps1',dst/'preparation/launch-closeout-physical.ps1')
print('ARCHIVED_SOURCES_AND_BOUNDED_EVIDENCE',dst)
