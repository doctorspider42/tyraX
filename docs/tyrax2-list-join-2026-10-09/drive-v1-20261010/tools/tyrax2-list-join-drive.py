"""A23 moving ordinary-source adjacent-list A/B, day/night and both orders."""
import argparse
import hashlib
import importlib.util
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():
    assert hashlib.sha256((a.source/rel).read_bytes()).hexdigest()==h,rel
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
for part in ('game','tyra/engine'):
    shutil.copytree(a.source/part,a.out/part,ignore=q.ignore)
shutil.copy2(a.source/'tyra/Makefile.base',a.out/'tyra/Makefile.base')
shutil.copy2(a.source/'authored-manifest.json',a.out/'authored-manifest.json')
engine=a.out/'tyra/engine'
for src,dst in [('tyrax2-list-join.hpp','list_join.hpp'),('tyrax2-list-join-fast.hpp','list_join_fast.hpp')]:
    shutil.copy2(repo/'tools'/src,engine/'inc/debug'/dst)

def edit(path,old,new):
    text=path.read_text(encoding='utf-8-sig')
    assert text.count(old)==1,(path,old,text.count(old))
    path.write_text(text.replace(old,new),encoding='utf-8')

target_method='''bool RendererCoreGS::selectDiagnosticTarget(u8 target) {
  if((bufferCount!=2 && bufferCount!=3) || target>=bufferCount ||
     settings->isFieldRendering() || rasterRedirected)return false;
  while(pendingBuffer>=0)graph_wait_vsync();
  if(settings->isHybridOutput()){
    if(target!=0)return false;
    context=0;emitDrawTargetSwitch(0);return true;
  }
  DIntr();context=target;displayedBuffer=(target+1)%bufferCount;
  presentFrameBuffer(static_cast<u8>(displayedBuffer));EIntr();
  emitDrawTargetSwitch(target);return true;
}

'''
edit(engine/'inc/renderer/core/gs/renderer_core_gs.hpp',
     '  u8 getRecordingContext() const { return context; }',
     '  u8 getRecordingContext() const { return context; }\n  bool selectDiagnosticTarget(u8 target);')
edit(engine/'src/renderer/core/gs/renderer_core_gs.cpp',
     '}  // namespace Tyra',target_method+'}  // namespace Tyra')

edit(engine/'inc/debug/list_join_fast.hpp','inline bool active = false, oracle = false;',
     '''inline bool active = false, oracle = false, checkpoint = false, interleaved = false;
struct Witness { unsigned frames=0,calls=0,accepted=0,bytes=0; };
inline Witness witnesses[12]{};''')
# Leave the rejected target gate disabled in every arm.
edit(engine/'inc/debug/target_audit.hpp','return MotionProbe::variant()==6;','return false;')
edit(engine/'inc/debug/target_audit.hpp','inline unsigned tested=0,rejected=0;','')
probe=engine/'inc/debug/motion_probe.hpp'
edit(probe,'namespace TargetAudit { extern unsigned tested,rejected; }',
     '#include "debug/list_join_fast.hpp"\nnamespace TargetAudit { inline unsigned tested=0,rejected=0; }')
edit(probe,'inline constexpr U phaseCount=6;','inline constexpr U phaseCount=12;')
edit(probe,'constexpr U modes[]={0,6,0,0,6,0};','constexpr U modes[]={0,6,0,6,0,6,0,6,0,6,0,6};')
edit(probe,'  current.driver=-1;', '''  current.driver=-1;
  FastJoin::active=variant()==6;
  FastJoin::oracle=FastJoin::active && !finishPending && offset>=gasStart && offset%128==64;
  ListJoin::reset(FastJoin::oracle);''')
edit(probe,'    printf("TARGETCOUNTS phase=', '''    const auto& join=FastJoin::witnesses[phase];
    printf("JOINWITNESS phase=%u frames=%u calls=%u accepted=%u bytes=%u\\n",phase,join.frames,join.calls,join.accepted,join.bytes);
    printf("TARGETCOUNTS phase=''')
edit(probe,'  enabled=false;clocks=false;++frame;++offset;', '''  if(FastJoin::oracle){auto& w=FastJoin::witnesses[phase];
    ++w.frames;w.calls+=ListJoin::calls;w.accepted+=ListJoin::accepted;w.bytes+=ListJoin::bytes;}
  enabled=false;clocks=false;++frame;++offset;''')
path=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp'
edit(path,'#include <string.h>','#include <string.h>\n#include "debug/list_join_fast.hpp"')
for name,count,following in [('fillByCopyMax',3,'fillByCopy1By2'),('fillByCopy1By2',2,'fillByCopy1By3')]:
    text=path.read_text();start=text.index('void StaPipQBuffer::'+name+'(')
    end=text.index('void StaPipQBuffer::'+following+'(',start);body=text[start:end]
    declaration=f'  const StaPipBagPackage* pkgs[{count}] = {{'+', '.join('&pkg'+str(i+1) for i in range(count))+'};'
    assert body.count(declaration)==1
    body=body.replace(declaration+'\n','').replace('  deallocateDynamicData();',
         declaration+'\n'+f'  if (FastJoin::apply(*this, pkgs, {count})) return;\n  deallocateDynamicData();',1)
    body=body.replace('  bag = pkg1.bag;\n}',f'  bag = pkg1.bag;\n  ListJoin::checkAndApply(*this, pkgs, {count});\n'+'}')
    path.write_text(text[:start]+body+text[end:])
