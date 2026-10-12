"""A34 direct semantic head preparation, common A31 owner in hot A/B arms."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();assert not a.out.exists();repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('q',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert q.sha(a.source/rel)==h,rel
plan=json.loads((a.source/'clip-capacity-manifest.json').read_text())
assert plan['experiment']=='A33-head' and not plan['performanceCandidate']
for target in ('emulator','physical'):
    r=json.loads((a.source/f'object-head-{target}.json').read_text())
    assert r['valid'] and not r['performanceCandidate'] and r['groupRasterEqual']
def ignore(path,names):
    return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','ring-shader-pins.json'):shutil.copy2(a.source/rel,a.out/rel)
def edit(path,old,new):
    text=path.read_text();assert text.count(old)==1,(path,old);path.write_text(text.replace(old,new))
engine=a.out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-object-head.hpp',engine/'inc/debug/object_head.hpp')
shutil.copy2(repo/'tools/tyrax2-head-hot.hpp',engine/'inc/debug/head_hot.hpp')
bag=engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp'
edit(bag,'#include "debug/world_runtime.hpp"','#include "debug/world_runtime.hpp"\n#include "debug/head_hot.hpp"')
edit(bag,'  u64 worldGeneration=0;','  u64 worldGeneration=0;\n  HeadHot::Attachment headOwner;')
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
text=renderer.read_text();old=(repo/'tools/tyrax2-object-head-runtime.inc').read_text()
assert text.endswith(old);renderer.write_text(text[:-len(old)])
anchor='  const u32 headStart=ClipConsumer::observe?packet2_get_qw_count(objectDataPacket):0;'
edit(renderer,anchor,'''  if(HeadHot::active && !ClipConsumer::observe){
    u32 count=0;
    const u32 used=packet2_get_qw_count(objectDataPacket);
    if(used<=objectDataPacket->max_qwords_count && prepareObjectHead(bag,mvp,texBuffers,
        bagMayClip,objectDataPacket->next,objectDataPacket->max_qwords_count-used,count)){
      objectDataPacket->next+=count;bagMayClip=true;objectDataPending=true;
      if(ClipTrial::observe){++HeadHot::trialDraws;HeadHot::trialWords+=count;}
      return;
    }
  }
'''+anchor)
edit(renderer,'if(ClipConsumer::observe)testObjectHead(bag,mvp,objectDataPacket,headStart,spotActive,mayClip,singleColorEnabled);',
     'if(ClipConsumer::observe)testPreparedObjectHead(bag,mvp,texBuffers,objectDataPacket,headStart,mayClip);')
renderer.write_text(renderer.read_text()+'\n'+(repo/'tools/tyrax2-head-hot-methods.inc').read_text())
header=engine/'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp'
edit(header,'  void testObjectHead(StaPipBag*,M4x4*,packet2_t*,u32,bool,bool,bool);',
     '''  bool installObjectHead(StaPipBag*,ObjectHead::Facts&,unsigned,const ObjectHead::ClipReference&);
  bool prepareObjectHead(StaPipBag*,M4x4*,RendererCoreTextureBuffers*,bool,void*,u32,u32&);
  void testPreparedObjectHead(StaPipBag*,M4x4*,RendererCoreTextureBuffers*,packet2_t*,u32,bool);''')
main=a.out/'game/src/terrain_game.cpp'
edit(main,'WorldRuntime::active=ClipTrial::mode==7;prepareWorldOwners();',
     'WorldRuntime::active=true;HeadHot::active=ClipTrial::mode==7;prepareWorldOwners();')
edit(main,'  if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active)return;',
     '  static bool headSeenActive=false;\n  if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active && headSeenActive==HeadHot::active)return;')
edit(main,'  worldSeenEpoch=epoch;', '''  if(!HeadHot::active)HeadHot::Owner::releaseAll();
  headSeenActive=HeadHot::active;
  worldSeenEpoch=epoch;''')
physics=a.out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'WorldRuntime::active=arm==1;prepareWorldOwners();WorldRuntime::reset();',
     'WorldRuntime::active=arm==1;HeadHot::active=arm==1;prepareWorldOwners();WorldRuntime::reset();')
anchor='  printf("WORLDWITNESS phase=%u mode=%u frames=%u draws=%u qwords=%u\\n",'
edit(physics,anchor,'''  printf("HEADWITNESS phase=%u mode=%u frames=%u draws=%u qwords=%u compiles=%u live=%u peak=%u\\n",
    MotionProbe::phase,active,ClipTrial::witnesses[MotionProbe::phase].frames,
    HeadHot::trialDraws,HeadHot::trialWords,HeadHot::trialCompiles,HeadHot::liveWords,HeadHot::peakWords);fflush(stdout);
  HeadHot::trialDraws=HeadHot::trialWords=HeadHot::trialCompiles=0;
'''+anchor)
plan.update(experiment='A34-head-hot',objectHeadStage='direct-semantic-hot-owner',
    performanceCandidate=False,headHot=True,commonWorldHot=True,headNetCompileCost=True,
    headWordBudget=32768,baseElfSha256=binding['elfSha256'],objectHeadRendererSha256=q.sha(renderer))
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
(a.out/'README.txt').write_text('A34 hot semantic head writer; common hot A31 body owner, private pending all acceptance gates.\n')
print('Created direct semantic hot-head fixture',a.out)
