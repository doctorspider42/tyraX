"""A39 protected ready-present Off/On/Off, actual Hybrid3 in every arm."""
import argparse,hashlib,importlib.util,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();source=a.source.resolve();out=a.out.resolve();assert not out.exists()
repo=Path(__file__).resolve().parents[1];sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
b=json.loads((source/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(source/rel)==digest,rel
r=json.loads((source/'triple-physical.json').read_text());assert r['valid'] and r['firstOrderGate']
assert json.loads((source/'triple-manifest.json').read_text())['phases']==[3,2,3,3,2,3]
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
def ignore(path,names):return q.ignore(path,names)+[n for n in names if n.startswith(('pc-drive-','pc-held-','triple-present-')) and n.endswith('.csv')]
for part in ('game','tyra/engine'):shutil.copytree(source/part,out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','parent-native-binding.json'):shutil.copy2(source/rel,out/rel)
shutil.copy2(source/'native-binding.json',out/'a38-native-binding.json')
engine=out/'tyra/engine'
for name in ('ready-guard','ready-trial'):shutil.copy2(repo/f'tools/tyrax2-{name}.hpp',engine/f'inc/debug/{name.replace("-","_")}.hpp')
def edit(path,old,new):
 t=path.read_text(encoding='utf-8-sig');path.write_text(q.once(t,old,new),encoding='utf-8')
run=engine/'src/engine.cpp'
edit(run,'#include "debug/triple_trial.hpp"','#include "debug/triple_trial.hpp"\n#include "debug/ready_trial.hpp"')
edit(run,'renderer.core.setDiagnosticTriple(MotionProbe::phase%3!=1);','renderer.core.setDiagnosticTriple(true);ReadyTrial::reset(false);')
edit(run,'unsigned(MotionProbe::phase%3!=1),renderer.core.gs.getFrameBufferCount()','1u,renderer.core.gs.getFrameBufferCount()')
edit(run,'if(MotionProbe::offset==MotionProbe::gasStart)TripleTrial::start(MotionProbe::phase);',
 'if(MotionProbe::offset==MotionProbe::gasStart){ReadyTrial::reset(MotionProbe::phase%3==1);TripleTrial::start(MotionProbe::phase);}')
edit(run,'TripleTrial::stopAndExport();','TripleTrial::stopAndExport();ReadyTrial::witness(MotionProbe::phase,0,0);ReadyTrial::reset(false);')
physics=out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'#include "debug/triple_trial.hpp"','#include "debug/triple_trial.hpp"\n#include "debug/ready_trial.hpp"')
edit(physics,'core.setDiagnosticTriple(arm==1,true);setupProjShadows();','core.setDiagnosticTriple(true,true);setupProjShadows();ReadyTrial::reset(arm==1);')
edit(physics,'  core.setDiagnosticTriple(MotionProbe::phase%3!=1,true);setupProjShadows();',
 '  ReadyTrial::reset(false);core.setDiagnosticTriple(true,true);setupProjShadows();')
edit(physics,'      core.synchronizeFrame();\n      TYRA_ASSERT(core.gs.selectDiagnosticTarget(uint8_t(target)),"Cannot pin join target");\n      beamBatchCall=0;',
 '      beamBatchCall=0;')
edit(physics,'      engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));',
 '''      engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));
      // Qualification-only, identical in all arms: let the old GPU job finish
      // while a new clear prefix is unsubmitted, exercising real early publish.
      u32 readyStart,readyNow;asm volatile("mfc0 %0,$9":"=r"(readyStart)::"memory");
      do{asm volatile("mfc0 %0,$9":"=r"(readyNow)::"memory");}while(u32(readyNow-readyStart)<11796480u);
      core.tryReadyPresent();''')
edit(physics,'      renderScene();engine->renderer.endFrame();core.synchronizeFrame();core.sync.align2D();\n    }',
 '      renderScene();engine->renderer.endFrame();\n    }\n    core.synchronizeFrame();core.sync.align2D();')
# Each arm's witness is outside rendered/price windows, before source readback.
edit(physics,'    printf("TERRAINFOCUSCHECK phase=',
 '    ReadyTrial::witness(MotionProbe::phase,1,arm);ReadyTrial::reset(false);\n    printf("TERRAINFOCUSCHECK phase=')
header=engine/'inc/renderer/core/renderer_core.hpp'
edit(header,'  void setDiagnosticTriple(bool on,bool force=false);',
 '  void setDiagnosticTriple(bool on,bool force=false);\n  void tryReadyPresent();')
gs=engine/'inc/renderer/core/gs/renderer_core_gs.hpp'
edit(gs,'  u8 getRecordingContext() const { return context; }',
 '  u8 getRecordingContext() const { return context; }\n  bool readyHybridPresent() const { return bufferCount==3 && pendingBuffer<0; }')
qh=engine/'inc/renderer/core/paths/path1/vif1_queue.hpp'
edit(qh,'  static void waitFor(u32 sequence);','  static void waitFor(u32 sequence);\n  static bool completeReadyPresentation(u32 sequence);')
qc=engine/'src/renderer/core/paths/path1/vif1_queue.cpp'
edit(qc,'void Vif1Queue::waitFor(u32 sequence) {','''bool Vif1Queue::completeReadyPresentation(u32 sequence) {
#if TYRA_FRAME_PIPELINE_SUPPORT
  if(!sequence || protectRecording || !beforeFrameSubmit)return false;
  advanceIfIdle();
  if(static_cast<s32>(completed-sequence)<0)return false;
  const auto writer=nativeWriter;const u32 mark=writer?writer->checkpoint():0;
  const auto arena=chainArena;const u32 used=arena?arena->used():0;
  const u32 bank=currentBank;const bool hadPrefix=nativePendingSequence!=0;
  {
    ReadyRecordingSuspend suspended(frameRecording,protectRecording,path3FencePending,nativePendingSequence);
    beforeFrameSubmit(beforeFrameUser);
  }
  TYRA_ASSERT(nativeWriter==writer && (!writer || writer->checkpoint()==mark) &&
              chainArena==arena && (!arena || arena->used()==used) && currentBank==bank,
              "A39 current prefix mutated during old presentation");
  if(hadPrefix)++ReadyTrial::protectedPrefixes;
  return true;
#else
  (void)sequence;return false;
#endif
}

void Vif1Queue::waitFor(u32 sequence) {''')
edit(qc,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/ready_guard.hpp"\n#include "debug/ready_trial.hpp"')
core=engine/'src/renderer/core/renderer_core.cpp'
edit(core,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/ready_trial.hpp"')
edit(core,'void RendererCore::beginFrameRecording() {','''void RendererCore::tryReadyPresent(){
  if(!ReadyTrial::enabled || !pipelineFramePending)return;
  ++ReadyTrial::checks;
  if(!pipelineFrameActive || !settings.isHybridOutput() || settings.isFieldRendering() ||
     blss.isEnabled() || gs.isRasterRedirected() || !gs.readyHybridPresent() || !sync.check())return;
  const u8 context=gs.getRecordingContext();
  if(Vif1Queue::completeReadyPresentation(pipelineSequence)){
    TYRA_ASSERT(!pipelineFramePending,"A39 old job not consumed");++ReadyTrial::presented;
  }
  gs.setRecordingContext(context);
}

void RendererCore::beginFrameRecording() {
  tryReadyPresent();''')
stapip=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(stapip,'void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {',
 'void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {\n  rendererCore->tryReadyPresent();')
(out/'triple-manifest.json').write_text(json.dumps(dict(experiment='A38',parentElf=json.loads((out/'parent-native-binding.json').read_text())['elfSha256'],phases=[3]*6,held=[3]*3),indent=2)+'\n')
(out/'ready-manifest.json').write_text(json.dumps(dict(experiment='A39',parentElf=b['elfSha256'],source=str(source),modes=[0,1,0,0,1,0],held=[0,1,0],scope='Actual ready old-job publication; same Hybrid3/workload, no sampler'),indent=2)+'\n')
print(out)
