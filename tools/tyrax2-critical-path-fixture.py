"""Build a six-trip, observer-controlled critical-path census without offline terrain."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path);p.add_argument('source',type=Path)
    a=p.parse_args();repo=Path(__file__).resolve().parent
    b=json.loads((a.source/'native-binding.json').read_text())
    for n,h in b['files'].items():assert sha(a.source/n)==h,n
    assert sha(a.source/'game/bin/vehicle-playground.elf')==b['elfSha256'] and not a.out.exists()
    def ignore(path,names):
        return [n for n in names if n in ('obj','.git','log.txt') or
                (Path(path).name=='bin' and (n.endswith(('.csv','.tga','.sym')) or
                 n.startswith(('tape-','command-','cell-','source-','moving-','world-price-','offline-'))))]
    for n in ('game','tyra/engine'):shutil.copytree(a.source/n,a.out/n,ignore=ignore)
    for n in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/n,a.out/n)
    shutil.copy2(repo/'tyrax2-critical-path.hpp',a.out/'tyra/engine/inc/debug/critical_path.hpp')
    def edit(n,old,new):
        f=a.out/n;t=f.read_text();assert t.count(old)==1,(n,old,t.count(old));f.write_text(t.replace(old,new))
    def include(n):
        f=a.out/n;f.write_text('#include "debug/critical_path.hpp"\n'+f.read_text())
    engine='tyra/engine/src/'
    e=engine+'engine.cpp';include(e)
    edit(e,'  MotionProbe::begin(renderer.core.getStallTotal());','  MotionProbe::begin(renderer.core.getStallTotal());\n  CriticalPath::begin(MotionProbe::phase,MotionProbe::offset,MotionProbe::gas());')
    edit(e,'game->loop();','CriticalPath::Scope measured(CriticalPath::Game); game->loop();')
    edit(e,'  MotionProbe::end(renderer.core.getStallTotal());','  CriticalPath::end();\n  MotionProbe::end(renderer.core.getStallTotal());')
    game='game/src/terrain_game.cpp';include(game)
    edit(game,'  if(MotionProbe::phase<6 && MotionProbe::offset==1099){OfflineTerrain::load();auditOfflineTerrain();}','  // Offline terrain is disabled and never loaded in this census.')
    edit(game,'  MotionProbe::renderBegin();','  CriticalPath::mark(CriticalPath::UpdateDone);\n  MotionProbe::renderBegin();')
    edit(game,'AudioIdle::set(true);OfflineTerrain::enabled=arm==1?!movingOffline:movingOffline;','AudioIdle::set(true);OfflineTerrain::enabled=false;')
    moving='game/inc/world_moving.hpp';include(moving)
    edit(moving,'OfflineTerrain::enabled=p<6 && modes[p];','OfflineTerrain::enabled=false;')
    edit(moving,'variant=offline-terrain\\n",p,unsigned(OfflineTerrain::enabled));','variant=critical-path\\n",p,unsigned(p<6 && modes[p]));')
    edit(moving,'inline void finish(){','inline void finish(){\n  CriticalPath::dump(phase);')
    physics='game/src/gen/game_physics.gen.cpp';include(physics)
    scopes={
      'void TerrainGame::renderScene() {':'Scene',
      'void TerrainGame::buildTerrainChunk(int slot, int cx, int cz) {':'TerrainBuild',
      'void TerrainGame::renderTerrain() {':'(CriticalPath::view?CriticalPath::TerrainReflection:CriticalPath::TerrainMain)',
    }
    for marker,bucket in scopes.items():
        expr=bucket if bucket.startswith('(') else 'CriticalPath::'+bucket
        edit(physics,marker,marker+'\n  CriticalPath::Scope measured('+expr+');')
    # Existing clocks are enabled only for the sample; existing cost barriers
    # remain disarmed. No render-cost request is created by this observer.
    edit(physics,'return (costSeq || (Tyra::HardwareTrace::active && Tyra::HardwareTrace::detailed)) ? profTicks() : 0;',
         'return (CriticalPath::active || costSeq || (Tyra::HardwareTrace::active && Tyra::HardwareTrace::detailed)) ? profTicks() : 0;')
    for label,bucket in [('Reflections_shared_probe','Reflection'),('Terrain','TerrainUpdate'),('Objects','Objects'),('Wheels','Wheels'),('Particles','Particles')]:
        # Every selected label has one top-level site in renderScene.
        import re
        f=a.out/physics;t=f.read_text();pat=r'costEnd\("'+label+r'",\s*-1,\s*([A-Za-z0-9_]+)\);'
        matches=list(re.finditer(pat,t));assert len(matches)==1,(label,len(matches))
        old=matches[0].group();start=matches[0].group(1)
        edit(physics,old,'if(CriticalPath::active){CriticalPath::current.elapsed[CriticalPath::'+bucket+']+=CriticalPath::ticks()-'+start+';++CriticalPath::current.calls[CriticalPath::'+bucket+'];} '+old)
    edit(physics,'    const u32 costSharedEnvStart = costStart();','    CriticalPath::View reflectionView(1);\n    const u32 costSharedEnvStart = costStart();')
    roads='game/src/gen/game_vehicles.gen.cpp';include(roads)
    edit(roads,'void TerrainGame::renderRoadChunks() {','void TerrainGame::renderRoadChunks() {\n  CriticalPath::Scope measured(CriticalPath::view?CriticalPath::RoadsReflection:CriticalPath::RoadsMain);')
    core=engine+'renderer/3d/pipeline/static/core/stapip_core.cpp';include(core)
    edit(core,'void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {',
         'void StaPipCore::render(StaPipBag* bag, StaPipLightReception reception) {\n  CriticalPath::Scope measured(CriticalPath::view?CriticalPath::CoreReflection:CriticalPath::CoreMain);')
    vif=engine+'renderer/core/paths/path1/vif1_queue.cpp';include(vif)
    edit(vif,'void Vif1Queue::start(u32 chain, u32 sequence) {','void Vif1Queue::start(u32 chain, u32 sequence) {\n  CriticalPath::launch(sequence);')
    edit(vif,'void Vif1Queue::waitFor(u32 sequence) {','void Vif1Queue::waitFor(u32 sequence) {\n  CriticalPath::Scope measured(CriticalPath::VifWait);')
    edit(vif,'  if (!nativePendingSequence) return;','  if (!nativePendingSequence) return;\n  CriticalPath::Scope measured(CriticalPath::NativeFlush);')
    sync=engine+'renderer/core/renderer_core_sync.cpp';include(sync)
    edit(sync,'void RendererCoreSync::waitAndClear() {','void RendererCoreSync::waitAndClear() {\n  CriticalPath::Scope measured(CriticalPath::GsWait);')
    renderer=engine+'renderer/core/renderer_core.cpp';include(renderer)
    edit(renderer,'  pipelineFramePending = false; // waits must not recursively present this job','  CriticalPath::Scope measured(CriticalPath::Pending);\n  CriticalPath::pending(pipelineSequence);\n  pipelineFramePending = false; // waits must not recursively present this job')
    edit(renderer,'Vif1Queue::waitFor(pipelineSequence); }','Vif1Queue::waitFor(pipelineSequence); }\n  CriticalPath::mark(CriticalPath::PendingVifDone);')
    # Match only this function; leave all ordinary GS ownership handshakes intact.
    edit(renderer,'  sync.waitAndClear();\n  HardwareTrace::Scope tracePacing', '  sync.waitAndClear();\n  CriticalPath::mark(CriticalPath::PendingGsDone);\n  HardwareTrace::Scope tracePacing')
    edit(renderer,'  if (gs.getFrameBufferCount() < 3 && isFrameLimitOn) graph_wait_vsync();',
         '  {CriticalPath::Scope measured(CriticalPath::Vsync); if (gs.getFrameBufferCount() < 3 && isFrameLimitOn) graph_wait_vsync();}\n  CriticalPath::mark(CriticalPath::VsyncDone);')
    edit(renderer,'  CriticalPath::mark(CriticalPath::VsyncDone);\n  gs.flipBuffers(isFrameLimitOn);','  CriticalPath::mark(CriticalPath::VsyncDone);\n  {CriticalPath::Scope measured(CriticalPath::Flip);gs.flipBuffers(isFrameLimitOn);}\n  CriticalPath::mark(CriticalPath::FlipDone);')
    m=json.loads((a.source/'world-cell-price-manifest.json').read_text())
    m.update(variant='critical-path',status='UNQUALIFIED',criticalPathObserver=True,offlineTerrainDisabled=True,
             criticalHeaderSha256=sha(repo/'tyrax2-critical-path.hpp'),parentElfSha256=b['elfSha256'])
    m['limits']+=['Observer Off/On comparison, not an optimization. Inclusive nested scopes must not be added.',
      'Current common terrain builder, inherited qualified drive/residency/private renderer; not full current-production coverage.',
      'No offline tables are loaded; packet/material/quality settings unchanged. Hardware snapshots are boundary states, not utilization.']
    (a.out/'world-cell-price-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    print(a.out)


if __name__=='__main__':main()
