"""A44 real terrain cell producer/submission, correctness-only before physical price."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
assert binding['requiredSymbol']=='Tyra::StaPipQBufferRenderer::beginCompleteWorldView()'
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (n.startswith(('tape-','command-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
def once(text,old,new):
    assert text.count(old)==1,old
    return text.replace(old,new)
engine=a.out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-world-cell-complete.hpp',engine/'inc/debug/world_cell_complete.hpp')
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
t=renderer.read_text()
# Replace only this backend with the audited current compiler/delivery gate.
start=t.index('bool StaPipQBufferRenderer::compileCompleteWorldCell(')
end=t.index('bool StaPipQBufferRenderer::prepareWorldGeometry(',start)
t=t[:start]+(repo/'tools/tyrax2-world-cell-complete-methods.inc').read_text()+'\n'+t[end:]
t=once(t,'  lastProgramName=cell.lastProgram;++submissionBatchBags;',
       '  lastProgramName=cell.lastProgram;++submissionBatchBags;\n  ++WorldCellComplete::submitted;')
renderer.write_text(t)
core=engine/'inc/renderer/3d/pipeline/static/core/stapip_core.hpp'
t=core.read_text();t=once(t,'  void collectWorldOwners(){qbufferRenderer.collectWorldOwners();}', '''
  bool compileCompleteWorldCell(WorldCellComplete::Cell& c,const std::vector<StaPipBag*>& b){
    return qbufferRenderer.compileCompleteWorldCell(c,b);}
  bool beginCompleteWorldView(){return qbufferRenderer.beginCompleteWorldView();}
  bool renderCompleteWorldCell(WorldCellComplete::Cell& c){return qbufferRenderer.renderCompleteWorldCell(c);}
  void collectWorldOwners(){qbufferRenderer.collectWorldOwners();}''');core.write_text(t)
header=a.out/'game/inc/terrain_game.hpp'
t=header.read_text();t=once(t,'  struct TerrainChunk {','  struct TerrainChunk {\n    WorldCellComplete::Cell completeCell;')
t=once(t,'  void prepareWorldOwners();','''  void prepareWorldOwners();
  bool completeTerrainDirty=true,completeTerrainSlots=false;
  bool prepareCompleteTerrain();
  void releaseCompleteTerrain();
  void renderCompleteTerrainCheckpoint();''');header.write_text(t)
scene=a.out/'game/src/gen/game_scene.gen.cpp'
t=scene.read_text();t=once(t,'  currentScene = sceneIndex;','  releaseCompleteTerrain();\n  currentScene = sceneIndex;');scene.write_text(t)
physics=a.out/'game/src/gen/game_physics.gen.cpp'
t=physics.read_text()
t=once(t,'void TerrainGame::resetTerrainChunks() {','void TerrainGame::resetTerrainChunks() {\n  releaseCompleteTerrain();')
anchor='  TerrainChunk& ch = terrainChunks[slot];'
t=once(t,anchor,anchor+'\n  ch.completeCell.owner.reset();completeTerrainDirty=true;')
t=once(t,'void TerrainGame::renderTerrain() {','void TerrainGame::renderTerrain() {\n  bool completeView=false;')
t=once(t,'    renderWorldBag(ch.bag.get());','''    if(WorldCellComplete::active){
      ++WorldCellComplete::requested;
      if(ch.completeCell.owner){
        if(!completeView)completeView=stapip.core.beginCompleteWorldView();
        if(completeView && stapip.core.renderCompleteWorldCell(ch.completeCell))continue;
      }
      ++WorldCellComplete::fallback;
    }
    completeView=false;
    renderWorldBag(ch.bag.get());''')
# An additional full-scene held gate does not replace or waive inherited checks.
start=t.index('void TerrainGame::renderJoinCheckpoint() {')
end=t.index('\nvoid TerrainGame::renderScene() {',start)
old=t[start:end]
capture_start=old.index('    for(unsigned env=0;env<2;++env){')
capture_end=old.index('  printf("CLIPCHECKPOINT',capture_start)
capture=old[capture_start:capture_end]
capture=capture.replace('host:tape-%u-%s.bin','host:cell-%u-%s.bin')
new='''
void TerrainGame::renderCompleteTerrainCheckpoint(){
  if(MotionProbe::phase>=3)return;
  auto& core=engine->renderer.core;
  const float savedDt=g_frameDt;g_frameDt=0;
  core.synchronizeFrame();core.sync.align2D();
  const unsigned target=core.gs.getRecordingContext();
  TYRA_ASSERT(prepareCompleteTerrain(),"A44 complete terrain preparation failed");
  for(unsigned arm=0;arm<3;++arm){
    WorldCellComplete::active=arm==1;
    for(unsigned frame=0;frame<32;++frame){
      WorldCellComplete::requested=WorldCellComplete::submitted=WorldCellComplete::fallback=0;
      core.synchronizeFrame();
      TYRA_ASSERT(core.gs.selectDiagnosticTarget(uint8_t(target)),"A44 pin target failed");
      beamBatchCall=0;
      engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));
      engine->renderer.renderer3D.usePipeline(stapip);
      renderScene();engine->renderer.endFrame();core.synchronizeFrame();core.sync.align2D();
    }
    if(arm==1)TYRA_ASSERT(WorldCellComplete::requested>0 &&
      WorldCellComplete::submitted==WorldCellComplete::requested && !WorldCellComplete::fallback,
      "A44 missing complete-cell GPU activation");
    printf("A44ARM phase=%u arm=%u requested=%u submitted=%u fallback=%u\\n",
      MotionProbe::phase,arm,WorldCellComplete::requested,WorldCellComplete::submitted,WorldCellComplete::fallback);
    fflush(stdout);
'''+capture+'''
  WorldCellComplete::active=false;g_frameDt=savedDt;
  printf("A44GPU phase=%u arms=3 target=%u complete=1\\n",MotionProbe::phase,target);fflush(stdout);
  releaseCompleteTerrain();
  worldSeenEpoch=0;prepareWorldOwners();
}

'''
t=t[:end]+ '\n'+new+t[end:]
# Release before inherited diagnostic buffer rebuilding/texture eviction.
t=once(t,'void TerrainGame::renderJoinCheckpoint() {','void TerrainGame::renderJoinCheckpoint() {\n  releaseCompleteTerrain();')
physics.write_text(t)
game=a.out/'game/src/terrain_game.cpp'
t=game.read_text();t=once(t,'  if(MotionProbe::finishPending) renderJoinCheckpoint();',
    '  if(MotionProbe::finishPending){renderJoinCheckpoint();renderCompleteTerrainCheckpoint();}')
t+='\n'+(repo/'tools/tyrax2-world-cell-terrain-methods.inc').read_text()
game.write_text(t)
manifest={'experiment':'A44-terrain-complete-cell-GPU','baseElfSha256':binding['elfSha256'],
 'status':'UNQUALIFIED','movingCandidate':False,'tools':{n:sha(repo/'tools'/n) for n in
 ('tyrax2-world-cell-complete.hpp','tyrax2-world-cell-complete-methods.inc',
  'tyrax2-world-cell-terrain-methods.inc','tyrax2-world-cell-terrain-fixture.py')},
 'limits':['New route only in extra daytime full-scene image gate, not moving price.',
 'Full resident terrain texture set with explicit 25 percent dynamic allowance.',
 'Inherited gates retained unchanged; cell images require independent comparison.',
 'Old A31 terrain owners retired before compilation; held control uses ordinary frontend.',
 'Moving producer/rebuild and sustained texture pressure still require qualification.']}
(a.out/'world-cell-terrain-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED actual terrain-cell producer and GPU gate')
