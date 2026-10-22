"""A32 private first frontend integration, combined A31 + owned validation/capacity/bounds."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('q',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert q.sha(a.source/rel)==h,rel
plan=json.loads((a.source/'clip-capacity-manifest.json').read_text())
assert plan['experiment']=='A31-owner' and plan['worldNetProducerCost']
for platform in ('emulator','physical'):
    r=json.loads((a.source/f'world-runtime-{platform}.json').read_text())
    assert r['valid'] and r['performanceCandidate'] and r['groupRasterEqual']
def ignore(path,names):
    return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','ring-shader-pins.json'):shutil.copy2(a.source/rel,a.out/rel)
engine=a.out/'tyra/engine';game=a.out/'game'
for src,dest in (('tyrax2-prepared-front.hpp','prepared_front.hpp'),
                 ('tyrax2-prepared-runtime.hpp','prepared_runtime.hpp')):
    shutil.copy2(repo/'tools'/src,engine/'inc/debug'/dest)
def edit(path,old,new):
    text=path.read_text();assert text.count(old)==1,(path,old);path.write_text(text.replace(old,new))
def append_function(path,name,addition):
    text=path.read_text();start=text.index('void TerrainGame::'+name+'(')
    begin=text.index('{',start);level=1;at=begin+1
    # These append sites use a verified following function boundary, not a C++ lexer.
    # Locate only the next column-zero close brace of this generated function.
    end=text.index('\n}',at)
    text=text[:end]+addition+text[end:];path.write_text(text)
bag=engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp'
edit(bag,'#include "debug/world_runtime.hpp"','#include "debug/world_runtime.hpp"\n#include "debug/prepared_runtime.hpp"')
edit(bag,'  u64 worldGeneration=0;', '  u64 worldGeneration=0;\n  FrontRuntime::Attachment frontOwner;\n  u64 frontGeneration=0;')
hdr=engine/'inc/renderer/3d/pipeline/static/core/stapip_core.hpp'
edit(hdr,'  bool prepareWorldGeometry(StaPipBag* b){return b && qbufferRenderer.prepareWorldGeometry(b,getMaxVertCountByBag(b));}',
     '  bool prepareFrontGeometry(StaPipBag*);\n  bool prepareWorldGeometry(StaPipBag* b){prepareFrontGeometry(b);return b && qbufferRenderer.prepareWorldGeometry(b,getMaxVertCountByBag(b));}')
edit(hdr,' private:', ' private:\n  StaPipBagPackagesBBox* admitFrontGeometry(StaPipBag*);')
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core,'#include "debug/clip_consumer.hpp"','#include "debug/clip_consumer.hpp"\n#include "debug/clip_capacity.hpp"')
edit(core,'  // Modified by TyraX: GS hardware fog - the PRIM FGE bit follows the',
     '  auto* preparedFront=admitFrontGeometry(bag);\n  // Modified by TyraX: GS hardware fog - the PRIM FGE bit follows the')
edit(core,'  TYRA_ASSERT(bag->vertices != nullptr,','  if(!preparedFront){\n  TYRA_ASSERT(bag->vertices != nullptr,')
anchor='  HardwareTrace::Scope traceBounds("Bounds", HardwareTrace::Kind::Span, true);'
edit(core,anchor,'  }\n'+anchor)
edit(core,'  u32 maxVertCount = getMaxVertCountByBag(bag);',
     '  u32 maxVertCount = preparedFront?bag->frontOwner.value->key().capacity:getMaxVertCountByBag(bag);')
edit(core,'    bbox = cacher.getBBoxes(bag->vertices, bag->count,',
     '    bbox = preparedFront?preparedFront:cacher.getBBoxes(bag->vertices, bag->count,')
core.write_text(core.read_text()+'\n'+(repo/'tools/tyrax2-prepared-runtime-methods.inc').read_text())
main=game/'src/terrain_game.cpp'
edit(main,'  WorldRuntime::active=ClipTrial::mode==7;prepareWorldOwners();',
     '  WorldRuntime::active=ClipTrial::mode==7;FrontRuntime::active=WorldRuntime::active;prepareWorldOwners();')
edit(main,'    else b->worldOwner.reset();','    else {b->worldOwner.reset();b->frontOwner.value.reset();}')
edit(main,'  worldSeenEpoch=epoch;', '''  auto front=[&](StaPipBag* b){if(!b)return;
    if(FrontRuntime::active || WorldRuntime::cold)stapip.core.prepareFrontGeometry(b);
    else b->frontOwner.value.reset();
  };
  for(auto& b:staticBatches)front(b.bag.get());
  for(auto& g:objectGeometry)if(!g.matrixMode)for(auto& p:g.parts){
    front(p.bag.get());front(p.aoBag.get());front(p.emisBag.get());
  }
  worldSeenEpoch=epoch;''')
physics=game/'src/gen/game_physics.gen.cpp'
edit(physics,'  const unsigned active=ClipTrial::mode;', '''  const unsigned active=ClipTrial::mode;
  printf("FRONTWITNESS phase=%u mode=%u frames=%u draws=%u parts=%u\\n",
    MotionProbe::phase,active,ClipTrial::witnesses[MotionProbe::phase].frames,
    FrontRuntime::trialDraws,FrontRuntime::trialParts);fflush(stdout);
  FrontRuntime::trialDraws=FrontRuntime::trialParts=0;''')
edit(physics,'CoarseStrip::active=true;WorldRuntime::active=arm==1;prepareWorldOwners();WorldRuntime::reset();',
     'CoarseStrip::active=true;WorldRuntime::active=arm==1;FrontRuntime::active=arm==1;prepareWorldOwners();WorldRuntime::reset();FrontRuntime::reset();')
edit(physics,'    printf("WORLDOWNER phase=%u arm=%u sources=%u draws=%u triangles=%u hash=%08x\\n",',
     '''    printf("FRONTCONTRACT phase=%u arm=%u sources=%u draws=%u parts=%u hash=%08x live=%u peak=%u owners=%u\\n",
      MotionProbe::phase,arm,FrontRuntime::sources,FrontRuntime::draws,FrontRuntime::parts,
      FrontRuntime::sourceHash,FrontRuntime::liveParts,FrontRuntime::peakParts,FrontRuntime::liveOwners);fflush(stdout);
    TYRA_ASSERT(FrontRuntime::sources>0 && FrontRuntime::parts>0 &&
      (arm==1?FrontRuntime::draws==FrontRuntime::sources:FrontRuntime::draws==0),"A32 frontend activation");
    printf("WORLDOWNER phase=%u arm=%u sources=%u draws=%u triangles=%u hash=%08x\\n",''')
collision=game/'src/gen/game_collision.gen.cpp'
append_function(collision,'rebuildObjectGeometry','''
  if(!g.matrixMode)for(auto& p:g.parts){
    stapip.core.prepareFrontGeometry(p.bag.get());stapip.core.prepareFrontGeometry(p.aoBag.get());
    stapip.core.prepareFrontGeometry(p.emisBag.get());
  }''')
append_function(collision,'rebuildStaticBatch','\n  stapip.core.prepareFrontGeometry(b.bag.get());')
append_function(collision,'applyGeoLod','''
  if(!g.matrixMode){stapip.core.prepareFrontGeometry(part.bag.get());
    stapip.core.prepareFrontGeometry(part.aoBag.get());stapip.core.prepareFrontGeometry(part.emisBag.get());}''')
plan.update(experiment='A32-front',preparedFrontend=True,preparedFrontendStage='owned-validation-capacity-bounds',
    combinedWorldOwner=True,frontNetProducerCost=True,frontPartBudget=8192,
    preparationSha256=q.sha(core),baseElfSha256=binding['elfSha256'],
    status='UNQUALIFIED first frontend integration; no hardware price')
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
print(a.out,'UNQUALIFIED combined A31 + A32 stage1')
