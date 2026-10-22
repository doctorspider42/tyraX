"""Private partial-road producer frontend/owned geometry; preserve ordinary inside replay."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==binding['elfSha256']
sdk=json.loads((a.source/'road-package-sdk-qualified.json').read_text())
gate=json.loads((a.source/'world-cell-price-emulator.json').read_text())
assert sdk['valid'] and gate['valid'] and sdk['elfSha256']==gate['elfSha256']==binding['elfSha256']
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
repo=Path(__file__).resolve().parent
for name,dest in [('tyrax2-road-package-plan.hpp','road_package_plan.hpp'),
                  ('tyrax2-road-prepared.hpp','road_prepared.hpp')]:
    shutil.copy2(repo/name,a.out/'tyra/engine/inc/debug'/dest)
def edit(rel,old,new):
    f=a.out/rel;t=f.read_text();assert t.count(old)==1,(rel,old);f.write_text(t.replace(old,new))
bag='tyra/engine/inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp'
edit(bag,'namespace Tyra {','namespace RoadPrepared {struct Owner;}\nnamespace Tyra {')
edit(bag,'  StaPipBag();','  std::shared_ptr<RoadPrepared::Owner> roadPrepared;\n  StaPipBag();')
buffer='tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_qbuffer.hpp'
edit(buffer,'namespace Tyra {','namespace RoadPrepared {struct Owner;struct Fragment;}\nnamespace Tyra {')
edit(buffer,'  StaPipQBuffer();','  RoadPrepared::Owner* roadOwner=nullptr;\n  RoadPrepared::Fragment* roadFragment=nullptr;\n  StaPipQBuffer();')
renderer='tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp'
edit(renderer,'#include "debug/world_cell_complete.hpp"',
     '#include "debug/world_cell_complete.hpp"\n#include "debug/road_prepared.hpp"')
edit(renderer,'  bool prepareWorldGeometry(StaPipBag*,u32);',
     '''  bool compileRoadPackages(StaPipBag*,RoadPrepared::Owner&,u32);
  bool roadPackagesReady(StaPipBag*,const RoadPrepared::Owner&);
  void emitRoadPackage(packet2_t*,StaPipQBuffer*,StaPipVU1Program*,bool);
  std::vector<RoadPrepared::Owner*> roadPacketOwners;
  bool prepareWorldGeometry(StaPipBag*,u32);''')
core='tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_core.hpp'
edit(core,'  bool prepareWorldTicket(StaPipBag* b);',
     '  bool prepareRoadPackages(StaPipBag*);\n  bool renderRoadPackages(StaPipBag*,StaPipLightReception);\n  bool prepareWorldTicket(StaPipBag* b);')
writer='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(writer,'  result->retainIndex = -1;',
     '  result->roadOwner=nullptr;result->roadFragment=nullptr;\n  result->retainIndex = -1;')
edit(writer,'    const bool emitState = lastProgramName != program->getName();',
     '''    const bool emitState = lastProgramName != program->getName();
    if(buffers[i]->roadFragment){
      emitRoadPackage(currentPacket,buffers[i],program,emitState);continue;
    }''')
edit(writer,'  // Modified by TyraX: the packet\'s wrap write is on its way (setBagWrap).',
     '''  for(auto* owner:roadPacketOwners)
    owner->geometry->submitted(owner->key,worldLastSequence,worldLastCopied);
  roadPacketOwners.clear();
  // Modified by TyraX: the packet's wrap write is on its way (setBagWrap).''')
f=a.out/writer;f.write_text(f.read_text()+'\n'+(repo/'tyrax2-road-prepared-renderer.inc').read_text())
core_src='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core_src,'  if(renderWorldTicket(bag,reception))return;',
     '  if(renderRoadPackages(bag,reception))return;\n  if(renderWorldTicket(bag,reception))return;')
f=a.out/core_src;f.write_text(f.read_text()+'\n'+(repo/'tyrax2-road-prepared-core.inc').read_text())
producer='game/src/gen/game_collision.gen.cpp'
edit(producer,'    stapip.core.prepareWorldGeometry(c.bag.get());',
     '''    if(c.owner==-3 && c.bag->stripped){
      if(!stapip.core.prepareRoadPackages(c.bag.get()))stapip.core.prepareWorldGeometry(c.bag.get());
    }else stapip.core.prepareWorldGeometry(c.bag.get());''')
moving='game/inc/world_moving.hpp'
edit(moving,'#include "debug/paint_cell_price.hpp"',
     '#include "debug/paint_cell_price.hpp"\n#include "debug/road_prepared.hpp"')
edit(moving,'PaintCellPrice::enabled=p<6 && modes[p];',
     'PaintCellPrice::enabled=true;RoadPrepared::active=p<6 && modes[p];')
edit(moving,'  phase=p;frames=gasFrames=',
     '  RoadPrepared::reset();\n  phase=p;frames=gasFrames=')
edit(moving,'variant=paint-cells\\n",p,unsigned(PaintCellPrice::enabled));',
     'variant=road-packages\\n",p,unsigned(RoadPrepared::active));')
edit(moving,'inline void finish(){','inline void finish(){\n  RoadPrepared::witness(phase);')
game='game/src/terrain_game.cpp'
edit(game,'  WorldMoving::begin(MotionProbe::phase);',
     '  WorldMoving::begin(MotionProbe::phase);\n  RoadPrepared::audit=MotionProbe::phase<6 && MotionProbe::offset==1100;')
edit(game,'  const bool movingPaint=PaintCellPrice::enabled;',
     '  const bool movingPaint=PaintCellPrice::enabled;\n  const bool movingRoad=RoadPrepared::active;')
edit(game,'  PaintCellPrice::enabled=arm==1?!movingPaint:movingPaint;',
     '  PaintCellPrice::enabled=true;RoadPrepared::active=arm==1?!movingRoad:movingRoad;')
edit(game,'      PaintCellPrice::reset();PaintCellPrice::held=true;',
     '      PaintCellPrice::reset();PaintCellPrice::held=true;RoadPrepared::reset();')
edit(game,'  PaintCellPrice::witness(MotionProbe::phase,arm);',
     '  PaintCellPrice::witness(MotionProbe::phase,arm);RoadPrepared::witness(MotionProbe::phase,int(arm));')
edit(game,'  PaintCellPrice::enabled=movingPaint;',
     '  PaintCellPrice::enabled=movingPaint;RoadPrepared::active=movingRoad;')
manifest=json.loads((a.source/'world-cell-price-manifest.json').read_text())
manifest.update(baseElfSha256=binding['elfSha256'],generatorSha256=sha(Path(__file__)),
                variant='road-packages',status='UNQUALIFIED',
                comparison='common direct+paint/Hybrid2: ordinary versus producer partial-road packages',
                limits=['New partial-road frontend and owned SDK geometry; inside bags retain ordinary O(1) replay.',
                        'Common road ownership prepared in BOTH roles; old unused A31 road preparation skipped ONLY when ready.',
                        'Live per-bag texture/material/light/fog heads and original qbuffer half/full flush cadence retained.',
                        'Eligible stripped roads only; lists and unsupported/unready bags retain ordinary path.',
                        'Actual native/GPU routing, lifetime, images and PS2 both-order net/tails required.'])
(a.out/'world-cell-price-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED partial-road frontend/queue integration')
