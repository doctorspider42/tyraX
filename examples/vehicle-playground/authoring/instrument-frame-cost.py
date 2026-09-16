"""Instrument an isolated benchmark AFTER --refresh-gen; build with native-build.

No engine edits. Store raw per-frame timings in RAM and write after sampling.
The finish bucket includes remaining renderer work; it is not a GS-only timer.

REGENERATE THE FIXTURE FIRST. This patches the fixture's generated
`src/terrain_game.cpp`, and `benchmark-district.py` copies the example's
*committed* generated sources, which drift. Measured 2026-09-15: a stale
fixture reported 3.6 ms more render submission and 12.17 phantom texture
re-uploads per frame than the same scene regenerated with the editor under
test. See docs/vu1-and-dma-cache-cost.md.

--attribute adds the RENDER-SUBMISSION ATTRIBUTION pass
(docs/render-submission-attribution.md): the `submit` bucket above is
beginFrame..endFrame, i.e. the whole render block including the post-process
passes and the 2D HUD, while `bounds`/`prepare`/`dispatch` cover only
StaPipCore::render. On the Motor District garage-day pose those three account
for 22.2 ms of 29.7 and nothing measured the other 7.5. This pass brackets
every renderScene phase, the object loop's own per-object tests, and the
post-fx and HUD blocks, into a second file, `bin/frame-attrib.csv`. It pairs
with the engine's own `TYRA_STAPIP_ATTRIB` counters, which split what is left
INSIDE StaPipCore::render; set that macro to 1 in
`vendor/tyra/.../static/core/stapip_attrib.hpp` for the same build, or the
engine columns come back as zeros (and say so, rather than being absent).

Every bracket here is a pair of COP0 reads and an add. NOTHING DRAINS. The
render-cost capture that renderScene already carries is a different
instrument: it calls `sync.align3D()` per phase, which serialises the frame
and is attribution evidence rather than a frame-time sample.
"""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('project', type=Path)
p.add_argument('--attribute', action='store_true',
               help='also bracket every renderScene phase, the object loop '
                    'and the post-fx/HUD blocks into bin/frame-attrib.csv')
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
tail = s[end:]
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

# ---------------------------------------------------------------- attribution
ATT_NAMES = ['rsTotal', 'rsHead', 'rsEnvProbe', 'rsPortal', 'rsSky',
             'rsTerrain', 'rsTerrainDraw', 'rsBatches',
             'rsProc', 'rsObjects', 'rsObjSubmit', 'rsWheels', 'rsWheelSubmit',
             'rsAnim', 'rsDecorate', 'rsLightFx', 'rsHighlight', 'rsParticles',
             'dmPost', 'dmHud']
# Three of those are SUBSETS of the phase above them, so that the game's own
# EE work can be told from the submission it wraps:
#   rsEnvProbe    < rsHead     the shared reflection-probe pass
#   rsTerrainDraw < rsTerrain  renderTerrain(), i.e. not the chunk streaming
#   rsObjSubmit   < rsObjects  the per-object part loop, i.e. not the tests
#   rsWheelSubmit < rsWheels   the ONE bag submit, i.e. not the EE wheel rebake
ENG_NAMES = ['spRender', 'spHead', 'spTail', 'spCalls', 'spCulled', 'spPkgr',
             'spTex', 'spProg', 'spLight', 'spBlss', 'spObjData', 'spGifWait',
             # the five parts of `bounds`, plus the bbox cacher's own counters
             'bdSize', 'bdProg', 'bdSizeCalc',
             'bdXform', 'bdCache', 'bdPlanes', 'bdMain',
             'bbHit', 'bbRecalc', 'bbFresh', 'bbProbe', 'bbEntries',
             'bbFrameEnd', 'bbRecalcT',
             # inside `dispatch`: the package creation/classification box
             'dsRetain', 'dsDirect', 'dsCreate', 'dsClassify', 'dsRender',
             'dsFlush', 'dsDirectBags', 'dsPartialBags', 'dsPackages',
             'dsMergeParts', 'dsMaskCalls']
# spCalls / spCulled are COUNTS, not ticks; everything else is COP0 ticks.
ATT_COUNTS = {'spCalls', 'spCulled',
              'bbHit', 'bbRecalc', 'bbFresh', 'bbProbe', 'bbEntries',
              'dsDirectBags', 'dsPartialBags', 'dsPackages',
              'dsMergeParts', 'dsMaskCalls'}