game=a.out/'game/src/terrain_game.cpp'
edit(game,'void TerrainGame::loop() {','''void TerrainGame::loop() {
  FastJoin::active=MotionProbe::variant()==6;
  if(MotionProbe::finishPending) renderJoinCheckpoint();''')
edit(game,'MotionProbe::phase >= 3','MotionProbe::phase >= 6')
edit(game,'MotionProbe::phase>=3 ? lights==0 : lights!=0','MotionProbe::phase>=6 ? lights==0 : lights!=0')
edit(game,'MotionProbe::phase>=3?0.0F:12.0F','MotionProbe::phase>=6?0.0F:12.0F')
edit(a.out/'game/inc/terrain_game.hpp','  void renderScene();','  void renderScene();\n  void renderJoinCheckpoint();')
vehicles=a.out/'game/src/gen/game_vehicles.gen.cpp'
vehicles.write_text('#include "debug/list_join_fast.hpp"\n'+vehicles.read_text())
edit(vehicles,'  heavyDrawn = 0;\n  if (INTERLEAVE_PASSES == 0 || splitPassActive) {',
     '  heavyDrawn = 0;\n  if (FastJoin::checkpoint) return FastJoin::interleaved;\n  if (INTERLEAVE_PASSES == 0 || splitPassActive) {')
physics=a.out/'game/src/gen/game_physics.gen.cpp'
physics.write_text('#include "debug/list_join_fast.hpp"\n#include <screenshot.h>\n'+physics.read_text())
edit(physics,'  if (sharedEnvGeneration != sceneGeneration) {',
     '  if (FastJoin::checkpoint) sharedEnvBasisValid=false;\n  if (sharedEnvGeneration != sceneGeneration) {')
edit(physics,'void TerrainGame::renderScene() {', '''void TerrainGame::renderJoinCheckpoint() {
  auto& core=engine->renderer.core;
  const bool active=FastJoin::active;
  const float dt=g_frameDt;
  FastJoin::checkpoint=true;FastJoin::interleaved=heavyActive;
  FastJoin::oracle=false;ListJoin::reset(false);g_frameDt=0;
  core.synchronizeFrame();core.sync.align2D();
  const unsigned target=core.gs.getRecordingContext();
  for(unsigned arm=0;arm<3;++arm){
    FastJoin::active=arm==1;
    for(unsigned frame=0;frame<8;++frame){
      core.synchronizeFrame();
      TYRA_ASSERT(core.gs.selectDiagnosticTarget(uint8_t(target)),"Cannot pin join target");
      beamBatchCall=0;
      engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));
      engine->renderer.renderer3D.usePipeline(stapip);
      renderScene();engine->renderer.endFrame();core.synchronizeFrame();core.sync.align2D();
    }
    for(unsigned env=0;env<2;++env){
      auto* fb=core.gs.getPreviousRealFrameBuffer();
      unsigned address=fb->address,width=fb->width,height=fb->height,psm=fb->psm;
      if(env){auto* texture=core.envMap.getTexture()->vramResident;
        TYRA_ASSERT(texture,"Missing live env texture");address=texture->address;width=height=128;psm=texture->psm;}
      TYRA_ASSERT((psm==0 || psm==2) && width<=1024,"Unsupported join readback");
      const unsigned stage=MotionProbe::phase*3+arm;
      char path[80];snprintf(path,sizeof(path),"host:tape-%u-%s.bin",stage,env?"env":"main");
      FILE* file=fopen(path,"wb");TYRA_ASSERT(file,"Join readback open");
      unsigned header[4]={width,height,psm,stage};TYRA_ASSERT(fwrite(header,sizeof(header),1,file)==1,"Join readback header");
      alignas(16) static unsigned line[1024];
      for(unsigned y=0;y<height;++y){
        TYRA_ASSERT(ps2_screenshot(line,address/64,0,y,width,1,psm),"Join GS readback");
        FlushCache(0);const unsigned stride=psm==2?2:4;
        TYRA_ASSERT(fwrite(line,stride,width,file)==width,"Join readback pixels");
      }
      TYRA_ASSERT(fclose(file)==0,"Join readback close");
    }
  }
  printf("JOINCHECKPOINT phase=%u target=%u z=%f speed=%f arms=3\\n",MotionProbe::phase,target,MotionProbe::current.z,MotionProbe::current.speed);fflush(stdout);
  FastJoin::checkpoint=false;FastJoin::active=active;g_frameDt=dt;
}

void TerrainGame::renderScene() {''')
(a.out/'list-join-drive-manifest.json').write_text(json.dumps(dict(
    experiment='A23',source=str(a.source),sourceElf=binding['elfSha256'],
    modes=[0,6,0,6,0,6]*2,hours=[12]*6+[0]*6,
    sampleStart=1200,gasStart=1320,endZ=108,
    oracle='On moving offset%128==64; exclude this frame and next frame from pricing',
    raster='After each live trip, current producer state frozen for O/J/O main/env readbacks',
    scope='Ordinary moving producers with real dt, roads and live reflections; same-ELF forward/reverse triples'),indent=2)+'\n')
print(a.out)
