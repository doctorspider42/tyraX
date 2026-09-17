"""Instrument ANY generated TyraX game's frame loop for per-frame cost sampling.

This is the project-agnostic twin of
`examples/vehicle-playground/authoring/instrument-frame-cost.py`, which brackets
that example's own `updateVehicles` call and its four-pose day/night script and
therefore only works on the Motor District. Everything here is emitted by
`templates.cpp` for every project, so it patches any generated game.

Why it exists: a performance change to the ENGINE has to be shown on more than
one map, or what has been measured is the showcase and not the engine. See
docs/engine-performance-on-a-second-map.md.

What it measures, per frame, into RAM and written once after sampling:

    update   loop() start .. beginFrame          the game's own simulation
    submit   beginFrame .. endFrame              the whole render block
    finish   endFrame .. loop() end, less stall  presentation excluded
    present  the renderer's stall ticks          buffer-flip / VSync wait

plus the static pipeline's own telemetry buckets, which OVERLAP each other and
overlap `submit`: bounds, prepare, dispatch, packet build, DMA submit, VIF1
wait, packet flushes, triangles, texture uploads and re-uploads.

None of these may be inverted into FPS, and `finish` is not a GS-only timer.

TWO RULES, both learned the expensive way (docs/vu1-and-dma-cache-cost.md):

  * REGENERATE THE FIXTURE FIRST. This patches the fixture's generated
    `src/terrain_game.cpp`, and a fixture copied from a checked-in example
    carries that example's committed generated sources, which drift. Measured:
    a stale Motor District fixture reported 3.6 ms more render submission and
    12.17 phantom texture re-uploads per frame than the same scene regenerated
    with the editor under test.

  * BUILD IT WITH `tools/toolchain/native-build.ps1` / `.sh` DIRECTLY. An
    editor `--build` re-runs code generation and removes this instrumentation.

Usage:
    python tools/instrument-frame-cost.py <projectDir> [--warmup N] [--samples N]
"""
import argparse
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__,
                            formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('project', type=Path)
p.add_argument('--warmup', type=int, default=120,
               help='frames to discard before sampling (default 120)')
p.add_argument('--samples', type=int, default=240,
               help='frames to record (default 240)')
a = p.parse_args()

path = a.project / 'src/terrain_game.cpp'
s = path.read_text(encoding='utf-8')
assert 'frameCost' not in s, 'Already instrumented'

start = s.index('void TerrainGame::loop() {')
end = s.index('\n// Adaptive plain BLSS', start)
loop = s[start:end]

TOTAL = a.warmup + a.samples

helper = r'''
// Added by tools/instrument-frame-cost.py. Raw per-frame timings in RAM,
// written ONCE after sampling - a host: write inside the window is a network
// round trip over ps2link and would be noise with a period.
namespace frameCost {
static unsigned frame = 0, count = 0;
struct Row {
  unsigned frame, update, submit, finish, present, total;
  unsigned bounds, prepare, dispatch, packet, dma, wait, flushes, triangles;
  unsigned uploads, reuploads;
};
static Row rows[__SAMPLES__];
static unsigned ticks() { unsigned t; asm volatile("mfc0 %0, $9" : "=r"(t)); return t; }
static void save() {
  FILE* f = fopen(Tyra::FileUtils::fromCwd("frame-cost.csv").c_str(), "w");
  if (!f) return;
  fprintf(f,"frame,phase,update_ms,submit_ms,finish_ms,present_ms,total_ms,"
            "bounds_included_ms,prepare_included_ms,dispatch_included_ms,"
            "packet_included_ms,dma_included_ms,vif_wait_included_ms,"
            "flushes,triangles,uploads,reuploads\n");
  for (unsigned i=0;i<count;++i) {
    const Row& r=rows[i];
    fprintf(f,"%u,0,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%u,%u\n",
      r.frame,r.update/294912.0,r.submit/294912.0,r.finish/294912.0,
      r.present/294912.0,r.total/294912.0,r.bounds/294912.0,
      r.prepare/294912.0,r.dispatch/294912.0,r.packet/294912.0,
      r.dma/294912.0,r.wait/294912.0,r.flushes,r.triangles,
      r.uploads,r.reuploads);
  }
  fclose(f);
}
}
'''.replace('__SAMPLES__', str(a.samples))

loop = loop.replace('void TerrainGame::loop() {', '''void TerrainGame::loop() {
  const unsigned fcStart = frameCost::ticks();
  stapip.core.setTelemetryEnabled(true);
  stapip.core.takeTelemetry();
  engine->renderer.core.takeStallTicks();
  const auto fcTex0 = engine->renderer.core.texture.stats;''', 1)

needle = '  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));'
assert loop.count(needle) == 1, 'beginFrame call not found or not unique'
loop = loop.replace(needle, '  const unsigned fcRender = frameCost::ticks();\n' + needle)

needle = '  engine->renderer.endFrame();'
assert loop.count(needle) == 1, 'endFrame call not found or not unique'
loop = loop.replace(needle, '''  const unsigned fcFinish = frameCost::ticks();
''' + needle + '''
  const unsigned fcEnd = frameCost::ticks();
  const unsigned fcStall = engine->renderer.core.takeStallTicks();
  const auto fcPipe = stapip.core.takeTelemetry();
  const auto fcTex = engine->renderer.core.texture.stats;
  const unsigned fcF = frameCost::frame++;
  if (fcF >= __WARMUP__ && frameCost::count < __SAMPLES__) {
    frameCost::rows[frameCost::count++] = {
      fcF,fcRender-fcStart,fcFinish-fcRender,fcEnd-fcFinish-fcStall,
      fcStall,fcEnd-fcStart,fcPipe.boundsTicks,fcPipe.prepareTicks,
      fcPipe.dispatchTicks,fcPipe.packetBuildTicks,fcPipe.dmaSubmitTicks,
      fcPipe.vu1WaitTicks,fcPipe.packetFlushes,
      fcPipe.trianglesCull+fcPipe.trianglesClip,
      fcTex.uploads-fcTex0.uploads,fcTex.reuploads-fcTex0.reuploads};
  }
  if (fcF == __TOTAL__) frameCost::save();'''
    .replace('__WARMUP__', str(a.warmup))
    .replace('__SAMPLES__', str(a.samples))
    .replace('__TOTAL__', str(TOTAL)))

path.write_text(s[:start] + helper + loop + s[end:], encoding='utf-8')
print(f'Instrumented: {path}  (warmup {a.warmup}, samples {a.samples}, '
      f'writes frame-cost.csv at frame {TOTAL})')
