"""Private P06 per-road windows and whole-frame explicit wait/transport ledger."""
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
out,source=[Path(p).resolve() for p in sys.argv[1:3]]
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-road-prep.py'),str(out),str(source),'--prefix'],
               check=True,env={**os.environ,'PYTHONUTF8':'1'})
engine=out/'tyra/engine';game=out/'game'

def edit(path,old,new,count=1):
    text=path.read_text(encoding='utf-8-sig')
    assert text.count(old)==count,(path,old,text.count(old),count)
    path.write_bytes(text.replace(old,new).encode())

def include(path):
    if '#include "debug/motion_probe.hpp"' not in path.read_text(encoding='utf-8-sig'):
        path.write_bytes(b'#include "debug/motion_probe.hpp"\n'+path.read_bytes())

header=engine/'inc/debug/motion_probe.hpp'
edit(header,'BarrierOmitted, EnvEligible','BarrierOmitted, CullChunks, CullVerts, ClipChunks, ClipVerts, EnvEligible')
edit(header,'barrierOmitted,envEligible','barrierOmitted,cullChunks,cullVerts,clipChunks,clipVerts,envEligible')
edit(header,'inline void add(Counter c,U n=1) {',
     (ROOT/'tools/tyrax2-road-trace.hpp').read_text()+'\ninline void add(Counter c,U n=1) {\n  RoadTrace::note(c,n);')
edit(header,'inline void init() {','inline void init() {\n  RoadTrace::open();')
edit(header,'  memset(timing,0,sizeof(timing));','  RoadTrace::begin();\n  memset(timing,0,sizeof(timing));')
edit(header,'    memcpy(r.t,timing,sizeof(timing));','    memcpy(r.t,timing,sizeof(timing));\n    RoadTrace::save(size-1);')
edit(header,'  } else if(finishPending) {','  } else if(finishPending) {\n    RoadTrace::dump();')
edit(game/'inc/terrain_config.hpp','constexpr int INTERLEAVE_PASSES = 1;','constexpr int INTERLEAVE_PASSES = 2;')
edit(engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp',
     '  bool diagnosticRoad = false;','  unsigned diagnosticRoadId = 0;\n  bool diagnosticRoad = false;')
edit(game/'src/gen/game_collision.gen.cpp','    c.bag->diagnosticRoad = c.owner == -3;',
     '    c.bag->diagnosticRoad = c.owner == -3;\n    c.bag->diagnosticRoadId=unsigned(&c-procChunks.data())+1;')
edit(engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp',
     '  MotionProbe::Scope probeCore(false, bag->diagnosticRoad);',
     '  MotionProbe::RoadTrace::Bag roadRecord(bag->diagnosticRoad,bag->diagnosticRoadId,bag->count);\n  MotionProbe::Scope probeCore(false, bag->diagnosticRoad);')
physics=game/'src/gen/game_physics.gen.cpp'
for signature,view in [('void TerrainGame::renderRtMirror(const MirrorData& mir) {',1),
                       ('void TerrainGame::renderObjectProbe(int index) {',1),
                       ('void TerrainGame::renderCameraFeed() {',2),
                       ('void TerrainGame::renderPortalView() {',2)]:
    edit(physics,signature,signature+f'\n  MotionProbe::RoadTrace::Camera traceView({view});')
edit(physics,'    const u32 costSharedEnvStart = costStart();',
     '    MotionProbe::RoadTrace::Camera traceView(1);\n    const u32 costSharedEnvStart = costStart();')
edit(game/'inc/terrain_game.hpp','std::vector<Tyra::StaPipBag*> heavyBags;',
     'std::vector<Tyra::StaPipBag*> heavyBags;\n  std::vector<unsigned> roadTraceViews;')
vehicles=game/'src/gen/game_vehicles.gen.cpp'
edit(vehicles,'  if (heavyCollect)\n    heavyBags.push_back(bag);',
     '  if (heavyCollect) { heavyBags.push_back(bag); roadTraceViews.push_back(MotionProbe::RoadTrace::view); }')
edit(vehicles,'  heavyBags.clear();','  heavyBags.clear(); roadTraceViews.clear();')
edit(vehicles,'  for (size_t k = 0; k < want; ++k) renderWorldBag(heavyBags[heavyNext++]);',
     '''  for(size_t k=0;k<want;++k) {
    TYRA_ASSERT(roadTraceViews.size()==heavyBags.size(),"Road trace view ownership");
    MotionProbe::RoadTrace::Camera traceView(roadTraceViews[heavyNext]);
    renderWorldBag(heavyBags[heavyNext++]);
  }''')
