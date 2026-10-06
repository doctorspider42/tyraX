from pathlib import Path
import shutil,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');prep=lab/'tyrax2-city-physics-v2';emu=lab/'tyrax2-city-physics-emulator-v1';dest=repo/'docs/tyrax2-city-physics-2026-10-06';assert not dest.exists()
def copy(src,name):
 p=dest/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,p)
for f in ['README.md','prove-native.py','root-release.py','root-review.json','prepare-parser.py','analyze-physics.py','prepare-proof.py','parser-controls.py','archive-preparation.py']:
 copy(prep/f,'preparation/'+f)
copy(lab/'prepare-city-physics-v1.py','preparation/prepare-city-physics-v1.py')
for f in ['source-review.json','native-proof.json','native-build.log','native-build.err']:copy(prep/'vehicle'/f,'preparation/vehicle/'+f)
for f in ['src/gen/game_vehicles.gen.cpp','inc/motion_route.hpp','bin/closeout.cfg']:
 copy(prep/'vehicle/game'/f,'changed-source/game/'+f)
for f in ['inc/debug/closeout.hpp','inc/debug/vehicle_probe.hpp','inc/debug/night_sampler.hpp']:
 copy(prep/'tyra/engine'/f,'changed-source/tyra/engine/'+f)
copy(prep/'parser-controls/controls.json','preparation/parser-controls.json')
for f in ['owned-launch.json','launch-attempt.json','owned-stop.json','strict-result.json']:copy(emu/f,'emulator/'+f)
for cap in ['capture-start','capture-final']:
 for f in(emu/cap).iterdir():
  if f.is_file():copy(f,'emulator/'+cap+'/'+f.name)
copy(emu/'runtime/profile/PCSX2/inis/PCSX2.ini','emulator/PCSX2.ini')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
pins={p.relative_to(dest).as_posix():sha(p)for p in dest.rglob('*')if p.is_file()}
(dest/'inventory.json').write_text(json.dumps(dict(status='IMMUTABLE_PREPARATION_EVIDENCE_PINS',files=pins),indent=2)+'\n')
print('PREPARATION_ARCHIVED',len(pins))
