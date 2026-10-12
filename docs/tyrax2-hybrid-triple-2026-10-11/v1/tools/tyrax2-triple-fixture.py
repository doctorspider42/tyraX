"""A38 same-ELF Hybrid double/triple/double, real VRAM rebuild and latched presents."""
import argparse,hashlib,importlib.util,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();source=a.source.resolve();out=a.out.resolve();assert not out.exists()
repo=Path(__file__).resolve().parents[1];sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
b=json.loads((source/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(source/rel)==digest,rel
assert json.loads((source/'pc-moving-physical.json').read_text())['valid']
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
def ignore(path,names):return q.ignore(path,names)+[n for n in names if n.startswith(('pc-drive-','pc-held-')) and n.endswith('.csv')]
for part in ('game','tyra/engine'):shutil.copytree(source/part,out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(source/rel,out/rel)
shutil.copy2(source/'native-binding.json',out/'parent-native-binding.json')
engine=out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-triple-trial.hpp',engine/'inc/debug/triple_trial.hpp')
def edit(path,old,new):
 t=path.read_text(encoding='utf-8-sig');path.write_text(q.once(t,old,new),encoding='utf-8')
run=engine/'src/engine.cpp'
edit(run,'#include "debug/pc_moving.hpp"','#include "debug/triple_trial.hpp"')
edit(run,'''  MotionProbe::begin(renderer.core.getStallTotal());
  if(MotionProbe::phase>=MotionProbe::phaseCount){SleepThread();return;}
  if(MotionProbe::offset==MotionProbe::gasStart){
    const unsigned us=MotionProbe::phase%3==0?997:MotionProbe::phase%3==1?1733:0;
    PcMoving::start(MotionProbe::phase,0,us);
  }
  PcMoving::mark(MotionProbe::offset,1);
  if(MotionProbe::finishPending){PcMoving::stop();PcMoving::exportRows();}''',
 '''  if(MotionProbe::phase>=MotionProbe::phaseCount){SleepThread();return;}
  if(MotionProbe::offset==0){
    renderer.core.setDiagnosticTriple(MotionProbe::phase%3==1);
    printf("TRIPLEMODE phase=%u requested=%u actual=%u\\n",MotionProbe::phase,unsigned(MotionProbe::phase%3==1),renderer.core.gs.getFrameBufferCount());fflush(stdout);
  }
  MotionProbe::begin(renderer.core.getStallTotal());
  if(MotionProbe::offset==MotionProbe::gasStart)TripleTrial::start(MotionProbe::phase);
  if(MotionProbe::finishPending){
    renderer.core.synchronizeFrame();renderer.core.gs.selectDiagnosticTarget(0);
    TripleTrial::stopAndExport();
  }''')
edit(run,'  PcMoving::mark(MotionProbe::offset,0);','')
game=out/'game/src/terrain_game.cpp'
edit(game,'#include "debug/pc_moving.hpp"','#include "debug/triple_trial.hpp"')
edit(game,'  PcMoving::mark(MotionProbe::offset,2);\n','')
header=engine/'inc/renderer/core/renderer_core.hpp'
edit(header,'  void synchronizeFrame() { completePipelineFrame(); }',
 '  void synchronizeFrame() { completePipelineFrame(); }\n  void setDiagnosticTriple(bool on,bool force=false);')
core=engine/'src/renderer/core/renderer_core.cpp'
core.write_text(core.read_text()+'''
namespace Tyra {
void RendererCore::setDiagnosticTriple(bool on,bool force){
  if(!force && settings.getTripleBuffering()==on)return;
  TYRA_ASSERT(settings.isHybridOutput() && !settings.isFieldRendering() && !blss.isEnabled(),"A38 unsupported raster");
  completePipelineFrame();
  TYRA_ASSERT(gs.selectDiagnosticTarget(0),"A38 pending presentation drain");
  path3Fence();sync.align2D();if(path1.isVU1Configured())sync.align3D();
  settings.setTripleBuffering(on);rebuildPermanentBuffers();hasPresentedFrame=false;
  TYRA_ASSERT(gs.getFrameBufferCount()==(on?3:2),"A38 requested buffer not granted");
}
}
''',encoding='utf-8')
gs=engine/'src/renderer/core/gs/renderer_core_gs.cpp'
edit(gs,'#include "renderer/core/gs/renderer_core_gs.hpp"','#include "renderer/core/gs/renderer_core_gs.hpp"\n#include "debug/triple_trial.hpp"')
edit(gs,'void RendererCoreGS::presentFrameBuffer(u8 index) {','void RendererCoreGS::presentFrameBuffer(u8 index) {\n  TripleTrial::record(index);')
edit(gs,'  presentFrameBuffer(static_cast<u8>(queued));','  TripleTrial::inVBlank=true;presentFrameBuffer(static_cast<u8>(queued));TripleTrial::inVBlank=false;')
physics=out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'#include "debug/pc_moving.hpp"','#include "debug/triple_trial.hpp"')
edit(physics,'''    if(arm==1)PcMoving::start(MotionProbe::phase,1,MotionProbe::phase%2?1733:997);
    const unsigned stopped=PcMoving::callbacks;
    for(unsigned frame=0;frame<32;++frame){
      PcMoving::mark(frame,8);''',
 '''    core.setDiagnosticTriple(arm==1,true);setupProjShadows();
    printf("TRIPLEHELD phase=%u arm=%u actual=%u\\n",MotionProbe::phase,arm,core.gs.getFrameBufferCount());fflush(stdout);
    for(unsigned frame=0;frame<32;++frame){''')
edit(physics,'''    if(arm==1){PcMoving::stop();PcMoving::exportRows();}
    else TYRA_ASSERT(PcMoving::callbacks==stopped,"A37 callbacks in off return");
''','')
edit(physics,'  TerrainFocus::checkpoint=false;WorldRuntime::cold=false;FastJoin::checkpoint=false;ClipTrial::mode=active;g_frameDt=dt;',
 '  core.setDiagnosticTriple(MotionProbe::phase%3==1,true);setupProjShadows();\n  TerrainFocus::checkpoint=false;WorldRuntime::cold=false;FastJoin::checkpoint=false;ClipTrial::mode=active;g_frameDt=dt;')
(out/'triple-manifest.json').write_text(json.dumps(dict(experiment='A38',parentElf=b['elfSha256'],source=str(source),
 phases=[2,3,2,2,3,2],held=[2,3,2],scope='Net current Hybrid pacing incl real VRAM pressure, no sampler, no quality omission'),indent=2)+'\n')
print(out)