wait_sites=[]
for rel,needles in [
 ('src/renderer/core/paths/path1/vif1_queue.cpp',[
  ('dma_channel_wait(DMA_CHANNEL_VIF1, 0);',3,'VifWait'),
  ('while (tail - head >= kDepth) advanceIfIdle();',1,'VifWait'),
  ('while (tail - head >= kRingDepth) advanceIfIdle();',1,'VifWait'),
  ('while (static_cast<s32>(completed - sequence) < 0) advanceIfIdle();',1,'VifWait'),
  ('while (running) advanceIfIdle();',1,'VifWait'),
  ('while (*stat & 0x1f000003) {}',1,'VifWait')]),
 ('inc/renderer/core/paths/path1/frame_submission.hpp',[
  ('dma_channel_wait(channel, timeout);',1,'OtherWait'),
  ('dma_channel_wait(channel, 0);',2,'OtherWait'),
  ('draw_wait_finish();',1,'GsWait'),
  ('while (*stat & 0x1f000003) {}',1,'VifWait')]),
 ('src/renderer/core/renderer_core_sync.cpp',[('while (!check()) {\n  }',1,'GsWait')]),
 ('src/renderer/core/renderer_core.cpp',[('while (*vif1Stat & kVif1Busy) {\n    }',1,'VifWait')])]:
    path=engine/rel;include(path)
    for needle,count,kind in needles:
        edit(path,needle,'{ MotionProbe::RoadTrace::Wait traceWait(MotionProbe::RoadTrace::'+kind+'); '+needle+' }',count)
        wait_sites.append(dict(file=rel,source=needle,count=count,kind=kind))
queue=engine/'src/renderer/core/paths/path1/vif1_queue.cpp'
for old,new,count in [
 ('u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {',
  '\n  MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::SubmitCalls);\n  MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::SubmitQwords,qwords);',1),
 ('  const u32 qwords = nativeWriter->finish();',
  '\n  MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::FlushCalls);\n  MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::FlushQwords,qwords);',1),
 ('    if (records > nativeWriter->availableQwords()) {',
  '\n      MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::CapacityFlush);',1),
 ('  if (required > nativeWriter->availableQwords()) {',
  '\n    MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::CapacityFlush);',1),
 ('  if (nativeInFlight || !nativeWriter->availableQwords()) {',
  '\n    MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::BarrierFlush);',1),
 ('      ++arenaResets;', '\n      MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::ArenaRetry);',2),
 ('  // Unsupported/unowned or oversized operations retain immediate ordering.',
  '\n  MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::FallbackFlush);',1),
 ('void Vif1Queue::drain() {','\n  MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::DrainCalls);',1),
 ('      waitFor(bankSequences[currentBank]);','\n      MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::BankWaitCalls);',1),
 ('void Vif1Queue::waitFor(u32 sequence) {','\n  MotionProbe::RoadTrace::metric(MotionProbe::RoadTrace::SequenceWaitCalls);',1)]:
    edit(queue,old,old+new,count)
edit(game/'src/terrain_game.cpp','    printf("LOG: TURNPINS phase=',
     '    printf("LOG: ROADTRACE phase=%u interleave=%u normalDrawing=1\\n",MotionProbe::phase,unsigned(INTERLEAVE_PASSES));\n    printf("LOG: TURNPINS phase=')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core,'    if(replayedWhole) MotionProbe::add(MotionProbe::Replay);', '''    if(replayedWhole) {
      MotionProbe::add(MotionProbe::Replay);
      MotionProbe::add(MotionProbe::CullChunks,(bag->count+maxVertCount-1)/maxVertCount);
      MotionProbe::add(MotionProbe::CullVerts,bag->count);
    }''')
q=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(q,'    for (u32 t = 0; t < pkg.size - 2u; t += triangles) {',
     '    MotionProbe::add(MotionProbe::ExpandBuilds);\n    for (u32 t = 0; t < pkg.size - 2u; t += triangles) {')
edit(q,'  buffer->fillByPointer(cached);',
     '  MotionProbe::add(MotionProbe::ExpandCachedVerts,count);\n  buffer->fillByPointer(cached);')
edit(q,'  lastProgramName = program->getName();\n  baked.countHit();',
     '  lastProgramName = program->getName();\n  MotionProbe::add(MotionProbe::PartialReplay);\n  MotionProbe::add(MotionProbe::PartialBytes,(size-4)*16);\n  baked.countHit();')
edit(q,'  const prim_t savedPrim = *prim;',
     '  MotionProbe::add(MotionProbe::PartialBuild);\n  const prim_t savedPrim = *prim;')
for name,prefix in [('cull','Cull'),('clip','Clip')]:
    text=q.read_text(encoding='utf-8-sig');first,last=quiet.body_span(text,'StaPipQBufferRenderer::'+name)
    body=text[first:last];needle='if (buffer->size == 0) {\n    return;\n  }'
    assert body.count(needle)==1
    body=body.replace(needle,needle+'\n  MotionProbe::add(MotionProbe::'+prefix+'Chunks);\n  MotionProbe::add(MotionProbe::'+prefix+'Verts,buffer->size);')
    q.write_bytes((text[:first]+body+text[last:]).encode())
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='road-window-wait-trace',modes=[0,6,0],
 fixedInterleave=2,normalRoadDrawing=True,constantVuPaint=True,windows=[[12,20],[24,32],[180,188]],
 waitSites=wait_sites,scope='wait leaves whole frame; per-road counters include mixed triggered queue work',
 files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
