from pathlib import Path
import shutil,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');prep=lab/'tyrax2-city-day-drive-v1';emu=lab/'tyrax2-city-day-drive-emulator-v1';physical=lab/'tyrax2-closeout-ps2-city-day-drive-v2';dest=repo/'docs/tyrax2-city-day-drive-2026-10-06';assert not dest.exists()
def copy(src,relative):
 d=dest/relative;d.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,d)
for f in ['README.md','prove-native.py','root-release.py','root-review.json','prepare-parser.py','analyze-day-drive.py','parser-controls.py','freeze-physical.py','archive.py']:
 copy(prep/f,'preparation/'+f)
copy(lab/'prepare-city-day-drive-v1.py','preparation/prepare-city-day-drive-v1.py')
for f in ['source-review.json','native-proof.json','native-build.log','native-build.err','native-build-v2.log','native-build-v2.err']:
 copy(prep/'vehicle'/f,'preparation/vehicle/'+f)
for f in ['src/terrain_game.cpp','inc/scene_data.hpp','inc/motion_route.hpp','inc/terrain_config.hpp','Makefile','bin/closeout.cfg']:
 copy(prep/'vehicle/game'/f,'changed-source/game/'+f)
for f in ['inc/debug/closeout.hpp','inc/debug/night_sampler.hpp','src/engine.cpp','src/renderer/core/renderer_core.cpp','inc/debug/frame_profile.hpp']:
 copy(prep/'tyra/engine'/f,'changed-source/tyra/engine/'+f)
copy(prep/'parser-controls/controls.json','preparation/parser-controls.json')
for f in ['owned-launch.json','launch-attempt.json','owned-stop.json','strict-result.json']:
 if(emu/f).exists():copy(emu/f,'emulator/'+f)
for capture in ['capture-start','capture-park','capture-final']:
 for f in (emu/capture).iterdir():
  if f.is_file():copy(f,'emulator/'+capture+'/'+f.name)
copy(emu/'runtime/profile/PCSX2/inis/PCSX2.ini','emulator/PCSX2.ini')
for f in ['owned-launch.json','launch-inputs.json','strict-result.json']:
 copy(physical/f,'physical/'+f)
for f in (physical/'completed-prefix-v2').iterdir():copy(f,'physical/completed-prefix/'+f.name)
copy(physical/'bin/closeout.cfg','physical/closeout.cfg')
inventory={f.relative_to(dest).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in dest.rglob('*')if f.is_file()}
(dest/'inventory.json').write_text(json.dumps(dict(status='IMMUTABLE_EVIDENCE_PINS',files=inventory),indent=2)+'\n')
print('ARCHIVED',len(inventory),'pins')
