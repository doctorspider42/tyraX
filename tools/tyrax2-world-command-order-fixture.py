"""Pin only held A44 draw order; ordinary moving Auto remains unchanged."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
p.add_argument('--mode',type=int,choices=(0,2),default=2)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
report=json.loads((a.source/'world-command-source-emulator.json').read_text())
assert report['valid'] and report['elfSha256']==binding['elfSha256']
assert report['logSha256']==sha(a.source/'emulator/emulator.log')
for rel,digest in report['files'].items():assert sha(a.source/rel)==digest,rel
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (n.startswith(('tape-','command-','cell-','source-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','world-cell-terrain-manifest.json',
            'world-cell-view-manifest.json','world-source-manifest.json',
            'world-source-switch-manifest.json','world-command-source-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
def once(text,old,new):
    assert text.count(old)==1,old
    return text.replace(old,new)
(a.out/'game/inc/world_held_order.hpp').write_text('''#pragma once
namespace WorldHeldOrder {
inline bool active=false,interleaved=false;
}
''')
vehicles=a.out/'game/src/gen/game_vehicles.gen.cpp'
t=vehicles.read_text();t=once(t,'#include "game_runtime.gen.hpp"',
    '#include "game_runtime.gen.hpp"\n#include "world_held_order.hpp"')
start=t.index('bool TerrainGame::interleaveBegin(bool serializedCost) {')
end=t.index('void TerrainGame::ilAccount(',start)
part=t[start:end]
part=once(part,'  heavyDrawn = 0;',
    '  heavyDrawn = 0;\n  if(WorldHeldOrder::active)return WorldHeldOrder::interleaved;')
t=t[:start]+part+t[end:];vehicles.write_text(t)
physics=a.out/'game/src/gen/game_physics.gen.cpp'
t=physics.read_text();t=once(t,'#include "debug/command_tape.hpp"',
    '#include "debug/command_tape.hpp"\n#include "world_held_order.hpp"')
start=t.index('void TerrainGame::renderCompleteTerrainCheckpoint(){')
end=t.index('void TerrainGame::renderScene() {',start)
part=t[start:end]
part=once(part,'  const Tyra::Vec4 savedLook=cameraLookAt;',f'''
  const bool savedOrderActive=WorldHeldOrder::active,savedOrder=WorldHeldOrder::interleaved;
  WorldHeldOrder::active=true;WorldHeldOrder::interleaved={'true' if a.mode==2 else 'false'};
  const Tyra::Vec4 savedLook=cameraLookAt;''')
part=once(part,'if(WorldSourceCapture::enabled)CommandTape::finish();WorldSourceCapture::finish();',
    f'''TYRA_ASSERT(heavyActive==WorldHeldOrder::interleaved,"A44 held order changed");
        printf("A44ORDER stage=%u mode=%u active=%u\\n",MotionProbe::phase*3+arm,{a.mode}U,unsigned(heavyActive));
        if(WorldSourceCapture::enabled)CommandTape::finish();WorldSourceCapture::finish();''')
part=once(part,'  cameraLookAt=savedLook;',
    '  WorldHeldOrder::active=savedOrderActive;WorldHeldOrder::interleaved=savedOrder;\n  cameraLookAt=savedLook;')
t=t[:start]+part+t[end:];physics.write_text(t)
(a.out/'world-command-order-manifest.json').write_text(json.dumps({
    'baseElfSha256':binding['elfSha256'],'generatorSha256':sha(Path(__file__)),
    'heldMode':a.mode,'movingAutoUnchanged':True,'status':'UNQUALIFIED',
    'limits':['Held order control only, not an interleave performance change.',
              'Inherited command/source observers require new image/state qualification.']},indent=2)+'\n')
print(a.out,'UNQUALIFIED held order',a.mode,'moving Auto unchanged')
