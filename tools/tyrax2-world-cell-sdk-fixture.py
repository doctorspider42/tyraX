"""A44 actual EE SDK compiler execution fixture, not a performance candidate."""
import argparse,hashlib,importlib.util,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert q.sha(a.source/rel)==h,rel
for part in ('game','tyra/engine'):
    shutil.copytree(a.source/part,a.out/part,ignore=q.ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
engine=a.out/'tyra/engine'
for name in ('plan','sdk','sdk-check'):
    shutil.copy2(repo/f'tools/tyrax2-world-cell-{name}.hpp',
                 engine/f'inc/debug/world_cell_{name.replace("-","_")}.hpp')
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
text=renderer.read_text()
text=q.once(text,'#include "debug/clip_ring.hpp"',
            '#define WORLD_CELL_ENGINE\n#include "debug/world_cell_sdk_check.hpp"\n#include "debug/clip_ring.hpp"')
anchor='  if(!worldEligible(bag) || !ClipRing::active || !vu1Clipping || hasProgramOverrides())return false;'
text=q.once(text,anchor,anchor+'''
  if(!WorldCellSdkCheck::done && bag->texture && bag->count>=3){
    StaPipBag plain;plain.info=bag->info;plain.color=bag->color;
    plain.vertices=bag->vertices;plain.count=bag->count;
    WorldCellSdk::Binding untextured{&plain,getClipProgramByBag(&plain),*prim,
      {17,0},worldEpoch,bufferSize,true};
    WorldCellSdk::Binding textured{bag,getClipProgramByBag(bag),*prim,
      {17,0},worldEpoch,bufferSize,true};
    WorldCellSdkCheck::run(untextured,textured);
  }
''')
renderer.write_text(text)
physics=a.out/'game/src/gen/game_physics.gen.cpp'
text=physics.read_text()
text=q.once(text,'core.setDiagnosticTriple(arm==1,true);','core.setDiagnosticTriple(false,true);')
text=q.once(text,'core.setDiagnosticTriple(MotionProbe::phase%3!=1,true);',
            'core.setDiagnosticTriple(false,true);')
physics.write_text(text)
manifest={'experiment':'A44-sdk-execution','baseElfSha256':binding['elfSha256'],
          'status':'UNQUALIFIED','worldRuntimeChanged':False,'hybridBuffers':2,
          'limits':['Cold EE compiler execution only, no new GPU route or FPS price.',
                    'C/TC list and independent strip stream delivery gate.',
                    'No view/material heads, VRAM reservation or DMA/GS lease integration.']}
(a.out/'world-cell-sdk-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED actual SDK execution fixture')