# `bbEntries` is a LEVEL (how many entries the 250-frame retention holds),
# not something accumulated over the frame - the others are per-frame totals.

if a.attribute:
    fields = ATT_NAMES + ENG_NAMES
    decl = ', '.join('a' + n for n in ATT_NAMES)
    row = ', '.join(fields)
    header = 'frame,phase,' + ','.join(
        (n if n in ATT_COUNTS else n + '_ms') for n in fields)
    fmt = ','.join('%u' if n in ATT_COUNTS else '%.6f' for n in fields)
    vals = ',\n      '.join(
        ('r.' + n) if n in ATT_COUNTS else ('r.%s/294912.0' % n)
        for n in fields)
    helper += r'''
// Added by TyraX: the render-submission attribution pass
// (docs/render-submission-attribution.md). Phase accumulators, cleared at the
// top of every loop() and read just before the frame row is stored. No drains,
// no host I/O, no allocation - a bracket is two COP0 reads and an add.
//
// stapip_attrib.hpp reaches here through the pipeline headers the generated
// game already includes, so TYRA_STAPIP_ATTRIB and Tyra::StaPipAttrib are in
// scope without an include of our own - this block sits inside the project's
// own namespace, where an include directive would not belong.
namespace districtMeasure {
struct Att { unsigned frame; unsigned ''' + row + r'''; };
static Att atts[960];
static unsigned aCount = 0;
static unsigned ''' + decl + r''';
static void resetPhases() {
  ''' + ' '.join('a%s = 0;' % n for n in ATT_NAMES) + r'''
}
static void saveAtt() {
  FILE* f = fopen(Tyra::FileUtils::fromCwd("frame-attrib.csv").c_str(), "w");
  if (!f) return;
  fprintf(f,"''' + header + r'''\n");
  for (unsigned i=0;i<aCount;++i) {
    const Att& r=atts[i];
    fprintf(f,"%u,%u,''' + fmt + r'''\n",
      r.frame, r.frame/360,
      ''' + vals + r''');
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

store = '''
    districtMeasure::rows[districtMeasure::count++] = {
      dmF,dmRender-dmStart,dmVehicles,dmFinish-dmRender,dmEnd-dmFinish-dmStall,
      dmStall,dmEnd-dmStart,dmPipe.boundsTicks,dmPipe.prepareTicks,
      dmPipe.dispatchTicks,dmPipe.packetBuildTicks,dmPipe.dmaSubmitTicks,
      dmPipe.vu1WaitTicks,dmPipe.packetFlushes,
      dmPipe.trianglesCull+dmPipe.trianglesClip,
      dmTex.uploads-dmTex0.uploads,dmTex.reuploads-dmTex0.reuploads};'''

if a.attribute:
    # The engine half is only present when TYRA_STAPIP_ATTRIB is compiled in.
    # When it is not, the columns are written as zeros rather than dropped, so
    # a control arm's CSV still lines up column for column with the armed one.
    eng = '''
#if TYRA_STAPIP_ATTRIB
    const Tyra::StaPipAttrib& dmA = dmPipe.attrib;
    const unsigned dmEng[''' + str(len(ENG_NAMES)) + '''] = {
      dmA.renderTicks, dmA.headTicks, dmA.tailTicks, dmA.renderCalls,
      dmA.renderCallsCulled, dmA.prepPackagerTicks, dmA.prepTextureTicks,
      dmA.prepProgramTicks, dmA.prepLightTicks, dmA.prepBlssTicks,
      dmA.prepObjectDataTicks, dmA.gifWaitTicks,
      dmA.bdSizeTicks, dmA.bdProgTicks, dmA.bdSizeCalcTicks,
      dmA.bdXformTicks, dmA.bdCacheTicks,
      dmA.bdPlanesTicks, dmA.bdMainTicks,
      dmA.bboxCacheHits, dmA.bboxCacheRecalcs, dmA.bboxCacheFresh,
      dmA.bboxCacheProbes, dmA.bboxCacheEntries, dmA.bboxCacheFrameEndTicks,
      dmA.bboxCacheRecalcTicks,
      dmA.dsRetainTicks, dmA.dsDirectTicks, dmA.dsCreateTicks,
      dmA.dsClassifyTicks, dmA.dsRenderTicks, dmA.dsFlushTicks,
      dmA.dsDirectBags, dmA.dsPartialBags, dmA.dsPackages,
      dmA.dsMergeParts, dmA.dsMaskCalls};
