from pathlib import Path
import shutil,json,hashlib
repo=Path('F:/Projects/tyra-editor');lab=Path('F:/Projects/tyrax2-lab-20261001');r=Path(__file__).parent;out=repo/'docs/tyrax2-loop-balance-2026-10-06'
assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for folder in ['tyrax2-empty-car-2026-10-06','tyrax2-city-day-drive-2026-10-06','tyrax2-city-physics-2026-10-06']:
 d=repo/'docs'/folder;j=json.loads((d/'inventory.json').read_text());pins={a['path']:a['sha256']for a in j}if isinstance(j,list)else j['files']
 for n,h in pins.items():assert sha(d/n)==h,(folder,n)
def cp(src,rel):
 dst=out/rel;assert not dst.exists();dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
for name in ['prove-native.py','review-coverage.py','root-release.py','root-review.json','analyze-day-drive.py','analyze-balance.py','parser-controls.py','freeze-physical.py','qualify-physical.py','archive-evidence.py']:
 cp(r/name,'preparation/'+name)
cp(lab/'prepare-loop-balance.py','preparation/prepare-loop-balance.py')
for name in ['source-review.json','native-proof.json','coverage-review.json','native-build-v1.log','native-build-v2.log','actual-symbols.txt']:
 cp(r/'vehicle'/name,'preparation/'+name)
parent=json.loads((lab/'tyrax2-city-day-drive-v1/vehicle/source-review.json').read_text())['files'];new=json.loads((r/'vehicle/source-review.json').read_text())['files']
for name,h in new.items():
 if parent.get(name)==h:continue
 source=r/'vehicle/game'/name[5:]if name.startswith('game/')else r/name
 cp(source,'source/'+name)
for name in ['launch-attempt.json','owned-launch.json','owned-stop.json','strict-result.json','strict-result.base.json']:
 cp(r/'emulator-v1'/name,'emulator/'+name)
cp(r/'emulator-v1/runtime/profile/PCSX2/inis/PCSX2.ini','emulator/PCSX2.ini')
for f in (r/'emulator-v1/completed').iterdir():
 if f.is_file():cp(f,'emulator/completed/'+f.name)
cp(r/'parser-controls/controls.json','preparation/parser-controls.json')
for f in (r/'parser-controls').glob('*.stderr'):cp(f,'preparation/parser-rejections/'+f.name)
run=lab/'tyrax2-closeout-ps2-loop-balance-v1'
for f in ['owned-launch.json','launch-inputs.json','strict-result.json','strict-result.base.json','root-qualification.json']:
 cp(run/f,'physical/'+f)
cp(run/'bin/closeout.cfg','physical/closeout.cfg')
for f in (run/'completed-prefix').iterdir():
 if f.is_file():cp(f,'physical/completed-prefix/'+f.name)
cp(lab/'launch-closeout-physical.ps1','preparation/launch-closeout-physical.ps1')
print('ARCHIVED_SOURCE_NATIVE_EMULATOR_PHYSICAL_EVIDENCE',out)
