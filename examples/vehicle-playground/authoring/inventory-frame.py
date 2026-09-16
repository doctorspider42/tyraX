"""Inventory a Motor District frame BY PRODUCER: triangles, VU1 packages, bags.

The frame-cost instrumenter answers "where did the milliseconds go". This one
answers the question nobody had ever asked of this scene - "what is the frame
MADE OF" - and it is the question that would have stopped the previous round's
misdirection. `docs/ee-submission-rearchitecture.md` promoted a whole front on
"the road surface alone is 31 050 triangles in the map", and the garage-day
frame turned out to contain 614 of the 9 798 the reduction removed: 1.5%. That
mistake is only possible while the frame's 40 961 triangles have never been
broken down by what produces them.

HOW IT WORKS. `StaPipCore::takeTelemetry()` clears as it reads, so a drain at
every producer boundary yields EXCLUSIVE per-producer counts with no new engine
counters and no engine edit at all. Every bracket opens with a drain into
`rest`, so code between two brackets is attributed to `rest` rather than
leaking into the next producer. Bags are counted game-side (every
`stapip.core.render()` call), which needs no `TYRA_STAPIP_ATTRIB` and is the
honest definition anyway: a bag is what the GAME hands the pipeline.

WHAT THE TRIANGLE COLUMN IS. `trianglesCull + trianglesClip`, i.e. the
primitives the GS is asked to rasterise - `size - 2` for a strip package and
`size / 3` for a list one. Degenerate strip joins and run padding are included,
because they cost. That is the same number `frame-cost.csv` reports as
`triangles` and the same one the four-pose table in
docs/ee-submission-rearchitecture.md quotes, so the rows SUM to it. It is NOT a
surface count; see stapip_telemetry.hpp.

THIS IS A COUNTS INSTRUMENT, NOT A TIMING ONE. It drains telemetry dozens of
times a frame, so its milliseconds are its own. Run it in PCSX2; no console is
needed and none of its numbers are hardware timings. Apply it INSTEAD of
instrument-frame-cost.py, not alongside it.

REGENERATE THE FIXTURE FIRST, with an editor built from the worktree under
test - the same rule instrument-frame-cost.py carries, for the same reason.
"""
import argparse
from pathlib import Path
import json

# One slot per producer, in the order renderScene submits them. `rest` holds
# everything no bracket covered, so the slots sum to the frame.
SLOTS = [
    'rest',            # 0 - unbracketed: beginFrame, the game's own EE work
    # The shared 128x128 reflection target (S4), split at the seam the three
    # options differ over: the dome and the discs are fixed, the "Show in
    # reflections" objects are the content decision.
    'env_probe_sky',   # 1 - dome + sun/moon discs into the probe target
    'env_probe_objs',  # 2 - every `reflected` object, no distance gate at all
    'camera_feed',     # 3
    'portal_view',     # 4
    'sky',             # 5 - dome + star field + sun/moon discs, main view
    'terrain',         # 6 - base + painted layers + AO + emissive, all chunks
    'static_batches',  # 7 - one bag per material x cell group
    'roads',           # 8 - ProcChunk owner == -3 (docs/roads.md)
    'proc_other',      # 9 - every other procedural chunk / prefab instance
    'object_tests',    # 10 - the object loop MINUS the per-object submits
    'object_submit',   # 11 - the solo objects' own bags (split per object too)
    'wheels',          # 12
    'anim',            # 13 - skinned models
    'shadow_decals',   # 14
    'mirrors',         # 15
    'portal_surfaces',  # 16
    'light_pools',     # 17
    'proj_shadows',    # 18
    'blob_shadows',    # 19
    'light_beams',     # 20
    'highlight',       # 21 - highlights and outlines
    'particles',       # 22 - INCLUDES vehicle smoke, skids and the glow pass
]
IDX = {n: i for i, n in enumerate(SLOTS)}