#else
    const unsigned dmEng[''' + str(len(ENG_NAMES)) + '''] = {0};
#endif
    districtMeasure::atts[districtMeasure::aCount++] = {dmF,
      ''' + ', '.join('districtMeasure::a' + n for n in ATT_NAMES) + ''',
      ''' + ', '.join('dmEng[%d]' % i for i in range(len(ENG_NAMES))) + '''};'''
    store += eng

needle = '  engine->renderer.endFrame();'
assert loop.count(needle) == 1
loop = loop.replace(needle, '''  const unsigned dmFinish = districtMeasure::ticks();
''' + ('  districtMeasure::aHudEnd();\n' if a.attribute else '') + needle + '''
  const unsigned dmEnd = districtMeasure::ticks();
  const unsigned dmStall = engine->renderer.core.takeStallTicks();
  const auto dmPipe = stapip.core.takeTelemetry();
  const auto dmTex = engine->renderer.core.texture.stats;
  const unsigned dmF = districtMeasure::frame++;
  if (dmF < 1440 && dmF % 360 >= 120 && districtMeasure::count < 960) {''' +
  store + '''
  }
  if (dmF == 1440) districtMeasure::save();''' +
  ('\n  if (dmF == 1440) districtMeasure::saveAtt();' if a.attribute else ''))

if a.attribute:
    # Clear the phase accumulators at the very top of the loop, where dmStart
    # is taken - every phase below writes into a frame of its own.
    loop = loop.replace('  const unsigned dmStart = districtMeasure::ticks();',
                        '  districtMeasure::resetPhases();\n'
                        '  const unsigned dmStart = districtMeasure::ticks();', 1)
    # The post-fx block runs between the scene and the HUD. Two unique anchors
    # split beginFrame..endFrame into scene / post-fx / HUD; the scene share is
    # then dmFinish-dmRender minus the other two, which is also what makes
    # beginFrame's own cost visible as `scene - rsTotal`.
    needle = '  updateSunFx();'
    assert loop.count(needle) == 1, needle
    loop = loop.replace(
        needle, '  districtMeasure::aMark = districtMeasure::ticks();\n' + needle, 1)
    # The HUD region opens where the post-fx one closes, and closes at
    # endFrame (aHudEnd, inserted with dmFinish above).
    needle = '    updateHudMotion();'
    assert loop.count(needle) == 1, needle
    loop = loop.replace(
        needle, '  districtMeasure::aPostEnd();\n'
                '  districtMeasure::aMark = districtMeasure::ticks();\n' + needle, 1)
    helper = helper.replace('static void resetPhases() {', r'''static unsigned aMark = 0;
