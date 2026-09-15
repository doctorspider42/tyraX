"""Instrument an isolated benchmark AFTER --refresh-gen; build with native-build.

No engine edits. Store raw per-frame timings in RAM and write after sampling.
The finish bucket includes remaining renderer work; it is not a GS-only timer.
"""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('project', type=Path)
a = p.parse_args()
assert (a.project / 'BENCHMARK.json').exists(), 'Use an isolated benchmark fixture'
manifest = json.loads((a.project / 'vehicle-playground.tyra').read_text(encoding='utf-8'))
settings = manifest['settings']
assert not settings.get('blssEnabled'), 'Measure a fixed native raster'
assert not settings.get('blssAdaptive'), 'Adaptive raster changes the workload'
assert not settings.get('frameExtrapolation'), 'Presentation is measured once per loop'
path = a.project / 'src/terrain_game.cpp'
s = path.read_text(encoding='utf-8')
assert 'districtMeasure' not in s, 'Already instrumented'
start = s.index('void TerrainGame::loop() {')
end = s.index('\n// Adaptive plain BLSS', start)
loop = s[start:end]
helper = r'''
namespace districtMeasure {
static unsigned frame = 0, count = 0;
struct Row {
  unsigned frame, update, vehicles, submit, finish, present, total;
  unsigned bounds, prepare, dispatch, packet, dma, wait, flushes, triangles;
  unsigned uploads, reuploads;
};
static Row rows[960];
static unsigned ticks() { unsigned t; asm volatile("mfc0 %0, $9" : "=r"(t)); return t; }
static void save() {
  FILE* f = fopen(Tyra::FileUtils::fromCwd("frame-cost.csv").c_str(), "w");
  if (!f) return;
  fprintf(f,"frame,phase,update_ms,vehicles_included_ms,submit_ms,finish_ms,present_ms,total_ms,bounds_included_ms,prepare_included_ms,dispatch_included_ms,packet_included_ms,dma_included_ms,vif_wait_included_ms,flushes,triangles,uploads,reuploads\n");
  for (unsigned i=0;i<count;++i) {
    const Row& r=rows[i];
    fprintf(f,"%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%u,%u\n",
      r.frame,r.frame/360,r.update/294912.0,r.vehicles/294912.0,r.submit/294912.0,
      r.finish/294912.0,r.present/294912.0,r.total/294912.0,r.bounds/294912.0,
      r.prepare/294912.0,r.dispatch/294912.0,r.packet/294912.0,r.dma/294912.0,
      r.wait/294912.0,r.flushes,r.triangles,r.uploads,r.reuploads);
  }
  fclose(f);
}
}
'''
loop = loop.replace('void TerrainGame::loop() {', '''void TerrainGame::loop() {
  const unsigned dmStart = districtMeasure::ticks();
  unsigned dmVehicles = 0;
  stapip.core.setTelemetryEnabled(true);
  stapip.core.takeTelemetry();
  engine->renderer.core.takeStallTicks();
  const auto dmTex0 = engine->renderer.core.texture.stats;''', 1)
needle = 'updateVehicles(g_frameScale * (1.0F / 50.0F));'
assert loop.count(needle) == 1
loop = loop.replace(needle, '{ const unsigned t=districtMeasure::ticks(); ' + needle + ' dmVehicles=districtMeasure::ticks()-t; }')
needle = '  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));'
assert loop.count(needle) == 1
loop = loop.replace(needle, '  const unsigned dmRender = districtMeasure::ticks();\n' + needle)
needle = '  engine->renderer.endFrame();'
assert loop.count(needle) == 1
loop = loop.replace(needle, '''  const unsigned dmFinish = districtMeasure::ticks();
''' + needle + '''
  const unsigned dmEnd = districtMeasure::ticks();
  const unsigned dmStall = engine->renderer.core.takeStallTicks();
  const auto dmPipe = stapip.core.takeTelemetry();
  const auto dmTex = engine->renderer.core.texture.stats;
  const unsigned dmF = districtMeasure::frame++;
  if (dmF < 1440 && dmF % 360 >= 120 && districtMeasure::count < 960) {
    districtMeasure::rows[districtMeasure::count++] = {
      dmF,dmRender-dmStart,dmVehicles,dmFinish-dmRender,dmEnd-dmFinish-dmStall,
      dmStall,dmEnd-dmStart,dmPipe.boundsTicks,dmPipe.prepareTicks,
      dmPipe.dispatchTicks,dmPipe.packetBuildTicks,dmPipe.dmaSubmitTicks,
      dmPipe.vu1WaitTicks,dmPipe.packetFlushes,
      dmPipe.trianglesCull+dmPipe.trianglesClip,
      dmTex.uploads-dmTex0.uploads,dmTex.reuploads-dmTex0.reuploads};
  }
  if (dmF == 1440) districtMeasure::save();''')
path.write_text(s[:start] + helper + loop + s[end:], encoding='utf-8')
print('Instrumented:', path)
