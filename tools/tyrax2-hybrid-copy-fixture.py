"""A41 independent immutable Hybrid2 copy: full-game Off/On/Off fixture."""
import argparse, hashlib, importlib.util, json, shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path);p.add_argument('out',type=Path)
p.add_argument('--reverse',action='store_true')
p.add_argument('--late-ready',action='store_true',help='A42 late-ready notifications against A41 START-only')
p.add_argument('--retain-ready',action='store_true',help='A43 retain ISR ownership while main waits for native readiness')
a=p.parse_args();source=a.source.resolve();out=a.out.resolve()
assert not (a.late_ready and a.retain_ready)
notifications=a.late_ready or a.retain_ready
enabled='!=1' if a.reverse else '==1'
order=[1,0,1]*2 if a.reverse else [0,1,0]*2
copyEnabled='true' if notifications else f'MotionProbe::phase%3{enabled}'
heldCopyEnabled='true' if notifications else f'arm{enabled}'
phaseReady=f'HybridCopyTrial::setLateReady(MotionProbe::phase%3{enabled});' if a.late_ready else ''
heldReady=f'HybridCopyTrial::setLateReady(arm{enabled});HybridCopyTrial::heldQualification=true;' if a.late_ready else ''
if a.retain_ready:
 phaseReady=f'HybridCopyTrial::setLateReady(true);HybridCopyTrial::retainNativeReady=MotionProbe::phase%3{enabled};'
 heldReady=f'HybridCopyTrial::setLateReady(true);HybridCopyTrial::retainNativeReady=arm{enabled};HybridCopyTrial::heldQualification=true;'
assert not out.exists()
repo=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(source/rel)==digest,rel
assert json.loads((source/'triple-physical.json').read_text())['firstOrderGate']
assert json.loads((source/'triple-manifest.json').read_text())['phases']==[3,2,3,3,2,3]
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
def ignore(path,names):
 return q.ignore(path,names)+[n for n in names if n.startswith(('pc-drive-','pc-held-','triple-present-')) and n.endswith('.csv')]
for part in ('game','tyra/engine'):shutil.copytree(source/part,out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','parent-native-binding.json'):shutil.copy2(source/rel,out/rel)
shutil.copy2(source/'native-binding.json',out/'a38-native-binding.json')
engine=out/'tyra/engine'
pins={}
for name in ('protocol','adapter','ee','window','trial'):
 filename=f'tyrax2-hybrid-copy-{name}.hpp'
 shutil.copy2(repo/'tools'/filename,engine/'inc/debug'/filename);pins[filename]=sha(repo/'tools'/filename)
if notifications:
 path=engine/'inc/debug/tyrax2-hybrid-copy-trial.hpp'
 text=path.read_text();assert text.count('lateReadySupport = false;')==1
 path.write_text(text.replace('lateReadySupport = false;','lateReadySupport = true;'))
 if a.retain_ready:
  text=path.read_text();assert text.count('retainReadySupport = false;')==1
  path.write_text(text.replace('retainReadySupport = false;','retainReadySupport = true;'))
 pins[path.name]=sha(path)
def edit(path,old,new):
 t=path.read_text(encoding='utf-8-sig');path.write_text(q.once(t,old,new),encoding='utf-8')
run=engine/'src/engine.cpp'
edit(run,'#include "debug/triple_trial.hpp"','#include "debug/triple_trial.hpp"\n#include "debug/tyrax2-hybrid-copy-trial.hpp"')
edit(run,'renderer.core.setDiagnosticTriple(MotionProbe::phase%3!=1);',
 f'renderer.core.setDiagnosticTriple(false);HybridCopyTrial::enabled={copyEnabled};{phaseReady}')
if notifications:
 edit(run,'  if(MotionProbe::offset==0){','  if(MotionProbe::offset==0){\n    renderer.core.synchronizeFrame();')
edit(run,'unsigned(MotionProbe::phase%3!=1),renderer.core.gs.getFrameBufferCount()',
 '0u,renderer.core.gs.getFrameBufferCount()')
edit(run,'if(MotionProbe::offset==MotionProbe::gasStart)TripleTrial::start(MotionProbe::phase);',
 'if(MotionProbe::offset==MotionProbe::gasStart)TripleTrial::start(MotionProbe::phase);')
edit(run,'  MotionProbe::begin(renderer.core.getStallTotal());',
 '  if(MotionProbe::offset==MotionProbe::gasStart){renderer.core.synchronizeFrame();HybridCopyTrial::begin(MotionProbe::phase);}\n  MotionProbe::begin(renderer.core.getStallTotal());')
edit(run,'if(MotionProbe::phase>=MotionProbe::phaseCount){SleepThread();return;}',
 'if(MotionProbe::phase>=MotionProbe::phaseCount){renderer.core.synchronizeFrame();HybridCopyTrial::uninstall();SleepThread();return;}')
edit(run,'TripleTrial::stopAndExport();',
 'TripleTrial::stopAndExport();HybridCopyTrial::exportRows(MotionProbe::phase,0,MotionProbe::phase%3);')
physics=out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'#include "debug/triple_trial.hpp"','#include "debug/triple_trial.hpp"\n#include "debug/tyrax2-hybrid-copy-trial.hpp"')
edit(physics,'core.setDiagnosticTriple(arm==1,true);setupProjShadows();',
 f'core.setDiagnosticTriple(false,true);setupProjShadows();HybridCopyTrial::enabled={heldCopyEnabled};{heldReady}HybridCopyTrial::begin(MotionProbe::phase);')