static void aPostEnd() { admPost += ticks() - aMark; }
static void aHudEnd() { admHud += ticks() - aMark; }
static void resetPhases() {''')

    # ---- renderScene: one bracket per phase, plus the object loop's split.
    def wrap_line(text, line, acc):
        assert text.count(line) == 1, line
        return text.replace(
            line,
            '  { const unsigned dmT=districtMeasure::ticks();' + line +
            ' districtMeasure::a' + acc + ' += districtMeasure::ticks()-dmT; }',
            1)

    def open_close(text, opener, closer, acc):
        assert text.count(opener) == 1, opener
        assert text.count(closer) == 1, closer
        text = text.replace(
            opener, '  const unsigned dmT_' + acc + '=districtMeasure::ticks();\n'
            + opener, 1)
        return text.replace(
            closer, closer + '\n  districtMeasure::a' + acc +
            ' += districtMeasure::ticks()-dmT_' + acc + ';', 1)

    needle = 'void TerrainGame::renderScene() {'
    assert tail.count(needle) == 1
    tail = tail.replace(
        needle, needle + '\n  const unsigned dmRsStart = districtMeasure::ticks();', 1)
    needle = '  renderCameraFeed();'
    assert tail.count(needle) == 1
    tail = tail.replace(
        needle, needle + '\n  districtMeasure::arsHead += '
        'districtMeasure::ticks() - dmRsStart;', 1)

    for line, acc in (
            ('  { const u32 ct=costStart(); renderPortalView(); costEnd("Portal",-1,ct); }', 'rsPortal'),
            ('  { const u32 ct=costStart(); renderStaticBatches(); costEnd("Static_batches",-1,ct); }', 'rsBatches'),
            ('  { const u32 ct=costStart(); renderProcChunks(); costEnd("Procedural",-1,ct); }', 'rsProc'),
            ('  { const u32 ct=costStart(); renderVehicleWheels(); costEnd("Wheels",-1,ct); }', 'rsWheels'),
            ('  stapip.core.render(wheelBag_.get());', 'rsWheelSubmit'),
            ('  { const u32 ct=costStart(); updateAndRenderAnimObjects(); costEnd("Animation",-1,ct); }', 'rsAnim'),
            ('  { const u32 ct=costStart(); renderShadowDecals(); costEnd("Shadow_decals",-1,ct); }', 'rsDecorate'),
            ('  { const u32 ct=costStart(); renderMirrors(); costEnd("Mirrors",-1,ct); }', 'rsDecorate'),
            ('  { const u32 ct=costStart(); renderPortals(); costEnd("Portal_surfaces",-1,ct); }', 'rsDecorate'),
            ('  { const u32 ct=costStart(); updateAndRenderLightPools(); costEnd("Light_pools",-1,ct); }', 'rsLightFx'),
            ('  { const u32 ct=costStart(); renderProjShadows(); costEnd("Projected_shadows",-1,ct); }', 'rsLightFx'),
            ('  { const u32 ct=costStart(); updateAndRenderBlobShadows(); costEnd("Blob_shadows",-1,ct); }', 'rsLightFx'),
            ('  { const u32 ct=costStart(); updateAndRenderLightBeams(); costEnd("Light_beams",-1,ct); }', 'rsLightFx')):
        tail = wrap_line(tail, line, acc)

    for opener, closer, acc in (
            ('  const u32 costSkyStart=costStart();', '  costEnd("Sky",-1,costSkyStart);', 'rsSky'),
            ('  const u32 costTerrainStart=costStart();', '  costEnd("Terrain",-1,costTerrainStart);', 'rsTerrain'),
            ('  const u32 costObjectsStart=costStart();', '  costEnd("Objects",-1,costObjectsStart);', 'rsObjects'),
            ('  const u32 costHighlightStart=costStart();', '  costEnd("Highlights_and_outlines",-1,costHighlightStart);', 'rsHighlight'),
            ('  const u32 costParticleStart=costStart();', '  costEnd("Particles",-1,costParticleStart);', 'rsParticles'),
            ('    const u32 costObjectStart=costStart();', '    costEnd("Object",i,costObjectStart);', 'rsObjSubmit'),
            ('    const u32 costSharedEnvStart = costStart();', '    costEnd("Reflections_shared_probe", -1, costSharedEnvStart);', 'rsEnvProbe')):
        tail = open_close(tail, opener, closer, acc)

    # renderTerrain() at renderScene's own indentation - the two deeper calls
    # (the mirror and portal passes) are indented four spaces and must not be
    # matched, so the anchor carries its newline and exactly two spaces.
    needle = '\n  renderTerrain();'
    assert tail.count(needle) == 1, needle
    tail = tail.replace(
        needle, '\n  const unsigned dmT_rsTerrainDraw=districtMeasure::ticks();'
        + needle, 1)
    needle = '  costEnd("Terrain",-1,costTerrainStart);'
    assert tail.count(needle) == 1, needle
    tail = tail.replace(
        needle, needle + '\n  districtMeasure::arsTerrainDraw += '
        'districtMeasure::ticks()-dmT_rsTerrainDraw;', 1)

    # Close the whole-function bracket at the real end of renderScene, after
    # the (unarmed during sampling) render-cost footer.
    needle = '''      fprintf(f,"END %u\\n",costSeq);
      fclose(f);
    }
  }

}'''
    assert tail.count(needle) == 1
    tail = tail.replace(needle, needle[:-1] +
                        '  districtMeasure::arsTotal += '
                        'districtMeasure::ticks() - dmRsStart;\n}', 1)

path.write_text(s[:start] + helper + loop + tail, encoding='utf-8')
print('Instrumented:', path, '(attribution)' if a.attribute else '')
