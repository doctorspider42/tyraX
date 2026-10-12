"""Add bounded exact object-source snapshots to the rejected matched ordinary gate."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
manifest=json.loads((a.source/'world-cell-view-manifest.json').read_text())
assert manifest['ordinaryOnly'] and manifest['ordinarySettlingFrames']==32
for phase in range(3):
    report=json.loads((a.source/f'world-cell-view-emulator-phase{phase}.json').read_text())
    assert not report['valid'] and report['activation']['submittedByView']==[0]*4
    assert report['elfSha256']==binding['elfSha256']
    assert report['logSha256']==sha(a.source/'emulator/emulator.log')
    for row in report['images']:
        for rel,digest in zip(row['files'],row['hashes']):assert sha(a.source/rel)==digest
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (n.startswith(('tape-','command-','cell-','source-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','world-cell-terrain-manifest.json',
            'world-cell-view-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
def once(text,old,new):
    assert text.count(old)==1,old
    return text.replace(old,new)
shutil.copy2(repo/'tools/tyrax2-world-source-capture.hpp',a.out/'game/inc/world_source_capture.hpp')
scene=a.out/'game/src/gen/game_scene.gen.cpp'
t=scene.read_text();t=once(t,'#include "game_runtime.gen.hpp"',
    '#include "game_runtime.gen.hpp"\n#include "world_source_capture.hpp"')
t=once(t,'  stapip.core.render(bag, receives ?',
    '  WorldSourceCapture::capture(owner,bag,engine->renderer.core,receives);\n  stapip.core.render(bag, receives ?')
scene.write_text(t)
collision=a.out/'game/src/gen/game_collision.gen.cpp'
t=collision.read_text();t=once(t,'#include "game_runtime.gen.hpp"',
    '#include "game_runtime.gen.hpp"\n#include "world_source_capture.hpp"')
start=t.index('void TerrainGame::renderStaticBatches() {')
end=t.index('void TerrainGame::procBlockVertexAo(',start)
part=t[start:end]
part=once(part,'    submitHeavy(b.bag.get());',
    '    WorldSourceCapture::capture(-10000-int(&b-staticBatches.data()),b.bag.get(),engine->renderer.core,true);\n    submitHeavy(b.bag.get());')
t=t[:start]+part+t[end:];collision.write_text(t)
physics=a.out/'game/src/gen/game_physics.gen.cpp'
t=physics.read_text();t=once(t,'#include "game_runtime.gen.hpp"',
    '#include "game_runtime.gen.hpp"\n#include "world_source_capture.hpp"')
t=once(t,'      renderHeld();','''      if(angle==3 && frame==31)WorldSourceCapture::begin(MotionProbe::phase*3+arm);
      renderHeld();
      if(angle==3 && frame==31)WorldSourceCapture::finish();''')
physics.write_text(t)
manifest['sourceCapture']=True
(a.out/'world-cell-view-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(a.out/'world-source-manifest.json').write_text(json.dumps({
    'baseElfSha256':binding['elfSha256'],'generatorSha256':sha(Path(__file__)),
    'headerSha256':sha(repo/'tools/tyrax2-world-source-capture.hpp'),
    'status':'UNQUALIFIED','limitBytes':2*1024*1024,'stages':list(range(9)),
    'limits':['Solo object and static-batch source boundaries only, not emitted VIF or GS texture bytes.',
              'Cold diagnostic only; no moving candidate or physical price.']},indent=2)+'\n')
print(a.out,'UNQUALIFIED bounded exact object-source snapshots')