edit(physics,'  core.setDiagnosticTriple(MotionProbe::phase%3!=1,true);setupProjShadows();',
 f'  core.setDiagnosticTriple(false,true);setupProjShadows();HybridCopyTrial::enabled={copyEnabled};'+
 (f'HybridCopyTrial::heldQualification=false;{phaseReady}' if notifications else ''))
edit(physics,'    printf("TERRAINFOCUSCHECK phase=',
 '    HybridCopyTrial::exportRows(MotionProbe::phase,1,arm);\n    printf("TERRAINFOCUSCHECK phase=')
edit(physics,'      core.synchronizeFrame();\n      TYRA_ASSERT(core.gs.selectDiagnosticTarget(uint8_t(target)),"Cannot pin join target");\n      beamBatchCall=0;',
 '      beamBatchCall=0;')
edit(physics,'      engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));',
 '''      engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));
      // Qualification ONLY, identical in all arms and outside moving prices.
      // Keep the new clear prefix unsubmitted across a fresh old-job VBlank.
      if(frame==1){
        u32 copyStart,copyNow;asm volatile("mfc0 %0,$9":"=r"(copyStart)::"memory");
        do{asm volatile("mfc0 %0,$9":"=r"(copyNow)::"memory");}while(u32(copyNow-copyStart)<11796480u);
      }''')
edit(physics,'      renderScene();engine->renderer.endFrame();core.synchronizeFrame();core.sync.align2D();\n    }',
 '      renderScene();engine->renderer.endFrame();\n    }\n    core.synchronizeFrame();core.sync.align2D();')
edit(physics,'    for(unsigned env=0;env<2;++env){','    for(unsigned env=0;env<3;++env){')
edit(physics,'      if(env){auto* texture=core.envMap.getTexture()->vramResident;',
 '      if(env==2){fb=core.gs.getIndependentHybridDisplayBuffer();address=fb->address;width=fb->width;height=fb->height;psm=fb->psm;}\n      if(env==1){auto* texture=core.envMap.getTexture()->vramResident;')
edit(physics,'stage,env?"env":"main");','stage,env==2?"display":env?"env":"main");')
edit(physics,'      alignas(16) static unsigned line[1024];','      alignas(64) static unsigned line[1024];')
qh=engine/'inc/renderer/core/paths/path1/vif1_queue.hpp'
qc=engine/'src/renderer/core/paths/path1/vif1_queue.cpp'
current=(repo/'vendor/tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp').read_text()
start=current.index('bool Vif1Queue::canCopyCompletedNativeFrame(')
end=current.index('void Vif1Queue::waitFor(',start)
edit(qh,'  static void waitFor(u32 sequence);',
 '  static void waitFor(u32 sequence);\n  static bool canCopyCompletedNativeFrame(u32 sequence);')
edit(qc,'void Vif1Queue::waitFor(u32 sequence) {',current[start:end]+'void Vif1Queue::waitFor(u32 sequence) {')
edit(qc,'u32 nativePendingSequence = 0, nativeInFlight = 0, nativeBatches = 0;',
 'u32 nativePendingSequence = 0, nativeInFlight = 0, nativeBatches = 0;\nvolatile u32 nativePresentationSequence = 0;')
edit(qc,'  if (frameRecording) completeBeforeSubmission();\n#endif\n  const u32 addr',
 '  if (frameRecording) completeBeforeSubmission();\n  nativePresentationSequence = 0;\n#endif\n  const u32 addr')
edit(qc,'  frameRecording = false;\n  flushRecording();',
 '  frameRecording = false;\n  flushRecording();\n  if (framePipelined) nativePresentationSequence = nativeInFlight;')
core=engine/'src/renderer/core/renderer_core.cpp'
edit(core,'#include "debug/hardware_trace.hpp"',
 '#include "debug/hardware_trace.hpp"\n#include "debug/tyrax2-hybrid-copy-trial.hpp"')
