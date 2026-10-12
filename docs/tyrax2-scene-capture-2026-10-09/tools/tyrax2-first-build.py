"""A02a: bounded first-build events on the P06 unchanged road workload."""
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
out, source = [Path(p).resolve() for p in sys.argv[1:3]]
subprocess.run([sys.executable, str(ROOT/'tools/tyrax2-road-trace.py'), str(out), str(source)],
               check=True, env={**os.environ, 'PYTHONUTF8': '1'})
engine = out/'tyra/engine'


def edit(path, old, new):
    text = path.read_text(encoding='utf-8-sig')
    assert text.count(old) == 1, (path, old, text.count(old))
    path.write_bytes(text.replace(old, new).encode())


h = engine/'inc/debug/motion_probe.hpp'
edit(h, 'struct Record { U offset,id,view,vertices,elapsed,wait,c[Count]; };', '''
enum BuildMetric { BakeTicks, FinalizeTicks, BakeCalls, FinalizeCalls,
  PayloadQwords, HeaderQwords, GrowthBlocks, CapacityAddedQwords,
  ArenaBytes, ArenaBuilds, BuildMetricCount };
inline const char* buildNames="bakeTicks,finalizeTicks,bakeCalls,finalizeCalls,payloadQwords,headerQwords,growthBlocks,capacityAddedQwords,arenaBytes,arenaBuilds";
struct Record { U offset,id,view,vertices,elapsed,wait,c[Count],t[TimingCount],b[BuildMetricCount]; };''')
edit(h, 'inline Record* currentRecord=nullptr;', '''inline Record* currentRecord=nullptr;
inline void buildMetric(BuildMetric m,U n=1){if(currentRecord)currentRecord->b[m]+=n;}
struct BuildScope {
  Record* row; U stamp=0; BuildMetric kind;
  explicit BuildScope(BuildMetric k):row(currentRecord),kind(k){
    if(row){stamp=ticks();++row->b[k==BakeTicks?BakeCalls:FinalizeCalls];}
  }
  ~BuildScope(){if(row)row->b[kind]+=ticks()-stamp;}
};''')
edit(h, '    currentRecord=row;start=ticks();',
     '    memcpy(row->t,timing,sizeof(timing));\n    currentRecord=row;start=ticks();')
edit(h, '  ~Bag(){if(row){row->elapsed=ticks()-start;currentRecord=nullptr;}}', '''  ~Bag(){if(row){
    row->elapsed=ticks()-start;
    for(U i=0;i<TimingCount;++i)row->t[i]=timing[i]-row->t[i];
    currentRecord=nullptr;
  }}''')
edit(h, '  fprintf(bagsFile,"\\n");', '''  for(U i=0;i<TimingCount;++i)fprintf(bagsFile,",t%u",i);
  fprintf(bagsFile,",%s\\n",buildNames);''')
edit(h, '      for(U v:r.c)fprintf(bagsFile,",%u",v);fprintf(bagsFile,"\\n");}', '''      for(U v:r.c)fprintf(bagsFile,",%u",v);
      for(U v:r.t)fprintf(bagsFile,",%u",v);
      for(U v:r.b)fprintf(bagsFile,",%u",v);
      fprintf(bagsFile,"\\n");}''')
q = engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(q, '  const u32 startOut = static_cast<u32>(bakeScratch.size());', '''  MotionProbe::RoadTrace::BuildScope firstBuild(MotionProbe::RoadTrace::BakeTicks);
  const u32 beforeCapacity=static_cast<u32>(bakeScratch.capacity());
  const u32 startOut = static_cast<u32>(bakeScratch.size());''')
edit(q, '    for (u32 q = 0; q < qwc; ++q) bakeScratch.push_back(payload[q]);', '''    for (u32 q = 0; q < qwc; ++q) bakeScratch.push_back(payload[q]);
    MotionProbe::RoadTrace::buildMetric(MotionProbe::RoadTrace::PayloadQwords,qwc);
    MotionProbe::RoadTrace::buildMetric(MotionProbe::RoadTrace::HeaderQwords);''')
edit(q, '  const u32 emitted = static_cast<u32>(bakeScratch.size()) - startOut;', '''  if(bakeScratch.capacity()!=beforeCapacity){
    MotionProbe::RoadTrace::buildMetric(MotionProbe::RoadTrace::GrowthBlocks);
    MotionProbe::RoadTrace::buildMetric(MotionProbe::RoadTrace::CapacityAddedQwords,
      static_cast<u32>(bakeScratch.capacity())-beforeCapacity);
  }
  const u32 emitted = static_cast<u32>(bakeScratch.size()) - startOut;''')
edit(q, 'void StaPipQBufferRenderer::endBakedBag() {\n  StaPipBakedEntry* entry', '''void StaPipQBufferRenderer::endBakedBag() {
  MotionProbe::RoadTrace::BuildScope finishBuild(MotionProbe::RoadTrace::FinalizeTicks);
  StaPipBakedEntry* entry''')
edit(q, '      memcpy(entry->arena, bakeScratch.data(), qw * 16);', '''      memcpy(entry->arena, bakeScratch.data(), qw * 16);
      MotionProbe::RoadTrace::buildMetric(MotionProbe::RoadTrace::ArenaBytes,qw*16);
      MotionProbe::RoadTrace::buildMetric(MotionProbe::RoadTrace::ArenaBuilds);''')
spec = importlib.util.spec_from_file_location('quiet', ROOT/'tools/tyrax2-quiet-fixture.py')
quiet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quiet)
(out/'first-build-manifest.json').write_text(json.dumps(dict(
    experiment='A02a-first-build-events', drawingChanged=False,
    scope='P06 road windows; bake and finalize elapsed, not pure EE; growthBlocks is not allocation count',
    files=quiet.source_inputs(out, 'vehicle-playground.tyra')), indent=2)+'\n')
print(out)
