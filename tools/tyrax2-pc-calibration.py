"""Clone bound V7 for isolated SDK interrupted-PC calibration, no renderer price."""
import argparse,hashlib,importlib.util,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();source=a.source.resolve();out=a.out.resolve();assert not out.exists()
repo=Path(__file__).resolve().parents[1]
b=json.loads((source/'native-binding.json').read_text())
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
for rel,digest in b['files'].items():assert sha(source/rel)==digest,rel
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
for part in ('game','tyra/engine'):shutil.copytree(source/part,out/part,ignore=q.ignore)
shutil.copy2(source/'tyra/Makefile.base',out/'tyra/Makefile.base')
shutil.copy2(source/'authored-manifest.json',out/'authored-manifest.json')
for original,name in [('tyrax2-pc-sample-store.hpp','pc_sample_store.hpp'),('tyrax2-pc-calibration.hpp','pc_calibration.hpp')]:
    shutil.copy2(repo/'tools'/original,out/'tyra/engine/inc/debug'/name)
game=out/'game/src/terrain_game.cpp'
text=game.read_text(encoding='utf-8-sig')
text='#include "debug/pc_calibration.hpp"\n'+q.once(text,'void TerrainGame::loop() {',
    'void TerrainGame::loop() {\n  PcCalibration::run();return;')
game.write_text(text,encoding='utf-8')
sdk=Path('C:/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2sdk')
pins={str(f.relative_to(sdk)).replace('\\','/'):sha(f) for f in
      [sdk/'ee/lib/libkernel.a',sdk/'ee/include/kernel.h',sdk/'ee/include/timer.h',sdk/'ee/include/timer_alarm.h']}
(out/'pc-calibration-manifest.json').write_text(json.dumps(dict(parentElf=b['elfSha256'],source=str(source),
    scope='Two known leaf loops/two periods, SDK alarms only; no renderer or pricing',sdkPins=pins),indent=2)+'\n')
print(out)