edit(core,'  sync.waitAndClear();\n  HardwareTrace::Scope tracePacing',
 '  const bool copied=HybridCopyTrial::complete(pipelineSequence);\n  if(!copied)sync.waitAndClear();\n  HardwareTrace::Scope tracePacing')
edit(core,'  if (gs.getFrameBufferCount() < 3 && isFrameLimitOn) graph_wait_vsync();',
 '  if (!copied && gs.getFrameBufferCount() < 3 && isFrameLimitOn) graph_wait_vsync();')
edit(core,'    pipelineFramePending = true;',
 '''    pipelineFramePending = true;
    if(isFrameLimitOn && settings.isHybridOutput() &&
       !settings.isFieldRendering() && !blss.isEnabled() && !gs.isRasterRedirected() &&
       gs.getFrameBufferCount()==2 && recordingContext==0 && gs.prepareIndependentHybridCopy())
      if(HybridCopyTrial::enabled)HybridCopyTrial::arm(pipelineSequence);''')
gsh=engine/'inc/renderer/core/gs/renderer_core_gs.hpp'
edit(gsh,'  void flipBuffers();',
 '  void flipBuffers();\n  bool prepareIndependentHybridCopy();\n  framebuffer_t* getIndependentHybridDisplayBuffer() { return &frameBuffers[1]; }')
gs=engine/'src/renderer/core/gs/renderer_core_gs.cpp'
edit(gs,'#include "debug/triple_trial.hpp"',
 '#include "debug/triple_trial.hpp"\n#include "debug/tyrax2-hybrid-copy-trial.hpp"')
edit(gs,'  TripleTrial::record(index);','  // A41 cadence is recorded at conversion kick, not later DISPFB bookkeeping.')
edit(gs,'  packet2_update(hybridPacket, q);\n  frameWaitGif(DMA_CHANNEL_GIF, 0);',
 '  packet2_update(hybridPacket, q);\n  if(HybridCopyTrial::capture(hybridPacket))return;\n  frameWaitGif(DMA_CHANNEL_GIF, 0);')
edit(gs,'  frameSendPacket(hybridPacket, DMA_CHANNEL_GIF, true);',
 '  frameSendPacket(hybridPacket, DMA_CHANNEL_GIF, true);\n  TripleTrial::record(target);')
edit(gs,'      emitHybridPresent(1);',
 '      if(HybridCopyTrial::skipNext)HybridCopyTrial::skipNext=false;\n      else emitHybridPresent(1);')
edit(gs,'void RendererCoreGS::emitHybridPresent(u8 target) {',
 '''bool RendererCoreGS::prepareIndependentHybridCopy() {
  using namespace HybridCopyTrial;
  if(!settings->isHybridOutput() || settings->isFieldRendering() ||
     settings->getDisplayMode()!=DisplayMode::Progressive480p ||
     !settings->isHybridDitherActive() || screenCenter!=2048.0F || zBuffer.mask!=0 ||
     bufferCount!=2 || context!=0 || isRasterRedirected() ||
     frameBuffers[0].width!=448 || frameBuffers[0].height!=448 ||
     frameBuffers[0].address!=0 || frameBuffers[1].address!=200704 ||
     zBuffer.address!=301056 || zBuffer.zsm!=GS_ZBUF_32 || !install())return false;
  TYRA_ASSERT(!adapter.sourceProtected(),"A41 build while old copy owned");
  // Layout/format is pinned above. Generate once, outside ISR; later arms
  // publish this same immutable packet with ordinary main cache operations.
  if(!packetWords){building=true;emitHybridPresent(1);building=false;}
  return packetWords==81;
}

void RendererCoreGS::emitHybridPresent(u8 target) {''')
manifest={'experiment':'A41','parentElf':binding['elfSha256'],'source':str(source),
 'modes':order,'held':order[:3],'buffers':[2]*6,'headers':pins,
 'scope':'Actual Core/native ownership, immutable original copy, independent S/E/FINISH IRQ; experimental physical budget, no default',
 'nextGates':['native binding','held main/env raster equality','positive old-job ISR and fallback witnesses',
 'actual physical blank completion','same-ELF PS2 Off/On/Off and reverse']}
if a.late_ready:
 manifest.update(experiment='A42',copyModes=[1]*6,heldCopy=[1]*3,
  scope='Late native-ready notifications against unchanged A41 START-only, all Hybrid2')
if a.retain_ready:
 manifest.update(experiment='A43',copyModes=[1]*6,heldCopy=[1]*3,
  readyModes=[1]*6,heldReady=[1]*3,
  scope='Retain Armed ownership through native tail against A42 early withdrawal; notifications ON all arms')
(out/'hybrid-copy-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(out/'triple-manifest.json').write_text(json.dumps({'experiment':'A38','parentElf':binding['elfSha256'],'phases':[2]*6,'held':[2]*3},indent=2)+'\n')
print(out)
