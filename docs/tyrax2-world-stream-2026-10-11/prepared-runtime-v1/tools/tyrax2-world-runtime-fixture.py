"""Private producer-owned world body runtime prototype; not yet qualified."""
import argparse
import importlib.util
import json
import re
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
assert plan['experiment']=='A30' and not plan['reverse'] and not plan['ringStateOnce']
for platform in ('emulator','physical'):
    r=json.loads((a.source/f'coarse-strip-{platform}.json').read_text())
    assert r['valid'] and r['groupRasterEqual'] and r['coarseSourceDeliveryEqual']
def ignore(path,names):
    return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','ring-shader-pins.json'):shutil.copy2(a.source/rel,a.out/rel)
engine=a.out/'tyra/engine'
for name in ('strip-plan','body','owner','payload','runtime'):
    src=repo/f'tools/tyrax2-world-{name}.hpp'
    dest='world_strip_plan.hpp' if name=='strip-plan' else f'world_{name}.hpp'
    shutil.copy2(src,engine/'inc/debug'/dest)
def edit(path,old,new):
    text=path.read_text();assert text.count(old)==1,(path,old)
    path.write_text(text.replace(old,new))
bag=engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp'
edit(bag,'#include "math/vec4.hpp"','#include "math/vec4.hpp"\n#include "debug/world_runtime.hpp"')
edit(bag,'  bool refineStrips;', '''  bool refineStrips;
  WorldRuntime::Attachment worldOwner;
  u64 worldGeneration=0;
  u32 worldSourceHash=0,worldTriangles=0;''')
hdr=engine/'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp'
edit(hdr,'class StaPipQBufferRenderer {\n public:', '''class StaPipQBufferRenderer {
 public:
  bool prepareWorldGeometry(StaPipBag*,u32);
  bool replayPreparedWorld(StaPipBag*);
  u32 getWorldEpoch()const{return worldEpoch;}
  void collectWorldOwners(){worldPool.collect();}''')
edit(hdr,'  StaPipProgramName lastProgramName;', '''  StaPipProgramName lastProgramName;
  WorldRuntime::Pool worldPool{196608};
  u32 worldEpoch=1,worldLastSequence=0;
  bool worldLastCopied=false;''')
corehdr=engine/'inc/renderer/3d/pipeline/static/core/stapip_core.hpp'
edit(corehdr,'  void setDiagnosticClipRing(bool enabled);', '''  void setDiagnosticClipRing(bool enabled);
  bool prepareWorldGeometry(StaPipBag* b){return b && qbufferRenderer.prepareWorldGeometry(b,getMaxVertCountByBag(b));}
  u32 getWorldEpoch()const{return qbufferRenderer.getWorldEpoch();}
  void collectWorldOwners(){qbufferRenderer.collectWorldOwners();}''')
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(renderer,'#include "debug/clip_consumer.hpp"',
     '#include "debug/clip_consumer.hpp"\n#include "debug/clip_capacity.hpp"')
edit(renderer,'  if(ClipRing::active==enabled)return;',
     '  if(ClipRing::active==enabled)return;\n  if(!++worldEpoch)++worldEpoch;')
edit(renderer,'    packetSequence[context] = sourceCopied ? 0 : sequence;',
     '    packetSequence[context] = sourceCopied ? 0 : sequence;\n    worldLastSequence=sequence;worldLastCopied=sourceCopied;')
text=renderer.read_text()+'\n'+(repo/'tools/tyrax2-world-runtime-methods.inc').read_text()
renderer.write_text(text)
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core,'  bool bakeBag = false;', '''  WorldRuntime::admitted=false;
  const bool worldPlayed=frustumCull && frustumCheck==PARTIALLY_IN_FRUSTUM && bag->info->fullClipChecks &&
     qbufferRenderer.replayPreparedWorld(bag);
  WorldRuntime::ResidualScope worldResidual;
  if(worldPlayed)return;
  bool bakeBag = false;''')
edit(core,'void StaPipCore::onFrameEnd() {','void StaPipCore::onFrameEnd() {\n  qbufferRenderer.collectWorldOwners();')
game=a.out/'game';gamehdr=game/'inc/terrain_game.hpp'
edit(gamehdr,'  Tyra::StaticPipeline stapip;', '''  Tyra::StaticPipeline stapip;
  u32 worldSeenEpoch=0;
  bool worldSeenActive=false;
  void prepareWorldOwners();''')
physics=game/'src/gen/game_physics.gen.cpp'
edit(physics,'  const unsigned active=ClipTrial::mode;', '''  const unsigned active=ClipTrial::mode;
  printf("WORLDWITNESS phase=%u mode=%u frames=%u draws=%u qwords=%u\\n",
    MotionProbe::phase,active,ClipTrial::witnesses[MotionProbe::phase].frames,
    WorldRuntime::trialDraws,WorldRuntime::trialQwords);fflush(stdout);
  WorldRuntime::trialDraws=WorldRuntime::trialQwords=0;''')
