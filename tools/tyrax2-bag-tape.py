"""A18 current-scene descriptor replay qualification. No timings yet."""
import argparse,hashlib,importlib.util,json,shutil
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path);p.add_argument('source',type=Path)
p.add_argument('--current-v7',action='store_true',help='A36 current baseline capture, ordinary frames only; no replay or pricing')
a=p.parse_args();out=a.out.resolve();source=a.source.resolve();assert not out.exists()
binding=json.loads((source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert hashlib.sha256((source/rel).read_bytes()).hexdigest()==digest,rel
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
for part in ('game','tyra/engine'):shutil.copytree(source/part,out/part,ignore=q.ignore)
shutil.copy2(source/'tyra/Makefile.base',out/'tyra/Makefile.base')
shutil.copy2(source/'authored-manifest.json',out/'authored-manifest.json')
engine=out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-bag-tape.hpp',engine/'inc/debug/bag_tape.hpp')
def edit(path,old,new):
    text=path.read_text(encoding='utf-8-sig')
    if a.current_v7 and old=='namespace Tyra {':
        assert old in text
        text=text.replace(old,new,1)
    else:text=q.once(text,old,new)
    path.write_text(text,encoding='utf-8')
# Keep the A16 target gate unconditionally disabled, with counters linked.
if not a.current_v7:
    edit(engine/'inc/debug/target_audit.hpp','return MotionProbe::variant()==6;','return false;')
    edit(engine/'inc/debug/target_audit.hpp','inline unsigned tested=0,rejected=0;','')
    edit(engine/'inc/debug/motion_probe.hpp','namespace TargetAudit { extern unsigned tested,rejected; }','namespace TargetAudit { inline unsigned tested=0,rejected=0; }')
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core,'namespace Tyra {','#include "debug/bag_tape.hpp"\nnamespace Tyra {')
edit(core,'void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {',
     'void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {\n  BagTape::record(this,bag,reception);')
buffer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(buffer,'namespace Tyra {','#include "debug/bag_tape.hpp"\nnamespace Tyra {')
for method,kind in [('beginSubmissionBatch','true'),('endSubmissionBatch','false')]:
    edit(buffer,f'void StaPipQBufferRenderer::{method}() {{',f'void StaPipQBufferRenderer::{method}() {{\n  BagTape::batch(this,{kind});')
env=engine/'src/renderer/core/envmap/renderer_core_envmap.cpp'
edit(env,'namespace Tyra {','#include "debug/bag_tape.hpp"\nnamespace Tyra {')
edit(env,'void RendererCoreEnvMap::begin(const Color& clearColor) {','void RendererCoreEnvMap::begin(const Color& clearColor) {\n  BagTape::env(this,true,clearColor);')
edit(env,'void RendererCoreEnvMap::end() {','void RendererCoreEnvMap::end() {\n  BagTape::env(this,false);')
game=out/'game/src/terrain_game.cpp'
edit(game,'void TerrainGame::loop() {','''void TerrainGame::loop() {
  if(BagTape::frozen){renderBagTapeTrial();return;}
  if(MotionProbe::phase==0 && MotionProbe::offset==1380){
    BagTape::interleaved=heavyActive;
    FILE* cfg=fopen("host:bag-tape-control.cfg","rb");
    if(cfg){BagTape::control=true;fclose(cfg);}
    BagTape::frozen=true;g_frameDt=0;engine->renderer.core.synchronizeFrame();
    printf("TAPEFREEZE phase=0 offset=1380 z=%f speed=%f\\n",MotionProbe::current.z,MotionProbe::current.speed);
    renderBagTapeTrial();return;
  }''')
game.write_text('#include "debug/bag_tape.hpp"\n'+game.read_text(encoding='utf-8-sig'),encoding='utf-8')
h=out/'game/inc/terrain_game.hpp';edit(h,'  void renderScene();','  void renderScene();\n  void renderBagTapeTrial();')
physics=out/'game/src/gen/game_physics.gen.cpp'
physics.write_text('#include "debug/bag_tape.hpp"\n'+physics.read_text(encoding='utf-8-sig'),encoding='utf-8')
vehicles=out/'game/src/gen/game_vehicles.gen.cpp'
vehicles.write_text('#include "debug/bag_tape.hpp"\n'+vehicles.read_text(encoding='utf-8-sig'),encoding='utf-8')
if a.current_v7:
    edit(vehicles,'  heavyDrawn = 0;', '  heavyDrawn = 0;\n  if (BagTape::frozen) return BagTape::interleaved;')
else:
    edit(vehicles,'  heavyDrawn = 0;\n  if (INTERLEAVE_PASSES == 0 || splitPassActive) {',
         '  heavyDrawn = 0;\n  if (BagTape::frozen) return BagTape::interleaved;\n  if (INTERLEAVE_PASSES == 0 || splitPassActive) {')
edit(physics,'  if (sharedEnvGeneration != sceneGeneration) {',
     '  if (BagTape::frozen) sharedEnvBasisValid=false;\n  if (sharedEnvGeneration != sceneGeneration) {')
# Insert inside the existing namespace, immediately before renderScene.
edit(physics,'void TerrainGame::renderScene() {', '''void TerrainGame::renderBagTapeTrial() {
  using namespace BagTape;
  if(iteration>=96){SleepThread();return;}
  auto& core=engine->renderer.core;renderer=&core;
  beamBatchCall=0;g_frameDt=0;
  const unsigned stage=iteration/24, local=iteration%24;
  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));
  engine->renderer.renderer3D.usePipeline(stapip);
  recording=iteration==23;
  checking=local==23 && (stage==1 || stage==3);
  checkIndex=checkMismatch=checkMask=0;firstMismatch=~0u;
  if(stage==2 && !control) replay();else renderScene();
  recording=false;
  checking=false;
  engine->renderer.endFrame();core.synchronizeFrame();
  if(local==23){
    printf("TAPECHECK stage=%u mismatches=%u mask=%u first=%u\\n",stage,checkMismatch,checkMask,firstMismatch);
    captureImage(core,stage,false);captureImage(core,stage,true);
    printf("TAPESTAGE stage=%u bags=%u events=%u bytes=%u replay=%u\\n",stage,calls,unsigned(events.size()),bytes,unsigned(stage==2 && !control));fflush(stdout);
  }
  ++iteration;
  if(iteration==96){printf("TAPEDONE stages=4 qualificationOnly=1\\n");fflush(stdout);}
}

void TerrainGame::renderScene() {''')
(out/'bag-tape-manifest.json').write_text(json.dumps(dict(experiment='A18 descriptor qualification',
    source=str(source),stages=['capture ordinary','ordinary control','descriptor replay','ordinary return'],
    frame='day drive offset1380; frozen update; capture forced in all ordinary arms',
    scope='Scene only; excludes HUD/postFX; no timings or packet replay yet'),indent=2)+'\n')
if a.current_v7:
    for name in ('command-tape','command-size'):
        shutil.copy2(repo/f'tools/tyrax2-{name}.hpp',engine/f'inc/debug/{name.replace("-","_")}.hpp')
    queue=engine/'src/renderer/core/paths/path1/vif1_queue.cpp'
    edit(queue,'namespace Tyra {','#include "debug/command_tape.hpp"\nnamespace Tyra {')
    edit(queue,'  // Logical source sequence numbers were reserved while recording.',
         '  CommandTape::capture(nativeWriter->data(),qwords);\n  // Logical source sequence numbers were reserved while recording.')
    edit(queue,'void Vif1Queue::enqueue(const void* chain, u32 sequence) {',
         'void Vif1Queue::enqueue(const void* chain, u32 sequence) {\n  if(CommandTape::armed)++CommandTape::submissions;')
    adapter=engine/'inc/renderer/core/paths/path1/frame_submission.hpp'
    edit(adapter,'#include "vif1_queue.hpp"','#include "vif1_queue.hpp"\n#include "debug/command_tape.hpp"')
    edit(adapter,'    // Order an unsupported direct transfer after all recorded VU/GIF work.',
         '    TYRA_ASSERT(!CommandTape::armed,"Uncaptured direct submission during command frame");\n    // Order an unsupported direct transfer after all recorded VU/GIF work.')
    edit(physics,'#include "debug/bag_tape.hpp"','#include "debug/bag_tape.hpp"\n#include "debug/command_tape.hpp"')
    motion=engine/'inc/debug/motion_probe.hpp'
    edit(motion,'inline void begin(U pacing) {','inline bool suspended=false;\ninline void begin(U pacing) {\n  if(suspended){sample=enabled=clocks=false;return;}')
    edit(motion,'inline void end(U pacing) {','inline void end(U pacing) {\n  if(suspended){sample=enabled=clocks=false;return;}')
    edit(game,'    BagTape::frozen=true;g_frameDt=0;engine->renderer.core.synchronizeFrame();',
         '    BagTape::frozen=true;MotionProbe::suspended=true;MotionProbe::sample=MotionProbe::enabled=MotionProbe::clocks=false;\n    WorldFront::active=false;ClipTrial::mode=0;\n    g_frameDt=0;engine->renderer.core.synchronizeFrame();')
    edit(physics,'  auto& core=engine->renderer.core;renderer=&core;',
         '''  auto& core=engine->renderer.core;renderer=&core;
  identityContext=this;
  identify=[](void* ctx,const StaPipBag* bag,const char*& producer,int& source,const char*& pass){
    static_cast<TerrainGame*>(ctx)->identifyTapeBag(bag,producer,source,pass);
  };''')
    edit(physics,'  if(stage==2 && !control) replay();else renderScene();','  renderScene();')
    edit(physics,'  engine->renderer.endFrame();core.synchronizeFrame();',
         '  engine->renderer.endFrame();core.synchronizeFrame();core.sync.align2D();\n  if(iteration==23)CommandTape::finish();')
    edit(physics,'  const unsigned stage=iteration/24, local=iteration%24;',
         '  const unsigned stage=iteration/24, local=iteration%24;\n  if(iteration==23)CommandTape::arm();')
    edit(physics,'    captureImage(core,stage,false);captureImage(core,stage,true);',
         '    if(stage==0)exportIdentity();\n    captureImage(core,stage,false);captureImage(core,stage,true);')
    edit(physics,'unsigned(stage==2 && !control)','0u')
    edit(vehicles,'    {\n      const bool parkedNow = v.sleepFrames >= 25;',
         '    if(!BagTape::frozen){\n      const bool parkedNow = v.sleepFrames >= 25;')
    edit(h,'  void renderBagTapeTrial();',
         '  void renderBagTapeTrial();\n  void identifyTapeBag(const Tyra::StaPipBag*,const char*&,int&,const char*&);')
    classifier='''void TerrainGame::identifyTapeBag(const Tyra::StaPipBag* bag,const char*& producer,int& source,const char*& pass) {
  auto match=[&](const Tyra::StaPipBag* b,const char* p,int s,const char* kind){
    if(!b || b!=bag)return false;
    producer=p;source=s;pass=kind;return true;
  };
  for(unsigned i=0;i<terrainChunks.size();++i){const auto& c=terrainChunks[i];
    if(match(c.bag.get(),"terrain",i,"base") || match(c.aoBag.get(),"terrain",i,"ao") ||
       match(c.emisBag.get(),"terrain",i,"emissive") || match(c.gsBag.get(),"terrain",i,"ground-shadow"))return;
    for(const auto& l:c.layerPasses)if(match(l.bag.get(),"terrain",i,"layer"))return;
  }
  for(unsigned i=0;i<procChunks.size();++i){const auto& c=procChunks[i];
    if(match(c.bag.get(),c.owner==-3?"road":"procedural",i,c.roadEdge?"edge":c.roadBlend?"spill":"base"))return;
  }
  for(unsigned i=0;i<staticBatches.size();++i)if(match(staticBatches[i].bag.get(),"static-batch",i,"base"))return;
  for(unsigned i=0;i<objectGeometry.size();++i)for(const auto& p:objectGeometry[i].parts){
    if(match(p.bag.get(),"object",i,"base") || match(p.envBag.get(),"object",i,"reflection") ||
       match(p.silBag.get(),"object",i,"silhouette") || match(p.aoBag.get(),"object",i,"ao") ||
       match(p.emisBag.get(),"object",i,"emissive"))return;
  }
  if(match(skyDome.bag.get(),"sky",0,"base") || match(wheelBag_.get(),"wheels",0,"base"))return;
}

'''
    edit(physics,'void TerrainGame::renderBagTapeTrial() {',classifier+'void TerrainGame::renderBagTapeTrial() {')
    (out/'bag-tape-manifest.json').write_text(json.dumps(dict(experiment='A36 current V7 ordinary baseline capture',
        source=str(source),parentElf=binding['elfSha256'],stages=['capture ordinary','ordinary control','ordinary control','ordinary return'],
        frame='day drive offset1380; update frozen and outer motion suspended; live capture forced',
        scope='Scene only; no timing/replay/default change; identity is actual descriptor owner, NOT per-command ownership'),indent=2)+'\n')
print(out)
