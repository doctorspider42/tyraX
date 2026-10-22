"""Generate the source-bound three-route frozen-scene timing harness."""
import argparse,subprocess,sys,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(repo/'tools/tyrax2-command-tape.py'),str(a.source),str(a.out),'--replay'],check=True)
engine=a.out/'tyra/engine'
def edit(path,old,new):
    t=path.read_text(encoding='utf-8-sig');assert t.count(old)==1,(path,old,t.count(old));path.write_text(t.replace(old,new),encoding='utf-8')
import shutil
shutil.copy2(repo/'tools/tyrax2-command-pricing.hpp',engine/'inc/debug/command_pricing.hpp')
edit(engine/'inc/debug/debug.hpp','#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n")','#define TYRA_LOG(...) ((void)0) // Private timing: no ordinary file logging')
edit(engine/'src/renderer/core/paths/path1/vif1_queue.cpp','  if (++frames % 120 == 0) {','  if (false) { // No periodic allocator/report work in private timing')
adapter=engine/'inc/renderer/core/paths/path1/frame_submission.hpp'
edit(adapter,'#include "vif1_queue.hpp"','#include "vif1_queue.hpp"\n#include "debug/command_tape.hpp"')
edit(adapter,'    // Order an unsupported direct transfer after all recorded VU/GIF work.',
     '    TYRA_ASSERT(!CommandTape::armed,"Uncaptured direct submission during command frame");\n    // Order an unsupported direct transfer after all recorded VU/GIF work.')
game=a.out/'game/src/terrain_game.cpp'
edit(game,'#include "debug/bag_tape.hpp"','#include "debug/command_pricing.hpp"')
edit(game,'void TerrainGame::loop() {','void TerrainGame::loop() {\n  TapePricing::configure();')
edit(game,'if(MotionProbe::phase==0 && MotionProbe::offset==1380)',
     'if(MotionProbe::phase==TapePricing::phase && MotionProbe::offset==TapePricing::offset)')
edit(game,'    BagTape::frozen=true;g_frameDt=0;',
     '    MotionProbe::sample=MotionProbe::enabled=MotionProbe::clocks=false;\n    BagTape::frozen=true;g_frameDt=0;')
edit(game,'    printf("TAPEFREEZE phase=0 offset=1380 z=%f speed=%f\\n",MotionProbe::current.z,MotionProbe::current.speed);',
     '    printf("PRICEFREEZE phase=%u offset=%u z=%f speed=%f yaw=%f\\n",TapePricing::phase,TapePricing::offset,MotionProbe::current.z,MotionProbe::current.speed,MotionProbe::current.yaw);')
vehicles=a.out/'game/src/gen/game_vehicles.gen.cpp'
edit(vehicles,'    {\n      const bool parkedNow = v.sleepFrames >= 25;',
     '    if(!BagTape::frozen){\n      const bool parkedNow = v.sleepFrames >= 25;')
game=a.out/'game/src/gen/game_physics.gen.cpp'
edit(game,'#include "debug/command_replay.hpp"','#include "debug/command_pricing.hpp"')
t=game.read_text(encoding='utf-8-sig');start=t.index('void TerrainGame::renderBagTapeTrial() {');end=t.index('void TerrainGame::renderScene() {',start)
body='''void TerrainGame::renderBagTapeTrial() {
  using namespace BagTape;
  if(iteration>=TapePricing::length*TapePricing::stages){SleepThread();return;}
  auto& core=engine->renderer.core;renderer=&core;beamBatchCall=0;g_frameDt=0;
  const unsigned stage=iteration/TapePricing::length,local=iteration%TapePricing::length;
  const unsigned mode=TapePricing::mode(stage);
  if(iteration==0)CommandTape::target=core.gs.getRecordingContext();
  core.synchronizeFrame();
  TYRA_ASSERT(core.gs.selectDiagnosticTarget(uint8_t(CommandTape::target)),"Cannot pin price target");
  const bool capture=stage==0 && local==TapePricing::length-1;
  const bool check=local==TapePricing::length-1;
  if(capture)CommandTape::arm();
  recording=capture;checking=check && stage>0 && mode==0;
  checkIndex=checkMismatch=checkMask=0;firstMismatch=~0u;
  const unsigned stall=core.getStallTotal(),start=TapePricing::tick();
  unsigned pacing=0,startup=0,prepare=0;
  if(mode==2){pacing=TapePricing::command(core,local==0 || check,startup);prepare=startup;}
  else{
    engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));
    engine->renderer.renderer3D.usePipeline(stapip);
    if(mode==1)replay();else renderScene();
    prepare=TapePricing::tick()-start;
    engine->renderer.endFrame();core.synchronizeFrame();
    pacing=core.getStallTotal()-stall;
  }
  core.sync.align2D(); // Fence the hybrid presentation copy in EVERY arm.
  const unsigned whole=TapePricing::tick()-start;
  recording=checking=false;
  if(stage>0 && local>=64 && local<192){
    TYRA_ASSERT(TapePricing::used<TapePricing::count && pacing<=whole,"Invalid timing window");
    TapePricing::samples[TapePricing::used++]={stage,local,whole,pacing,startup,prepare};
  }
  if(capture){CommandTape::finish();CommandTape::prepare(core);
    TapePricing::closureHash=TapePricing::hash(CommandTape::memory,CommandTape::bytes);}
  if(check){
    TYRA_ASSERT(TapePricing::closureHash==TapePricing::hash(CommandTape::memory,CommandTape::bytes),"Owned command closure mutated");
    printf("PRICECHECK stage=%u mode=%u mismatches=%u mask=%u bags=%u events=%u bytes=%u lights=%u target=%u buffers=%u frameYield=%u\\n",
      stage,mode,checkMismatch,checkMask,calls,unsigned(events.size()),bytes,unsigned(core.dynLightCount),unsigned(CommandTape::target),unsigned(core.gs.getFrameBufferCount()),unsigned(core.getFrameYield()));fflush(stdout);
    captureImage(core,stage,false);captureImage(core,stage,true);
  }
  ++iteration;if(iteration==TapePricing::length*TapePricing::stages)TapePricing::dump();
}

'''
game.write_text(t[:start]+body+t[end:],encoding='utf-8')
(a.out/'command-pricing-manifest.json').write_text(json.dumps(dict(stages=[0,0,1,0,2,0,2,0,1,0],length=224,sampleRange=[64,192],scope='Frozen Scene; fixed captured target; inclusive completion less pacing; helper cost retained; no update/HUD/postFX',config='Optional three-path.cfg: phase(0 or3) offset(1200..1600)'),indent=2)+'\n')
print(a.out)