p = argparse.ArgumentParser(description=__doc__,
                            formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('project', type=Path)
p.add_argument('--objects', type=int, default=256,
               help='per-object row capacity (default 256)')
a = p.parse_args()
assert (a.project / 'BENCHMARK.json').exists(), 'Use an isolated benchmark fixture'
manifest = json.loads(
    (a.project / 'vehicle-playground.tyra').read_text(encoding='utf-8'))
settings = manifest['settings']
assert not settings.get('blssEnabled'), 'Measure a fixed native raster'
assert not settings.get('blssAdaptive'), 'Adaptive raster changes the workload'
assert not settings.get('frameExtrapolation'), 'One presentation per loop'

path = a.project / 'src/terrain_game.cpp'
s = path.read_text(encoding='utf-8')
assert 'districtInv' not in s, 'Already instrumented'
assert 'districtMeasure' not in s, \
    'frame-cost instrumentation present; this pass replaces it, it does not stack'

# --- every bag submission, counted where the GAME makes it -------------------
# `stapip.core.render(x)` is only ever an expression statement in the generated
# game (checked: 57 call sites, none in a for-header or an argument list), so a
# comma expression in front of it is legal everywhere and changes no control
# flow.
CALL = 'stapip.core.render('
assert s.count(CALL) > 20, 'Unexpected generated game: too few submissions'
s = s.replace(CALL, '++districtInv::bags, ' + CALL)
# Submissions happen in functions defined BEFORE loop(), where the helper
# below has not been declared yet, so the counter itself is declared at the top
# of the file. It needs no header of its own, which is why it can go there.
last_include = s.rfind('\n#include', 0, 8000)
assert last_include > 0, 'No include block found in the generated game'
cut = s.index('\n', last_include + 1) + 1
s = s[:cut] + 'namespace districtInv { extern unsigned bags; }\n' + s[cut:]

helper = r'''
// Added by TyraX (authoring/inventory-frame.py): PER-PRODUCER FRAME INVENTORY.
// takeTelemetry() clears as it reads, so a drain at every producer boundary is
// an exclusive split of the frame with no engine counters added. Every bracket
// opens with a drain into `rest`, so unbracketed work never leaks into the
// producer that follows it.
namespace districtInv {
enum { kSlots = ''' + str(len(SLOTS)) + r''', kObjects = ''' + str(a.objects) + r''' };
struct Acc {
  unsigned tri, triOut, pkg, pkgOut, guard, verts, flush, bags, hits;
};
static Acc slot[4][kSlots];
static Acc obj[4][kObjects];
// The SAME per-object split for the PROBE pass, which is a different question:
// these rows say which "Show in reflections" object is worth what in a 128x128
// target, and that is the whole content decision behind S4.
static Acc probeObj[4][kObjects];
// What each object IS, so the host can group the rows without guessing the
// codegen's object order: the SceneObjectData type tag and its MODEL_PATHS
// slot (-1 for a primitive). Recorded once, when the object first draws.
static int objType[kObjects];
static int objModel[kObjects];
unsigned bags = 0;              // stapip.core.render() calls since the drain
static unsigned frame = 0;
static unsigned frames[4];
static bool written = false;
static unsigned phase() { const unsigned f = frame; return f < 1440 ? f / 360 : 3; }
// The same window instrument-frame-cost.py samples: 120 warm-up frames, then
// 240 recorded ones, in each of the fixture's four parked poses.
static bool sampling() { return frame < 1440 && frame % 360 >= 120; }
static void fold(Acc& acc, const Tyra::StaPipTelemetry& t) {
  acc.tri += t.trianglesCull + t.trianglesClip;
  acc.triOut += t.trianglesOutside;
  acc.pkg += t.packagesCull + t.packagesClip;
  acc.pkgOut += t.packagesOutside;
  acc.guard += t.packagesGuardBand;
  acc.verts += t.verticesSubmitted;
  acc.flush += t.packetFlushes;
  acc.bags += bags;
  ++acc.hits;
}
// Every drain goes through here: the bag counter is zeroed with it, so a bag
// is charged to exactly one producer.
static void add(unsigned s, const Tyra::StaPipTelemetry& t) {
  if (sampling() && s < kSlots) fold(slot[phase()][s], t);
  bags = 0;
}
static void addObject(Acc* table, int i, int type, int model,
                      const Tyra::StaPipTelemetry& t) {
  const unsigned keep = bags;
  if (sampling() && i >= 0 && i < (int)kObjects) {
    fold(table[i], t);
    objType[i] = type;
    objModel[i] = model;
  }
  bags = keep;  // the same drain is also charged to the slot-level total
}
// The reflection reuse gate's own verdict (docs/reflective-materials.md, "The
// reuse budget"), recorded where it is decided. `worstReused` is the QUALITY
// number of that change: the largest staleness, in pixels of the probe's own
// 128-pixel target, that the gate actually let stand. A budget is a promise;
// this is what was delivered against it.
static unsigned reuseBeats[4];   // cadence beats the gate was consulted on
static unsigned reuseTaken[4];   // ...of which the capture was skipped
static float worstReused[4];     // worst drift, in target pixels, when skipped
static float worstSeen[4];       // worst drift at a beat, skipped or not
static void reuse(bool ok, float driftPx) {
  if (!sampling()) return;
  const unsigned ph = phase();
  ++reuseBeats[ph];
  if (driftPx > worstSeen[ph]) worstSeen[ph] = driftPx;
  if (ok) {
    ++reuseTaken[ph];
    if (driftPx > worstReused[ph]) worstReused[ph] = driftPx;
  }
}
static void save() {
  if (written) return;
  written = true;
  FILE* f = fopen(Tyra::FileUtils::fromCwd("frame-inventory.csv").c_str(), "w");
  if (!f) return;
  fprintf(f, "kind,phase,producer,object,type,model,frames,hits,triangles,"
             "packages,packages_outside,packages_guard,vertices,flushes,"
             "bags\n");
  static const char* kNames[kSlots] = {''' + ', '.join(
      '"%s"' % n for n in SLOTS) + r'''};
  for (unsigned ph = 0; ph < 4; ++ph) {
    for (unsigned i = 0; i < kSlots; ++i) {
      const Acc& r = slot[ph][i];
      fprintf(f, "producer,%u,%s,-1,-1,-1,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", ph,
              kNames[i], frames[ph], r.hits, r.tri, r.pkg, r.pkgOut, r.guard,
              r.verts, r.flush, r.bags);
    }
    for (unsigned i = 0; i < kObjects; ++i) {
      const Acc& r = obj[ph][i];
      if (!r.hits) continue;
      fprintf(f,
              "object,%u,object_submit,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
              ph, i, objType[i], objModel[i], frames[ph], r.hits, r.tri, r.pkg,
              r.pkgOut, r.guard, r.verts, r.flush, r.bags);
    }
    for (unsigned i = 0; i < kObjects; ++i) {
      const Acc& r = probeObj[ph][i];
      if (!r.hits) continue;
      fprintf(f,
              "probe_object,%u,env_probe_objs,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,"
              "%u,%u\n",
              ph, i, objType[i], objModel[i], frames[ph], r.hits, r.tri, r.pkg,
              r.pkgOut, r.guard, r.verts, r.flush, r.bags);
    }
    // The reuse gate's row. `hits` is the beats it was consulted on and
    // `bags` the beats it skipped; the two drift figures ride in the columns
    // the producers use for triangles and packages, scaled by 1000 because
    // this file is integers throughout.
    fprintf(f, "reuse,%u,env_reuse,-1,-1,-1,%u,%u,%u,%u,0,0,0,0,%u\n", ph,
            frames[ph], reuseBeats[ph],
            (unsigned)(worstReused[ph] * 1000.0F + 0.5F),
            (unsigned)(worstSeen[ph] * 1000.0F + 0.5F), reuseTaken[ph]);
  }
  fclose(f);
}
}
'''

start = s.index('void TerrainGame::loop() {')
end = s.index('\n// Adaptive plain BLSS', start)
loop = s[start:end]
tail = s[end:]

# --- loop(): arm the telemetry, drain the tail, advance the frame ------------
loop = loop.replace('void TerrainGame::loop() {', '''void TerrainGame::loop() {
  stapip.core.setTelemetryEnabled(true);
  districtInv::bags = 0;
  stapip.core.takeTelemetry();''', 1)
needle = '  engine->renderer.endFrame();'
assert loop.count(needle) == 1
loop = loop.replace(needle, needle + '''
  // AFTER endFrame: its final chain flush is part of the frame, and charging
  // it to the next one would make `rest` report a frame out of step.
  districtInv::add(0, stapip.core.takeTelemetry());
  if (districtInv::sampling()) ++districtInv::frames[districtInv::phase()];
  ++districtInv::frame;
  if (districtInv::frame == 1440) districtInv::save();''', 1)

# --- renderScene(): one drain pair per producer ------------------------------
OPEN = '  { const Tyra::StaPipTelemetry dmI = stapip.core.takeTelemetry(); districtInv::add(0, dmI); }\n'


def close(slot):
    return ('  { const Tyra::StaPipTelemetry dmI = stapip.core.takeTelemetry();'
            ' districtInv::add(%d, dmI); }\n' % IDX[slot])


def wrap_line(text, line, slot):
    """A whole phase that is exactly one line of the generated renderScene."""
    assert text.count(line) == 1, line
    return text.replace(line, OPEN + line + '\n' + close(slot).rstrip('\n'), 1)


def open_close(text, opener, closer, slot):
    assert text.count(opener) == 1, opener
    assert text.count(closer) == 1, closer
    text = text.replace(opener, OPEN + opener, 1)
    return text.replace(closer, closer + '\n' + close(slot).rstrip('\n'), 1)


for line, slot in (
        ('  renderCameraFeed();', 'camera_feed'),
        ('  { const u32 ct=costStart(); renderPortalView(); costEnd("Portal",-1,ct); }', 'portal_view'),
        ('  { const u32 ct=costStart(); renderStaticBatches(); costEnd("Static_batches",-1,ct); }', 'static_batches'),
        ('  { const u32 ct=costStart(); renderProcChunks(); costEnd("Procedural",-1,ct); }', 'proc_other'),
        ('  { const u32 ct=costStart(); renderVehicleWheels(); costEnd("Wheels",-1,ct); }', 'wheels'),
        ('  { const u32 ct=costStart(); updateAndRenderAnimObjects(); costEnd("Animation",-1,ct); }', 'anim'),
        ('  { const u32 ct=costStart(); renderShadowDecals(); costEnd("Shadow_decals",-1,ct); }', 'shadow_decals'),
        ('  { const u32 ct=costStart(); renderMirrors(); costEnd("Mirrors",-1,ct); }', 'mirrors'),
        ('  { const u32 ct=costStart(); renderPortals(); costEnd("Portal_surfaces",-1,ct); }', 'portal_surfaces'),
        ('  { const u32 ct=costStart(); updateAndRenderLightPools(); costEnd("Light_pools",-1,ct); }', 'light_pools'),
        ('  { const u32 ct=costStart(); renderProjShadows(); costEnd("Projected_shadows",-1,ct); }', 'proj_shadows'),
        ('  { const u32 ct=costStart(); updateAndRenderBlobShadows(); costEnd("Blob_shadows",-1,ct); }', 'blob_shadows'),
        ('  { const u32 ct=costStart(); updateAndRenderLightBeams(); costEnd("Light_beams",-1,ct); }', 'light_beams')):
    tail = wrap_line(tail, line, slot)

for opener, closer, slot in (
        ('  const u32 costSkyStart=costStart();', '  costEnd("Sky",-1,costSkyStart);', 'sky'),
        ('  const u32 costTerrainStart=costStart();', '  costEnd("Terrain",-1,costTerrainStart);', 'terrain'),
        ('  const u32 costObjectsStart=costStart();', '  costEnd("Objects",-1,costObjectsStart);', 'object_tests'),
        ('  const u32 costHighlightStart=costStart();', '  costEnd("Highlights_and_outlines",-1,costHighlightStart);', 'highlight'),
        ('  const u32 costParticleStart=costStart();', '  costEnd("Particles",-1,costParticleStart);', 'particles'),
        ('    const u32 costSharedEnvStart = costStart();', '    costEnd("Reflections_shared_probe", -1, costSharedEnvStart);', 'env_probe_objs')):
    tail = open_close(tail, opener, closer, slot)

# Inside the probe pass, the dome and the two discs are fixed cost and the
# "Show in reflections" objects are the content decision, so they are split
# here and each reflected object also gets a row of its own. That loop runs
# with NO distance or screen-size gate - the engine's per-bag classify is the
# only thing that rejects anything - which is exactly what these rows price.
#
# Both anchors below appear in the per-object reflected-ray probe and in the
# mirror pass as well, so they are matched INSIDE the shared probe's bracket
# rather than over the whole file.
lo = tail.index('    const u32 costSharedEnvStart = costStart();')
hi = tail.index('    costEnd("Reflections_shared_probe", -1, costSharedEnvStart);')
block = tail[lo:hi]

needle = '    skyDome.infoBag->zTestType = prevZTest;'
assert block.count(needle) == 1, needle
block = block.replace(needle, needle + '\n' + close('env_probe_sky').rstrip('\n'), 1)
needle = '''      for (GeoPart& part : objectGeometry[ri].parts)
        if (part.bag) ++districtInv::bags, stapip.core.render(part.bag.get());'''
assert block.count(needle) == 1, 'probe object loop shape changed'
block = block.replace(needle, needle + '''
      { const Tyra::StaPipTelemetry dmI = stapip.core.takeTelemetry();
        districtInv::addObject(districtInv::probeObj[districtInv::phase()], ri,
                               ro.data.type, ro.data.model, dmI);
        districtInv::add(%d, dmI); }''' % IDX['env_probe_objs'], 1)
tail = tail[:lo] + block + tail[hi:]

# The reflection reuse gate (1.106.0), when the tree under test has one: record
# its verdict and the staleness it permitted, in the same target pixels the
# budget is written in. Optional on purpose - this instrument must still apply
# to a tree from before the gate existed, or it cannot measure the control.
needle = '      envReuseOk = REFLECTION_REUSE_BUDGET > 0.0F &&\n' \
         '                   drift <= REFLECTION_REUSE_BUDGET;'
if tail.count(needle) == 1:
    tail = tail.replace(
        needle, needle + '\n      districtInv::reuse(envReuseOk, drift);', 1)
    print('  (reflection reuse gate found - recording its verdict)')
else:
    print('  (no reflection reuse gate in this tree - the reuse row is zeros)')

# The per-object submit block sits INSIDE the object loop, so its drain both
# charges the object_submit slot and files a row against the object's index.
opener = '    const u32 costObjectStart=costStart();'
closer = '    costEnd("Object",i,costObjectStart);'
assert tail.count(opener) == 1 and tail.count(closer) == 1
tail = tail.replace(opener, OPEN + opener, 1)
tail = tail.replace(closer, closer + '''
  { const Tyra::StaPipTelemetry dmI = stapip.core.takeTelemetry();
    districtInv::addObject(districtInv::obj[districtInv::phase()], i,
                           runtimeObjects[i].data.type,
                           runtimeObjects[i].data.model, dmI);
    districtInv::add(%d, dmI); }''' % IDX['object_submit'], 1)

# Roads are procedural chunks with owner == -3 (docs/roads.md); everything else
# renderProcChunks draws is a volume or a prefab instance. Splitting them needs
# the drain INSIDE the chunk loop, because the two kinds interleave.
needle = '    ++districtInv::bags, stapip.core.render(c.bag.get());\n  }\n}'
assert tail.count(needle) == 1, 'renderProcChunks shape changed'
tail = tail.replace(needle, '''    ++districtInv::bags, stapip.core.render(c.bag.get());
    { const Tyra::StaPipTelemetry dmI = stapip.core.takeTelemetry();
      districtInv::add(c.owner == -3 ? %du : %du, dmI); }
  }
}''' % (IDX['roads'], IDX['proc_other']), 1)

path.write_text(s[:start] + helper + loop + tail, encoding='utf-8')
print('Instrumented for inventory:', path)