edit(physics,'  TerrainFocus::checkpoint=true;',
     '  TerrainFocus::checkpoint=true;WorldRuntime::cold=true;')
edit(physics,'  TerrainFocus::checkpoint=false;',
     '  TerrainFocus::checkpoint=false;WorldRuntime::cold=false;')
edit(physics,'    stapip.core.setDiagnosticClipRing(false);stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=ClipTrial::mode==7;',
     '    stapip.core.setDiagnosticClipRing(false);stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=true;WorldRuntime::active=arm==1;prepareWorldOwners();WorldRuntime::reset();')
edit(physics,'''    TYRA_ASSERT(ClipConsumer::nativeCalls>0,
      "A29 native strip activation mismatch");''', '''    TYRA_ASSERT(WorldRuntime::sources>0 && WorldRuntime::triangles>0 &&
      (arm==1?WorldRuntime::draws==WorldRuntime::sources:WorldRuntime::draws==0),
      "A31 full owner activation mismatch");''')
edit(physics,'''    TYRA_ASSERT(CoarseStrip::sourcePackages>0 &&
      (arm==1?(CoarseStrip::deliveryJobs>0 && CoarseStrip::same(CoarseStrip::source,CoarseStrip::delivered)):
               CoarseStrip::deliveryJobs==0),"A30 source/delivery contract");''',
     '''    TYRA_ASSERT(CoarseStrip::same(CoarseStrip::source,CoarseStrip::delivered),
      "A31 residual coarse source/delivery contract");''')
anchor='    printf("RINGFALLBACK phase=%u arm=%u calls=%u triangles=%u\\n",'
edit(physics,anchor,'''    printf("WORLDOWNER phase=%u arm=%u sources=%u draws=%u triangles=%u hash=%08x\\n",
      MotionProbe::phase,arm,WorldRuntime::sources,WorldRuntime::draws,WorldRuntime::triangles,WorldRuntime::sourceHash);fflush(stdout);
'''+anchor)
# Producer rebuild hooks run only after source arrays and all companion bags bind.
end='\n}\n\n\n\n// Unbuilt chunks in the current view rect'
edit(physics,end,'''\n  stapip.core.prepareWorldGeometry(ch.bag.get());
  stapip.core.prepareWorldGeometry(ch.aoBag.get());
  stapip.core.prepareWorldGeometry(ch.emisBag.get());
  stapip.core.prepareWorldGeometry(ch.gsBag.get());
  for(auto& layer:ch.layerPasses)stapip.core.prepareWorldGeometry(layer.bag.get());
}\n\n\n\n// Unbuilt chunks in the current view rect''')
collision=game/'src/gen/game_collision.gen.cpp'
anchor='      c.centre[a] = 0.5F * (c.aabbMin[a] + c.aabbMax[a]);\n  }\n}'
edit(collision,anchor,'''      c.centre[a] = 0.5F * (c.aabbMin[a] + c.aabbMax[a]);
    stapip.core.prepareWorldGeometry(c.bag.get());
  }
}''')
main=game/'src/terrain_game.cpp'
edit(main,'''  if(CoarseStrip::active!=(ClipTrial::mode==7))stapip.core.setDiagnosticClipRing(false);
  stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=ClipTrial::mode==7;''',
     '''  if(!CoarseStrip::active)stapip.core.setDiagnosticClipRing(false);
  stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=true;
  WorldRuntime::active=ClipTrial::mode==7;prepareWorldOwners();''')
namespace=re.search(r'^namespace (\w+) \{',gamehdr.read_text(),re.M).group(1)
text=main.read_text()+'\nnamespace '+namespace+' {\nusing namespace Tyra;\n'+'''
void TerrainGame::prepareWorldOwners(){
  const u32 epoch=stapip.core.getWorldEpoch();
  if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active)return;
  auto prepare=[&](StaPipBag* b){
    if(!b)return;
    if(WorldRuntime::active || WorldRuntime::cold)stapip.core.prepareWorldGeometry(b);
    else b->worldOwner.reset();
  };
  for(auto& c:procChunks)prepare(c.bag.get());
  for(auto& ch:terrainChunks){
    prepare(ch.bag.get());prepare(ch.aoBag.get());prepare(ch.emisBag.get());prepare(ch.gsBag.get());
    for(auto& layer:ch.layerPasses)prepare(layer.bag.get());
  }
  worldSeenEpoch=epoch;
  worldSeenActive=WorldRuntime::active;
  stapip.core.collectWorldOwners();
}
'''+ '\n}\n'
main.write_text(text)
plan.update(experiment='A31-owner',worldProducer=True,worldHotCandidate=True,
            hotBaselineCoarse=True,worldBodyBudgetQwords=196608,
            worldMovingWitness=True,worldCompiledPayload=True,
            worldNetProducerCost=True,
            preparationSha256=q.sha(core),
            baseElfSha256=binding['elfSha256'],status='UNQUALIFIED runtime owner; no physical price')
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
print(a.out,'UNQUALIFIED producer-owned runtime')
