"""Create a private command-capture continuation of source-bound A18 V5."""
import argparse,hashlib,json,shutil,importlib.util
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('out',type=Path)
p.add_argument('--replay',action='store_true')
a=p.parse_args();assert not a.out.exists()
repo=Path(__file__).resolve().parents[1]
b=json.loads((a.source/'native-binding.json').read_text())
for rel,h in b['files'].items():assert hashlib.sha256((a.source/rel).read_bytes()).hexdigest()==h,rel
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
def ignore(path,names):
    return set(q.ignore(path,names)) | {n for n in names if n.startswith(('tape-','command-')) or n=='bag-tape-control.cfg'}
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
shutil.copy2(a.source/'tyra/Makefile.base',a.out/'tyra/Makefile.base')
for name in ('authored-manifest.json','bag-tape-manifest.json'):shutil.copy2(a.source/name,a.out/name)
engine=a.out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-command-tape.hpp',engine/'inc/debug/command_tape.hpp')
shutil.copy2(repo/'tools/tyrax2-command-size.hpp',engine/'inc/debug/command_size.hpp')
def edit(path,old,new):path.write_text(q.once(path.read_text(encoding='utf-8-sig'),old,new),encoding='utf-8')
queue=engine/'src/renderer/core/paths/path1/vif1_queue.cpp'
edit(queue,'namespace Tyra {','#include "debug/command_tape.hpp"\nnamespace Tyra {')
edit(queue,'  // Logical source sequence numbers were reserved while recording.',
     '  CommandTape::capture(nativeWriter->data(),qwords);\n  // Logical source sequence numbers were reserved while recording.')
edit(queue,'void Vif1Queue::enqueue(const void* chain, u32 sequence) {',
     'void Vif1Queue::enqueue(const void* chain, u32 sequence) {\n  if(CommandTape::armed)++CommandTape::submissions;')
game=a.out/'game/src/gen/game_physics.gen.cpp'
edit(game,'#include "debug/bag_tape.hpp"','#include "debug/bag_tape.hpp"\n#include "debug/command_tape.hpp"')
edit(game,'  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));\n  engine->renderer.renderer3D.usePipeline(stapip);\n  recording=iteration==23;',
     '  if(iteration==23)CommandTape::arm();\n  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));\n  engine->renderer.renderer3D.usePipeline(stapip);\n  recording=iteration==23;')
edit(game,'  engine->renderer.endFrame();core.synchronizeFrame();\n  if(local==23){',
     '  engine->renderer.endFrame();core.synchronizeFrame();\n  if(iteration==23)CommandTape::finish();\n  if(local==23){')
if a.replay:
    shutil.copy2(repo/'tools/tyrax2-command-replay.hpp',engine/'inc/debug/command_replay.hpp')
    edit(game,'#include "debug/command_tape.hpp"','#include "debug/command_replay.hpp"')
    queueh=engine/'inc/renderer/core/paths/path1/vif1_queue.hpp'
    edit(queueh,'  static void drain();','  static void drain();\n  static u32 submitDiagnosticOwned(const void* chain);')
    edit(queue,'void Vif1Queue::enqueue(const void* chain, u32 sequence) {','''u32 Vif1Queue::submitDiagnosticOwned(const void* chain) {
  if(recordingFrame())return 0;
  drain();
  u32 sequence=submitted+1;if(!sequence)sequence=1;
  submitted=sequence;
#if TYRA_VIF1_QUEUE_LAZY_FLUSH
  flushedUpTo=sequence-1;
#else
  FlushCache(0);
#endif
  enqueue(chain,sequence);return sequence;
}
void Vif1Queue::enqueue(const void* chain, u32 sequence) {''')
    gsh=engine/'inc/renderer/core/gs/renderer_core_gs.hpp'
    edit(gsh,'  void setRecordingContext(u8 target) { context = target; }',
         '  void setRecordingContext(u8 target) { context = target; }\n  bool selectDiagnosticTarget(u8 target);')
    gsc=engine/'src/renderer/core/gs/renderer_core_gs.cpp'
    edit(gsc,'void RendererCoreGS::flipBuffers(bool throttle, bool synthetic) {','''bool RendererCoreGS::selectDiagnosticTarget(u8 target) {
  if((bufferCount!=2 && bufferCount!=3) || target>=bufferCount ||
     settings->isFieldRendering() || rasterRedirected){
    printf("COMMANDTARGET rejected buffers=%u target=%u hybrid=%u field=%u redirected=%u\\n",
      unsigned(bufferCount),unsigned(target),unsigned(settings->isHybridOutput()),unsigned(settings->isFieldRendering()),unsigned(rasterRedirected));
    return false;
  }
  while(pendingBuffer>=0)graph_wait_vsync();
  if(settings->isHybridOutput()){
    if(target!=0)return false;
    context=0;emitDrawTargetSwitch(0);return true;
  }
  DIntr();context=target;displayedBuffer=(target+1)%bufferCount;
  presentFrameBuffer(static_cast<u8>(displayedBuffer));EIntr();
  emitDrawTargetSwitch(target);return true;
}
void RendererCoreGS::flipBuffers(bool throttle, bool synthetic) {''')
    edit(game,'  if(iteration>=96){SleepThread();return;}','  if(iteration>=144){SleepThread();return;}')
    edit(game,'  if(iteration==23)CommandTape::arm();',
         '  if(iteration==23){CommandTape::target=core.gs.getRecordingContext();CommandTape::arm();}\n  if(stage==4){CommandTape::run(core);}else{')
    edit(game,'  if(iteration==23)CommandTape::finish();',
         '  if(iteration==23){CommandTape::finish();CommandTape::prepare(core);}\n  }')
    edit(game,'  checking=local==23 && (stage==1 || stage==3);','  checking=local==23 && (stage==1 || stage==3 || stage==5);')
    edit(game,'  if(iteration==96){printf("TAPEDONE stages=4 qualificationOnly=1\\n");fflush(stdout);}',
         '  if(iteration==144){printf("TAPEDONE stages=6 qualificationOnly=1 commands=%u\\n",unsigned(CommandTape::replays));fflush(stdout);}')
(a.out/'command-tape-manifest.json').write_text(json.dumps(dict(source=str(a.source),parentElf=b['elfSha256'],scope='One owned frame capture; no timing yet'),indent=2)+'\n')
print(a.out)
