"""A35 early prepared WORLD frontend, original SDK heads retained."""
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
assert plan['experiment']=='A31-owner' and plan['worldNetProducerCost']
for target in ('emulator','physical'):
 r=json.loads((a.source/f'world-runtime-{target}.json').read_text())
 assert r['valid'] and r['groupRasterEqual'] and r['worldSourceDeliveryEqual']
def ignore(path,names):
 return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','ring-shader-pins.json'):shutil.copy2(a.source/rel,a.out/rel)
def edit(path,old,new):
 text=path.read_text();assert text.count(old)==1,(path,old);path.write_text(text.replace(old,new))
engine=a.out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-world-front.hpp',engine/'inc/debug/world_front.hpp')
bag=engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp'
edit(bag,'#include "debug/world_runtime.hpp"','#include "debug/world_runtime.hpp"\n#include "debug/world_front.hpp"')
edit(bag,'  u64 worldGeneration=0;','  u64 worldGeneration=0;\n  WorldFront::Attachment worldTicket;')
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core,'void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {',
 '''void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {
  if(renderWorldTicket(bag,reception))return;''')
core.write_text(core.read_text()+'\n'+(repo/'tools/tyrax2-world-front-methods.inc').read_text())
core.write_text(core.read_text()+'\n'+(repo/'tools/tyrax2-world-front-admission.inc').read_text())
header=engine/'inc/renderer/3d/pipeline/static/core/stapip_core.hpp'
edit(header,'  bool prepareWorldGeometry(StaPipBag* b){return b && qbufferRenderer.prepareWorldGeometry(b,getMaxVertCountByBag(b));}',
 '''  bool prepareWorldTicket(StaPipBag* b);
  unsigned verifyWorldTicketAdmission(StaPipBag* b);
  bool prepareWorldGeometry(StaPipBag* b){
    if(!b)return false;
    const bool body=qbufferRenderer.prepareWorldGeometry(b,getMaxVertCountByBag(b));
    prepareWorldTicket(b);return body;
  }''')
edit(header,'  void setMaxVertCount(const u32& count);',
     '  bool renderWorldTicket(StaPipBag*,StaPipLightReception);\n  void setMaxVertCount(const u32& count);')
rh=engine/'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp'
edit(rh,'  bool replayPreparedWorld(StaPipBag*);',
 '''  bool replayPreparedWorld(StaPipBag*);
  bool worldBodyReady(StaPipBag* bag);
  bool prepareWorldCullBody(StaPipBag* bag,u32 capacity);
  bool worldCullReady();''')
edit(rh,'  bool worldCullReady();','  bool worldCullReady();\n  u32 diagnosticObjectHeadQwords()const{return packets?packet2_get_qw_count(packets[context]):0;}')
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
anchor='  if (submissionBatchCandidate || kInlineUniforms) {\n    emitUnpack(objectDataPacket, VU1_MVP_MATRIX_ADDR, mvp->data, 4);'
edit(renderer,anchor,'  const u32 frontHeadStart=ClipConsumer::observe?packet2_get_qw_count(objectDataPacket):0;\n'+anchor)
anchor='  // Do not terminate the DMA chain here: addBuffersDataToPacket appends the'
edit(renderer,anchor,'''  if(ClipConsumer::observe){
    const u32 n=packet2_get_qw_count(objectDataPacket)-frontHeadStart;
    TYRA_ASSERT(n>0 && n<=96,"A35 invalid actual head size");
    ++WorldFront::headChecks;WorldFront::headWords+=n;
    const u32* words=reinterpret_cast<const u32*>(objectDataPacket->base+frontHeadStart);
    for(u32 i=0;i<n*4;++i){WorldFront::headHash^=words[i];WorldFront::headHash*=16777619U;}
  }
'''+anchor)
renderer.write_text(renderer.read_text()+'''
namespace Tyra {
bool StaPipQBufferRenderer::worldBodyReady(StaPipBag* bag){
  if(!WorldRuntime::active || !bag->worldOwner || !worldEligible(bag) ||
      !ClipRing::active || !vu1Clipping || hasProgramOverrides())return false;
  const auto* program=getClipProgramByBag(bag);
  return bag->worldOwner->admit(worldKey(bag,worldEpoch,program->getDestinationAddress(),*prim))!=nullptr;
}
bool StaPipQBufferRenderer::prepareWorldCullBody(StaPipBag* bag,u32 capacity){
  const bool ready=beginBakedBag(bag,capacity,true);endBakedBag();return ready;
}
bool StaPipQBufferRenderer::worldCullReady(){
  if(!bakedCurrent || !bakedCurrent->complete || !bakedCurrent->arenaQw ||
      bakedCurrent->arenaQw>65535)return false;
#if TYRA_STAPIP_BAKED_ANY_VERIFY
  if(baked.verifying(bakedCurrent))return false;
#endif
  return true;
}
}
''')
main=a.out/'game/src/terrain_game.cpp'
edit(main,'WorldRuntime::active=ClipTrial::mode==7;prepareWorldOwners();',
 'WorldRuntime::active=true;WorldFront::active=ClipTrial::mode==7;prepareWorldOwners();')
