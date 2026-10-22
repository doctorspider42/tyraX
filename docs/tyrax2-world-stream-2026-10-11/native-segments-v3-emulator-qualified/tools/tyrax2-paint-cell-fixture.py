"""Price existing VU paint against accepted direct terrain cells, actual Hybrid2 both roles."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
p.add_argument('--physical-attempt',type=Path,required=True)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==binding['elfSha256']
for platform,location in [('emulator',a.source),('ps2',a.physical_attempt)]:
    gate=json.loads((location/f'world-cell-price-{platform}.json').read_text())
    assert gate['valid'] and gate['variant']=='direct-cold'
    assert gate['elfSha256']==binding['elfSha256']

def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
repo=Path(__file__).resolve().parent
shutil.copy2(repo/'tyrax2-paint-cell-price.hpp',a.out/'tyra/engine/inc/debug/paint_cell_price.hpp')

def edit(rel,old,new):
    file=a.out/rel;t=file.read_text();assert t.count(old)==1,(rel,old)
    file.write_text(t.replace(old,new))

edit('game/inc/world_moving.hpp','#include "debug/world_cell_complete.hpp"',
     '#include "debug/world_cell_complete.hpp"\n#include "debug/paint_cell_price.hpp"')
edit('game/inc/world_moving.hpp','WorldCellComplete::directCold=p<6 && modes[p];',
     'WorldCellComplete::directCold=true;PaintCellPrice::enabled=p<6 && modes[p];PaintCellPrice::held=false;')
edit('game/inc/world_moving.hpp','variant=direct-cold\\n",p,unsigned(WorldCellComplete::directCold));',
     'variant=paint-cells\\n",p,unsigned(PaintCellPrice::enabled));')
physics='game/src/gen/game_physics.gen.cpp'
edit(physics,'#include "world_moving.hpp"',
     '#include "world_moving.hpp"\n#include "debug/paint_cell_price.hpp"')
edit(physics,'part.envTexBag->paintVu = false && stapip.core.supportsPaintVu(part.envBag.get());',
     'part.envTexBag->paintVu = PaintCellPrice::enabled && stapip.core.supportsPaintVu(part.envBag.get());')
writer='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(writer,'#include "debug/world_cell_direct.hpp"',
     '#include "debug/world_cell_direct.hpp"\n#include "debug/paint_cell_price.hpp"')
edit(writer,'  const u32 frontHeadStart=ClipConsumer::observe?packet2_get_qw_count(objectDataPacket):0;',
     '  const u32 paintHeadStart=PaintCellPrice::held?packet2_get_qw_count(objectDataPacket):0;\n  const u32 frontHeadStart=ClipConsumer::observe?packet2_get_qw_count(objectDataPacket):0;')
edit(writer,'        emitUnpack(objectDataPacket, VU1_LIGHTS_DIRS_ADDR, p, 2);',
     '''        const u32 paintStart=PaintCellPrice::held?packet2_get_qw_count(objectDataPacket):0;
        emitUnpack(objectDataPacket, VU1_LIGHTS_DIRS_ADDR, p, 2);
        if(PaintCellPrice::held){
          const u32 added=packet2_get_qw_count(objectDataPacket)-paintStart;
          TYRA_ASSERT(added==3,"A44 paint uniform shape changed");PaintCellPrice::paintWords+=added;
        }''')
edit(writer,'  // Do not terminate the DMA chain here: addBuffersDataToPacket appends the',
     '''  if(PaintCellPrice::held && bag->texture && bag->texture->coordinatesAreNormals &&
     bag->texture->textureFunction==3){
    ++PaintCellPrice::heads;
    PaintCellPrice::headWords+=packet2_get_qw_count(objectDataPacket)-paintHeadStart;
    if(paintVu)++PaintCellPrice::vuHeads;
  }
  // Do not terminate the DMA chain here: addBuffersDataToPacket appends the''')
game='game/src/terrain_game.cpp'
edit(game,'  const bool movingDirect=WorldCellComplete::directCold;',
     '  const bool movingDirect=WorldCellComplete::directCold;\n  const bool movingPaint=PaintCellPrice::enabled;')
edit(game,'  WorldCellComplete::directCold=arm==1?!movingDirect:movingDirect;',
     '  WorldCellComplete::directCold=true;\n  PaintCellPrice::enabled=arm==1?!movingPaint:movingPaint;')
edit(game,'    if(i==31)WorldCellComplete::requested=WorldCellComplete::submitted=WorldCellComplete::fallback=0;',
     '''    if(i==31){
      WorldCellComplete::requested=WorldCellComplete::submitted=WorldCellComplete::fallback=0;
      PaintCellPrice::reset();PaintCellPrice::held=true;
    }''')
edit(game,'    renderScene();engine->renderer.endFrame();core.synchronizeFrame();core.sync.align2D();\n  }\n  for(unsigned kind=0;kind<3;++kind){',
     '    renderScene();engine->renderer.endFrame();core.synchronizeFrame();core.sync.align2D();\n    PaintCellPrice::held=false;\n  }\n  for(unsigned kind=0;kind<3;++kind){')
edit(game,'  fflush(stdout);\n  }\n  WorldCellComplete::directCold=movingDirect;',
     '  PaintCellPrice::witness(MotionProbe::phase,arm);\n  fflush(stdout);\n  }\n  PaintCellPrice::enabled=movingPaint;\n  WorldCellComplete::directCold=movingDirect;')
manifest=json.loads((a.source/'world-cell-price-manifest.json').read_text())
manifest.update(baseElfSha256=binding['elfSha256'],generatorSha256=sha(Path(__file__)),
                variant='paint-cells',comparison='common direct terrain/ordinary roads/Hybrid2: EE paint versus VU paint',
                status='UNQUALIFIED',limits=[
                    'Existing A13/A40 accepted paint basis/readiness and all capability fallbacks retained.',
                    'No triple buffers, ISR copy, reflection freeze or geometry omission.',
                    'Moving activation uses existing paintVu CSV counter; head/+3QW witness only outside price.',
                    'Native/full same-ELF image/physical both-order net/tail gates required.'])
(a.out/'world-cell-price-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED existing paint offload/common direct cells/Hybrid2')