edit(main,'  if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active)return;',
 '''  static bool frontSeenActive=false;
  if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active && frontSeenActive==WorldFront::active)return;''')
edit(main,'  worldSeenEpoch=epoch;',
 '''  if(!WorldFront::active)WorldFront::Owner::releaseAll();
  frontSeenActive=WorldFront::active;
  worldSeenEpoch=epoch;''')
physics=a.out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'WorldRuntime::active=arm==1;prepareWorldOwners();WorldRuntime::reset();',
 'WorldRuntime::active=arm==1;WorldFront::active=arm==1;prepareWorldOwners();WorldRuntime::reset();')
anchor='    for(unsigned frame=0;frame<32;++frame){'
edit(physics,anchor,'''    if(arm==1){
      unsigned tested=0;
      for(auto& ch:terrainChunks){if((tested=stapip.core.verifyWorldTicketAdmission(ch.bag.get())))break;}
      if(!tested)for(auto& ch:procChunks){if((tested=stapip.core.verifyWorldTicketAdmission(ch.bag.get())))break;}
      TYRA_ASSERT(tested>=39,"A35 no fresh target admission ticket");
      printf("FRONTADMISSION phase=%u checks=%u headUnchanged=1\\n",MotionProbe::phase,tested);fflush(stdout);
    }
'''+anchor)
edit(physics,'CoarseStrip::reset();ClipConsumer::observe=frame==31;',
 'CoarseStrip::reset();WorldFront::resetOracle();ClipConsumer::observe=frame==31;')
anchor='    printf("WORLDOWNER phase=%u arm=%u sources=%u draws=%u triangles=%u hash=%08x\\n",'
edit(physics,anchor,'''    printf("FRONTORACLE phase=%u arm=%u inside=%u partial=%u outside=%u heads=%u words=%u hash=%08x\\n",
      MotionProbe::phase,arm,WorldFront::coldInside,WorldFront::coldPartial,WorldFront::coldOutside,
      WorldFront::headChecks,WorldFront::headWords,WorldFront::headHash);fflush(stdout);
'''+anchor)
anchor='  printf("WORLDWITNESS phase=%u mode=%u frames=%u draws=%u qwords=%u\\n",'
edit(physics,anchor,'''  printf("FRONTWITNESS phase=%u mode=%u frames=%u inside=%u partial=%u outside=%u live=%u peak=%u\\n",
    MotionProbe::phase,active,ClipTrial::witnesses[MotionProbe::phase].frames,
    WorldFront::movingInside,WorldFront::movingPartial,WorldFront::movingOutside,WorldFront::live,WorldFront::peak);fflush(stdout);
  WorldFront::movingInside=WorldFront::movingPartial=WorldFront::movingOutside=0;
'''+anchor)
plan.update(experiment='A35-world-front',performanceCandidate=False,worldFront=True,
 worldFrontStage='early-Core-unqualified',commonWorldHot=True,originalHeads=True,
 worldFrontOwnerLimit=1024,worldFrontWholeHeadOracle=True,worldFrontTargetAdmission=True,baseElfSha256=binding['elfSha256'],
 preparationSha256=q.sha(core),worldFrontRendererSha256=q.sha(renderer))
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
(a.out/'README.txt').write_text('A35 whole early-Core world-strip frontend; original heads; UNQUALIFIED, no price.\n')
print('Created UNQUALIFIED early world frontend',a.out)
