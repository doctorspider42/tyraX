#!/usr/bin/env python3
"""TyraX PS2 timing rig: instrument a copied project + engine copy (tools/ps2rig/README.md).

usage: make_arm.py <project> <engine-root-copy> [--day] [--ndebug]
<project> must already be generated (--build done once). Patches are idempotent
only against pristine generated files: re-run --refresh-gen / re-copy first.
"""
import sys, shutil, pathlib, re

proj = pathlib.Path(sys.argv[1])
eng_root = pathlib.Path(sys.argv[2])
eng = eng_root / 'engine'
day = '--day' in sys.argv
ndebug = '--ndebug' in sys.argv
opt = next((a.split('=',1)[1] for a in sys.argv if a.startswith('--opt=')), None)
rig = pathlib.Path(__file__).parent

def edit(path, old, new, count=1):
    t = path.read_text(encoding='utf-8-sig')
    n = t.count(old)
    if n != count:
        raise SystemExit(f'anchor count {n}!={count} in {path}: {old[:70]!r}')
    path.write_bytes(t.replace(old, new).encode())

def include(path, header='debug/cld_probe.hpp'):
    t = path.read_text(encoding='utf-8-sig')
    if 'cld_probe.hpp' not in t:
        path.write_bytes((f'#include "{header}"\n' + t).encode())

# ---------------- engine ----------------
shutil.copy(rig / 'cld_probe.hpp', eng / 'inc/debug/cld_probe.hpp')
edit(eng / 'inc/debug/debug.hpp',
     '#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n")',
     '#define TYRA_LOG(...) ((void)0) // Claude night rig: quiet')
edit(eng / 'src/renderer/core/paths/path1/vif1_queue.cpp',
     'if (++frames % 120 == 0) {', 'if ((++frames % 120 == 0) && false) {')
p = eng / 'src/engine.cpp'; include(p)
edit(p, 'void Engine::realLoop() {',
     'void Engine::realLoop() {\n  Cld::begin(renderer.core.getStallTotal());')
p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'; include(p)
edit(p, 'void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {',
     'void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {\n  Cld::S cldCore("core");')
p = eng / 'src/renderer/core/paths/path1/vif1_queue.cpp'; include(p)
edit(p, 'u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {',
     'u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {\n  Cld::S cldSub("q.submit");')
edit(p, 'void Vif1Queue::flushRecording() {',
     'void Vif1Queue::flushRecording() {\n  Cld::S cldFl("q.flushrec");')
edit(p, 'void Vif1Queue::waitFor(u32 sequence) {',
     'void Vif1Queue::waitFor(u32 sequence) {\n  Cld::S cldW("q.waitFor");')
# ---- fine scopes inside StaPipCore::render ----
if '--lean' not in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '  TYRA_ATTRIB_INC(renderCalls);\n', '  TYRA_ATTRIB_INC(renderCalls);\n  const Cld::M cldT0 = Cld::mark();\n')
    edit(p, '    if (frustumCheck == OUTSIDE_FRUSTUM) {\n', '    if (frustumCheck == OUTSIDE_FRUSTUM) {\n      Cld::span("c.culled", cldT0, Cld::mark());\n')
    edit(p, '  HardwareTrace::Scope traceBounds("Bounds", HardwareTrace::Kind::Span, true);\n', '  const Cld::M cldTa = Cld::mark(); Cld::span("c.head", cldT0, cldTa);\n  HardwareTrace::Scope traceBounds("Bounds", HardwareTrace::Kind::Span, true);\n')
    edit(p, '    bbox = cacher.getBBoxes(bag->vertices, bag->count,\n', '    const Cld::M cldTc = Cld::mark();\n    bbox = cacher.getBBoxes(bag->vertices, bag->count,\n')
    edit(p, '                            bag->bboxVersion, maxVertCount);\n', '                            bag->bboxVersion, maxVertCount);\n    Cld::span("c.b.getBBoxes", cldTc, Cld::mark());\n')
    edit(p, '  const u32 prepareStart = telemetryEnabled', '  const Cld::M cldT1 = Cld::mark(); Cld::span("c.bounds", cldTa, cldT1);\n  const u32 prepareStart = telemetryEnabled')
    edit(p, '  RendererCoreTextureBuffers texBuffersStorage;\n', '  const Cld::M cldTt = Cld::mark(); Cld::span("c.p.pre", cldT1, cldTt);\n  RendererCoreTextureBuffers texBuffersStorage;\n')
    edit(p, '  qbufferRenderer.setBagWrap(&wantedWrap);\n', '  qbufferRenderer.setBagWrap(&wantedWrap);\n  const Cld::M cldTw = Cld::mark(); Cld::span("c.p.texture", cldTt, cldTw);\n')
    edit(p, '  qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);\n', '  const Cld::M cldT2 = Cld::mark(); Cld::span("c.p.lightetc", cldTw, cldT2);\n  qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);\n  const Cld::M cldT3 = Cld::mark(); Cld::span("c.objdata", cldT2, cldT3);\n')
    edit(p, '  bool replayedWhole = false;\n', '  bool replayedWhole = false;\n  const Cld::M cldTr = Cld::mark(); Cld::span("c.d.pre", cldT3, cldTr);\n')
    edit(p, '  TYRA_ATTRIB_ADD(dsDirectTicks, attribReplayStart);\n  TYRA_ATTRIB_MARK(attribRetainStart);\n', '  TYRA_ATTRIB_ADD(dsDirectTicks, attribReplayStart);\n  Cld::span("c.d.bakeAndReplay", cldTr, Cld::mark());\n  TYRA_ATTRIB_MARK(attribRetainStart);\n')
    edit(p, '  TYRA_ATTRIB_MARK(attribFlushStart);\n', '  const Cld::M cldT4 = Cld::mark();\n  Cld::span(replayedWhole ? "c.d.replay" : directRoute ? "c.d.direct" : bag->stripped ? "c.d.partStrip" : "c.d.partial", cldT3, cldT4);\n  TYRA_ATTRIB_MARK(attribFlushStart);\n')
    edit(p, '  TYRA_ATTRIB_ADD(dsFlushTicks, attribFlushStart);\n', '  TYRA_ATTRIB_ADD(dsFlushTicks, attribFlushStart);\n  Cld::span("c.flush", cldT4, Cld::mark());\n')
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    edit(p, 'void TerrainGame::dripHeavy(bool all) {\n', 'void TerrainGame::dripHeavy(bool all) {\n  Cld::S cldDrip("G.dripHeavy");\n')

if ndebug:
    edit(eng / 'inc/debug/debug.hpp', '#define TYRA_ASSERT(condition, ...) \\\n  if (!(condition)) TyraDebug::trap(__FILE__, __LINE__, ##__VA_ARGS__)', '#define TYRA_ASSERT(condition, ...) ((void)0) // Claude rig: asserts compiled out')
mb = eng_root / 'Makefile.base'
flags = '-D_EE -G0 -Wall ' + ('-' + opt if opt else '-O3') + (' -DCLD_EV0=12u' if '--count-instr' in sys.argv else '') + (' -DCLD_NOPMU=1' if '--lean' in sys.argv else '')
import os
PGO_PREFIX = os.environ.get('PGO_PREFIX', os.path.expanduser('~/.cache/tyrax/native'))  # WSL native-build cache
pgo_use = next((a.split('=',1)[1] for a in sys.argv if a.startswith('--pgo-use=')), None)
if '--pgo-gen' in sys.argv:
    p = eng / 'src/engine.cpp'
    p.write_bytes(p.read_bytes() + b'\nextern "C" void __gcov_dump(void);\nnamespace Cld { void cldGcovDump() { __gcov_dump(); } }\n')
    # engine only: the game's own TUs stay uninstrumented to save RAM
    flags += ' -DCLD_PGO_GEN=1'
    edit(eng / 'Makefile', 'CFLAGS      := \n', 'CFLAGS      := -fprofile-arcs -fprofile-dir=host: -fprofile-prefix-path=' + PGO_PREFIX + ' -fprofile-update=single\n')
    edit(mb, 'LINKFLAGS\t+= -O3 -Wl,-zmax-page-size=128', 'LINKFLAGS\t+= -O3 -Wl,-zmax-page-size=128 -fprofile-arcs')
if pgo_use:
    edit(eng / 'Makefile', 'CFLAGS      := \n', 'CFLAGS      := -fprofile-use=' + pgo_use + ' -fprofile-prefix-path=' + PGO_PREFIX + ' -Wno-missing-profile -Wno-coverage-mismatch\n')
edit(mb, 'CFLAGS      += -D_EE -G0 -Wall -O3', 'CFLAGS      += ' + flags)

# ---------------- game ----------------
p = proj / 'src/terrain_game.cpp'; include(p)
edit(p, 'void TerrainGame::loop() {', 'void TerrainGame::loop() {\n  const unsigned cldLoop0 = Cld::ticks();')
edit(p, '  inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);\n',
     '  inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);\n'
     '  { Tyra::PadButtons neutral{}; engine->pad.setState(neutral,neutral,128,128,128,128); }\n')
edit(p, '  if (scriptCtx.requestScene >= 0) {',
     '  if (Cld::resetRequested) { scriptCtx.requestScene = 0; Cld::resetRequested = false; }\n'
     '  if (scriptCtx.requestScene >= 0) {')
edit(p, '  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));\n  {',
     '  Cld::scope("G.update", Cld::ticks() - cldLoop0);\n  const unsigned cldR0 = Cld::ticks();\n'
     '  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));\n'
     '  Cld::scope("G.beginFrame", Cld::ticks() - cldR0);\n  {')
edit(p, '    } else {\n      renderScene();\n    }',
     '    } else {\n      Cld::S cldScene("G.scene");\n      renderScene();\n    }')
edit(p, '  engine->renderer.endFrame();\n  // Frame extrapolation',
     '  Cld::scope("G.renderAll", Cld::ticks() - cldR0);\n'
     '  { Cld::S cldEnd("G.endFrame");\n  engine->renderer.endFrame(); }\n  // Frame extrapolation')

p = proj / 'src/gen/game_physics.gen.cpp'; include(p)
edit(p, '  auto costStart = [&]() -> u32 { return (costSeq || (Tyra::HardwareTrace::active && Tyra::HardwareTrace::detailed)) ? profTicks() : 0; };',
     '  auto costStart = [&]() -> u32 { return profTicks(); };')
edit(p, '  auto costEnd = [&](const char* label, int object, u32 start) {\n',
     '  auto costEnd = [&](const char* label, int object, u32 start) {\n    Cld::scope(label, profTicks() - start);\n')

p = proj / 'src/gen/game_vehicles.gen.cpp'; include(p)
edit(p, '    } else if (v.wpCount > 0 && (s.damageMechanical <= 0.5F || v.damage < 0.999F)) {',
     '''      // Claude night rig: full pedal straight up the boulevard, then hold.
      Cld::carZ = v.pos[2]; Cld::carSpeed = v.speed;
      { const bool g = Cld::gas && v.pos[2] < 100.0F;
        inThrottle = g ? 1.0F : 0.0F; inBrake = 0.0F; inSteer = 0.0F; inHand = g ? 0 : 1; inNos = 0; }
    } else if (v.wpCount > 0 && (s.damageMechanical <= 0.5F || v.damage < 0.999F)) {''')

p = proj / 'src/scripts/district_mood.cpp'
edit(p, '    const bool night = ctx.saveValues && DISTRICT_NIGHT_VALUE < ctx.saveValueCount &&\n                       ctx.saveValues[DISTRICT_NIGHT_VALUE] >= 0.5F;',
     f'    const bool night = {"false" if day else "true"};  // Claude night rig')
print('patched', proj, eng_root, 'day' if day else 'night', 'NDEBUG' if ndebug else '')

# ---- optional: cold/warm duplicate-call experiment (parked state only) ----
if '--dup' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, 'void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {\n  Cld::S cldCore("core");',
         'void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {\n'
         '  static bool cldInner = false;\n'
         '  if (!cldInner && Cld::state == 1) {\n'
         '    cldInner = true;\n'
         '    { Cld::S a("dup.cold"); render(bag, reception); }\n'
         '    { Cld::S b("dup.warm"); render(bag, reception); }\n'
         '    cldInner = false;\n'
         '    return;\n'
         '  }\n'
         '  Cld::S cldCore("core");')
    print('dup experiment on')

# ---- PS2-way: wider submission batching ----
bs = next((a.split('=',1)[1] for a in sys.argv if a.startswith('--batchsize=')), None)
if bs:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    edit(p, 'constexpr u16 kSubmissionBatchSize = 4;', 'constexpr u16 kSubmissionBatchSize = ' + bs + ';')
    print('batch size', bs)
if '--batchscope' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  renderTerrain();\n  costEnd("Terrain",-1,costTerrainStart);',
         '  stapip.core.beginSubmissionBatch();\n  renderTerrain();\n  costEnd("Terrain",-1,costTerrainStart);')
    edit(p, '  { const u32 ct=costStart(); renderProcChunks(); costEnd("Procedural",-1,ct); }\n',
         '  { const u32 ct=costStart(); renderProcChunks(); costEnd("Procedural",-1,ct); }\n  stapip.core.endSubmissionBatch();\n')
    edit(p, '  { const u32 ct=costStart(); renderVehicleWheels(); costEnd("Wheels",-1,ct); }',
         '  stapip.core.beginSubmissionBatch();\n  { const u32 ct=costStart(); renderVehicleWheels(); costEnd("Wheels",-1,ct); }\n  stapip.core.endSubmissionBatch();')
    print('batch scope widened')

# ---- runtime A/B: arm 1 = wider batch scope + batch size 12, arm 0 = stock ----
if '--ab-batch' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    include(p)
    edit(p, 'constexpr u16 kSubmissionBatchSize = 4;', 'constexpr u16 kSubmissionBatchSize = 12;  // allocation; threshold below is runtime A/B')
    edit(p, '    } else if (++submissionBatchBags >= kSubmissionBatchSize) {', '    } else if (++submissionBatchBags >= (Cld::arm ? 12 : 4)) {')
    edit(p, '      submissionBatchBags >= kSubmissionBatchSize) {', '      submissionBatchBags >= (Cld::arm ? 12 : 4)) {')
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  renderTerrain();\n  costEnd("Terrain",-1,costTerrainStart);',
         '  const bool cldWide = Cld::arm != 0;\n  if (cldWide) stapip.core.beginSubmissionBatch();\n  renderTerrain();\n  costEnd("Terrain",-1,costTerrainStart);')
    edit(p, '  { const u32 ct=costStart(); renderProcChunks(); costEnd("Procedural",-1,ct); }\n',
         '  { const u32 ct=costStart(); renderProcChunks(); costEnd("Procedural",-1,ct); }\n  if (cldWide) stapip.core.endSubmissionBatch();\n')
    edit(p, '  { const u32 ct=costStart(); renderVehicleWheels(); costEnd("Wheels",-1,ct); }',
         '  if (cldWide) stapip.core.beginSubmissionBatch();\n  { const u32 ct=costStart(); renderVehicleWheels(); costEnd("Wheels",-1,ct); }\n  if (cldWide) stapip.core.endSubmissionBatch();')
    print('runtime A/B batch')

# ---- per-producer bag counts ----
if '--bagcount' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '  Cld::S cldCore("core");', '  Cld::S cldCore("core");\n  ++Cld::coreCalls;')
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  auto costStart = [&]() -> u32 { return profTicks(); };', '  auto costStart = [&]() -> u32 { Cld::bagPush(); return profTicks(); };')
    edit(p, '    Cld::scope(label, profTicks() - start);\n', '    Cld::scope(label, profTicks() - start);\n    Cld::bagPop(label);\n')
    print('bag counts')

# ---- snapshot copy volume ----
if '--copycount' in sys.argv:
    p = eng / 'inc/renderer/core/paths/path1/frame_chain_arena.hpp'
    edit(p, '#include "debug/hardware_trace.hpp"\n', '#include "debug/hardware_trace.hpp"\n#include "debug/cld_probe.hpp"\n')
    edit(p, '    cursor += needed;\n', '    Cld::copyChain += qwords * 16; Cld::copyMut += payload - qwords * 16; Cld::borrowB += borrowed; ++Cld::snaps;\n    cursor += needed;\n', count=2)
    print('copy counters')

# ---- stamps A/B: both arms widen the batch scope; arm 1 enables stamps ----
if '--ab-stamp' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  renderTerrain();\n  costEnd("Terrain",-1,costTerrainStart);',
         '  stapip.core.beginSubmissionBatch();\n  renderTerrain();\n  costEnd("Terrain",-1,costTerrainStart);')
    edit(p, '  { const u32 ct=costStart(); renderProcChunks(); costEnd("Procedural",-1,ct); }\n',
         '  { const u32 ct=costStart(); renderProcChunks(); costEnd("Procedural",-1,ct); }\n  stapip.core.endSubmissionBatch();\n')
    edit(p, '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n',
         '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n'
         '  stapip.core.setStampEnabled(Cld::arm != 0);\n'
         '  engine->audio.idleSleepMs = Cld::arm >= 1 ? 16 : 0;\n'
         '  Cld::stampHits += stapip.core.takeStampHits(); Cld::stampCaps += stapip.core.takeStampCaptures();\n')
    print('stamp A/B')

# ---- stamp capture diagnostics ----
if '--stampdiag' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '  if (qbufferRenderer.isStampEnabled() && frustumCull &&\n      frustumCheck == IN_FRUSTUM && bbox != nullptr && !blssOn &&',
         '''  if (qbufferRenderer.isStampEnabled()) {
    static unsigned why[12]; static unsigned calls = 0;
    ++calls;
    if (!(frustumCull && frustumCheck == IN_FRUSTUM)) ++why[0];
    else if (bbox == nullptr) ++why[1];
    else if (false) ++why[2];
    else if (bag->lighting != nullptr) ++why[3];
    else if (!cheapBatchCandidate) ++why[4];
    else if (!residentTexture || !repeatTexture) ++why[5];
    else if (!isIdentity(*bag->info->model)) ++why[6];
    else if (bag->color->single != nullptr) ++why[7];
    else ++why[8];
    if (calls % 4000 == 0) { printf("STAMPWHY notIn=%u nobbox=%u light=%u lit=%u notCand=%u tex=%u notIdent=%u single=%u ok=%u\\n", why[0],why[1],why[2],why[3],why[4],why[5],why[6],why[7],why[8]); fflush(stdout); }
  }
  if (qbufferRenderer.isStampEnabled() && frustumCull &&
      frustumCheck == IN_FRUSTUM && bbox != nullptr && !blssOn &&''')
    print('stamp diag')
if '--stampdiag' in sys.argv:
    edit(eng / 'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp',
         '#define TYRA_STAPIP_STAMP_DIAG 0', '#define TYRA_STAPIP_STAMP_DIAG 1')

# ---- partial-bag diagnostics ----
if '--partdiag' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '    TYRA_ATTRIB_INC(dsPartialBags);\n',
         '    TYRA_ATTRIB_INC(dsPartialBags);\n'
         '    { static unsigned pd = 0; if ((++pd % 97) == 0) { u8 cm = 0; CoreBBox::frustumCheckAABB(objectSpacePlanes, (*bbox->getMainBBox())[0], (*bbox->getMainBBox())[7], &cm);\n'
         '      const u8 gm = CoreBBox::activePlaneMaskAABB(clipObjectSpacePlanes, (*bbox->getMainBBox())[0], (*bbox->getMainBBox())[7], 8);\n'
         '      printf("PARTDIAG bag=%p n=%d maxv=%u strip=%d clipchk=%d viewmask=%02x clipmask=%02x tex=%d\\n", (void*)bag, (int)bag->count, (unsigned)maxVertCount, (int)bag->stripped, (int)bag->info->fullClipChecks, cm, gm, bag->texture ? 1 : 0); } }\n')
    print('partial diag')

# ---- wheels on VU (arm 2) ----
if '--wheelvu' in sys.argv:
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    edit(p, 'void TerrainGame::renderVehicleWheels() {\n',
         (rig / 'wheelvu.inc').read_text() + 'void TerrainGame::renderVehicleWheels() {\n')
    t = p.read_text(encoding='utf-8-sig')
    i = t.index('void TerrainGame::renderVehicleWheels() {\n')
    head, tail = t[:i], t[i:]
    a = '  bool changed = false;\n'
    assert tail.count(a) >= 1
    tail = tail.replace(a, a + '  bool cldVuUsed = false;\n', 1)
    b = '    if (nredo > 0) {\n'
    assert tail.count(b) == 1, tail.count(b)
    tail = tail.replace(b, (rig / 'wheelvu_block.inc').read_text() + b, 1)
    c = '  if (slot == 0 || !src) continue;\n'
    assert tail.count(c) == 1
    tail = tail.replace(c, c + '  if (cldVuUsed) continue;\n', 1)
    p.write_bytes((head + tail).encode())
    print('wheel vu')

# ---- lean reflection probe (arm 2): base terrain pass only inside the probe ----
if '--probelean' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '    g_groundDrawRadius = REFLECTION_GROUND_RADIUS;\n    renderTerrain();\n',
         '    g_groundDrawRadius = REFLECTION_GROUND_RADIUS;\n    Cld::probePass = Cld::arm == 2;\n    renderTerrain();\n    Cld::probePass = false;\n')
    edit(p, '    // adjacent also keeps the texture cache warm per chunk).\n    for (TerrainChunk::LayerPass& lp : ch.layerPasses)\n',
         '    // adjacent also keeps the texture cache warm per chunk).\n    if (Cld::probePass) continue;  // Claude rig: base pass only in the 128px probe\n    for (TerrainChunk::LayerPass& lp : ch.layerPasses)\n')
    print('probe lean')

# ---- probe internals ----
if '--probescopes' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '    core.envMap.begin(Color(scriptCtx.skyColor.r, scriptCtx.skyColor.g,\n                            scriptCtx.skyColor.b, 128.0F));\n',
         '    { Cld::S cldPb("P.begin");\n    core.envMap.begin(Color(scriptCtx.skyColor.r, scriptCtx.skyColor.g,\n                            scriptCtx.skyColor.b, 128.0F)); }\n')
    edit(p, '    renderWorldBag(skyDome.bag.get());\n    renderSkyBodies(probeEye, envLook);\n',
         '    { Cld::S cldPs("P.sky");\n    renderWorldBag(skyDome.bag.get());\n    renderSkyBodies(probeEye, envLook); }\n')
    edit(p, '    renderTerrain();\n    Cld::probePass = false;\n    renderRoadChunks();\n',
         '    { Cld::S cldPt("P.terrain"); renderTerrain(); }\n    Cld::probePass = false;\n    { Cld::S cldPr("P.roads"); renderRoadChunks(); }\n    Cld::S* cldPo = new (alloca(sizeof(Cld::S))) Cld::S("P.objects");\n')
    edit(p, '    core.renderer3D.popEnvView(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));\n    core.envMap.end();\n',
         '    cldPo->~S();\n    core.renderer3D.popEnvView(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));\n    { Cld::S cldPe("P.end"); core.envMap.end(); }\n')
    print('probe scopes')

# ---- sky bodies as local quad + matrix (arm 2) ----
if '--skybodies' in sys.argv:
    p = proj / 'src/gen/game_scene.gen.cpp'
    include(p)
    edit(p, '    b.verts[0] = c0;\n', (rig / 'skybody_block.inc').read_text() + '    b.verts[0] = c0;\n')
    print('sky bodies')

# ---- probe cadence 4 + terrain budget 1 (arm 2) ----
if '--cadence' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '      ((envMapTick++) & (adaptiveReduced ? 3U : 1U)) == 0;',
         '      ((envMapTick++) & ((adaptiveReduced || Cld::arm == 2) ? 3U : 1U)) == 0;')
    edit(p, '                        p2Focus ? players[1].z : 0.0F, p2Focus, 2);',
         '                        p2Focus ? players[1].z : 0.0F, p2Focus, Cld::arm == 2 ? 1 : 2);')
    print('cadence')

# ---- amortised probe (arm 2) ----
if '--probesplit' in sys.argv:
    edit(eng / 'inc/renderer/core/envmap/renderer_core_envmap.hpp',
         '  bool doubleBuffered = false;', '  bool doubleBuffered = true;  // Claude rig')
    p = proj / 'src/gen/game_physics.gen.cpp'
    a = ('  if (!ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 && skyDome.bag &&\n'
         '      (refreshEnvMap || !sharedEnvBasisValid) && !envReuseOk &&\n'
         '      !splitPassActive) {\n'
         '    const u32 costSharedEnvStart = costStart();\n')
    edit(p, a, '  if (Cld::arm == 2) {\n' + (rig / 'probe_split.inc').read_text() +
         '  } else\n' + a)
    print('probe split')

# ---- terrain streaming scope + budget 1 in arm 2 ----
if '--tstream' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '    updateTerrainChunks(terrainFocusX, terrainFocusZ,\n',
         '    Cld::S cldTs("T.stream");\n    updateTerrainChunks(terrainFocusX, terrainFocusZ,\n')
    edit(p, '                        p2Focus ? players[1].z : 0.0F, p2Focus, 2);',
         '                        p2Focus ? players[1].z : 0.0F, p2Focus, Cld::arm == 2 ? 1 : 2);')
    print('terrain stream')

# ---- terrain streaming before the probe; probe yields on build frames (arm 2) ----
if '--streamfirst' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  if (!splitSecondPass) {\n    const bool p2Focus = playerTwoActive && players[1].objIndex >= 0;\n',
         '  if (!splitSecondPass && Cld::arm != 2) {\n    const bool p2Focus = playerTwoActive && players[1].objIndex >= 0;\n')
    edit(p, '  if (Cld::arm == 2) {\n    // Claude rig (arm 2): the shared probe amortised',
         '  Cld::streamBusy = false;\n'
         '  if (Cld::arm == 2 && !splitSecondPass) {\n'
         '    const bool p2Focus = playerTwoActive && players[1].objIndex >= 0;\n'
         '    const unsigned cldT0 = Cld::ticks();\n'
         '    updateTerrainChunks(terrainFocusX, terrainFocusZ,\n'
         '                        p2Focus ? players[1].x : 0.0F,\n'
         '                        p2Focus ? players[1].z : 0.0F, p2Focus, 1);\n'
         '    const unsigned cldDt = Cld::ticks() - cldT0;\n'
         '    Cld::scope("T.stream", cldDt);\n'
         '    Cld::streamBusy = cldDt > 88473u;  // > 0.3 ms: a chunk was built\n'
         '  }\n'
         '  if (Cld::arm == 2) {\n    // Claude rig (arm 2): the shared probe amortised')
    print('stream first')

# ---- per-label route counts (needs bag counts) ----
if '--routes' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '      Cld::span("c.culled", cldT0, Cld::mark());\n',
         '      Cld::span("c.culled", cldT0, Cld::mark());\n      ++Cld::routes[4];\n')
    edit(p, '  Cld::span(replayedWhole ? "c.d.replay" : directRoute ? "c.d.direct" : bag->stripped ? "c.d.partStrip" : "c.d.partial", cldT3, cldT4);\n',
         '  Cld::span(replayedWhole ? "c.d.replay" : directRoute ? "c.d.direct" : bag->stripped ? "c.d.partStrip" : "c.d.partial", cldT3, cldT4);\n'
         '  ++Cld::routes[replayedWhole ? 0 : directRoute ? 1 : bag->stripped ? 3 : 2];\n')
    edit(p, '            &kNoSpotLight))\n      return;\n',
         '            &kNoSpotLight)) {\n      ++Cld::routes[5];\n      return;\n    }\n')
    print('routes')

# ---- wheel internals: bake vs submit, per-event counts ----
if '--wheelscopes' in sys.argv:
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    edit(p, 'void TerrainGame::renderVehicleWheels() {\n  if (vehicleCount_ <= 0) return;\n',
         'void TerrainGame::renderVehicleWheels() {\n  if (vehicleCount_ <= 0) return;\n  Cld::S cldWAll("W.all");\n')
    edit(p, '    if (nredo > 0) {\n      changed = true;\n',
         '    Cld::scope("W.car", 0);\n    if (nredo > 0) {\n      Cld::S cldWb("W.rebake");\n      changed = true;\n')
    edit(p, '  stapip.core.render(wheelBag_.get(), receiverClasses == 2 && receiverClass == 0 ?\n',
         '  Cld::scope(changed ? "W.bagChanged" : "W.bagSame", 0);\n  const unsigned cldWs0 = Cld::ticks();\n  stapip.core.render(wheelBag_.get(), receiverClasses == 2 && receiverClass == 0 ?\n')
    edit(p, '      Tyra::StaPipLightReception::Suppress : Tyra::StaPipLightReception::FromInfo);\n  }\n  }\n#if TYRA_WHEEL_REBUILD_REPORT\n  // Same 300-frame',
         '      Tyra::StaPipLightReception::Suppress : Tyra::StaPipLightReception::FromInfo);\n  Cld::scope("W.submit", Cld::ticks() - cldWs0);\n  }\n  }\n#if TYRA_WHEEL_REBUILD_REPORT\n  // Same 300-frame')
    print('wheel scopes')

# ---- per-batch wheel bags (arm >= 1): a shared bag pointer defeats stamps ----
if '--wheelbags' in sys.argv:
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    t = p.read_text(encoding='utf-8-sig')
    i = t.index('void TerrainGame::renderVehicleWheels() {\n')
    j = t.index('void TerrainGame::renderRoadChunks() {')
    body = t[i:j]
    for name in ('wheelColorBag_', 'wheelTexBag_', 'wheelBag_'):
        body = body.replace(name, 'CldWB_' + name + '[cldWbi]')
    a = '  WheelBatch& batch = wheelBatches_[(size_t)batchIndex];\n'
    assert body.count(a) == 1
    body = body.replace(a, a + '  const int cldWbi = Cld::arm >= 1 ? (batchIndex & 15) : 0;\n')
    decl = ('inline std::unique_ptr<Tyra::StaPipBag> CldWB_wheelBag_[16];\n'
            'inline std::unique_ptr<Tyra::StaPipColorBag> CldWB_wheelColorBag_[16];\n'
            'inline std::unique_ptr<Tyra::StaPipTextureBag> CldWB_wheelTexBag_[16];\n')
    p.write_bytes((t[:i] + decl + body + t[j:]).encode())
    print('wheel bags')

# ---- GPU-side A/B: arm 1 drops particles/smoke/skids, arm 2 drops night light effects ----
if '--gpuab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '    if (ps.bag && ps.bag->count > 0) renderWorldBag(ps.bag.get());\n  renderVehicleGlass();\n  renderVehicleSkids();\n  renderVehicleSmoke();\n',
         '    if (ps.bag && ps.bag->count > 0 && Cld::arm != 1) renderWorldBag(ps.bag.get());\n  renderVehicleGlass();\n  if (Cld::arm != 1) renderVehicleSkids();\n  if (Cld::arm != 1) renderVehicleSmoke();\n')
    edit(p, 'updateAndRenderLightPools();', 'if (Cld::arm != 2) updateAndRenderLightPools();')
    edit(p, 'updateAndRenderLightBeams();', 'if (Cld::arm != 2) updateAndRenderLightBeams();')
    edit(p, 'renderVehicleGlow();', 'if (Cld::arm != 2) renderVehicleGlow();')
    edit(p, 'renderVehicleLampGlow();', 'if (Cld::arm != 2) renderVehicleLampGlow();')
    print('gpu A/B')

# ---- split of the night lights: arm 1 = no pools, arm 2 = no beams + car glows ----
if '--lightsplit' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, 'updateAndRenderLightPools();', 'if (Cld::arm != 1) updateAndRenderLightPools();')
    edit(p, 'updateAndRenderLightBeams();', 'if (Cld::arm != 2) updateAndRenderLightBeams();')
    edit(p, 'renderVehicleGlow();', 'if (Cld::arm != 2) renderVehicleGlow();')
    edit(p, 'renderVehicleLampGlow();', 'if (Cld::arm != 2) renderVehicleLampGlow();')
    print('light split')

# ---- arm 1 = no lamp beam CONES (keep coronas), arm 2 = no car headlight glow ----
if '--conesplit' in sys.argv:
    p = proj / 'src/gen/game_lighting.gen.cpp'
    t = p.read_text(encoding='utf-8-sig')
    a = '      renderWorldBag(bb.coneBag.get());\n'
    b = '    renderWorldBag(bb.coneBag.get());\n'
    assert t.count(a) == 1 and t.count(b) == 2, (t.count(a), t.count(b))
    t = t.replace(a, '      if (Cld::arm != 1) renderWorldBag(bb.coneBag.get());\n')
    t = t.replace(b, '    if (Cld::arm != 1) renderWorldBag(bb.coneBag.get());\n')
    p.write_bytes(t.encode())
    include(p)
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, 'renderVehicleGlow();', 'if (Cld::arm != 2) renderVehicleGlow();')
    print('cone split')

# ---- GPU ablation (day): arm 1 = terrain base pass only, arm 2 = no shared probe ----
if '--terrainab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '    for (TerrainChunk::LayerPass& lp : ch.layerPasses)\n      if (lp.bag && lp.bag->count > 0) renderWorldBag(lp.bag.get());\n',
         '    if (Cld::arm == 1) continue;  // Claude rig: base pass only\n    for (TerrainChunk::LayerPass& lp : ch.layerPasses)\n      if (lp.bag && lp.bag->count > 0) renderWorldBag(lp.bag.get());\n')
    edit(p, '  const bool envWanted = !ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 &&\n',
         '  const bool envWanted = Cld::arm != 2 && !ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 &&\n')
    print('terrain/probe ablation')

# ---- GPU-state sampler during the frame-completion waits ----
if '--gpustat' in sys.argv:
    p = eng / 'src/renderer/core/renderer_core.cpp'
    include(p)
    edit(p, '    Vif1Queue::waitFor(pipelineSequence); }\n',
         '    Cld::gpuSampling = true; Vif1Queue::waitFor(pipelineSequence); }\n')
    edit(p, '  sync.waitAndClear();\n  HardwareTrace::Scope tracePacing("Present"',
         '  sync.waitAndClear();\n  Cld::gpuSampling = false;\n  HardwareTrace::Scope tracePacing("Present"')
    p = eng / 'src/renderer/core/paths/path1/vif1_queue.cpp'
    edit(p, '  while (static_cast<s32>(completed - sequence) < 0) advanceIfIdle();\n',
         '  while (static_cast<s32>(completed - sequence) < 0) { advanceIfIdle(); Cld::gsample(); }\n')
    p = eng / 'src/renderer/core/renderer_core_sync.cpp'
    include(p)
    edit(p, '  while (!check()) {\n  }\n', '  while (!check()) {\n    if (Cld::gpuSampling) Cld::gsample();\n  }\n')
    print('gpu sampler')
arms = next((a.split('=',1)[1] for a in sys.argv if a.startswith('--arms=')), None)
if arms:
    edit(eng / 'inc/debug/cld_probe.hpp', '#define CLD_ARMS 3', '#define CLD_ARMS ' + arms)
    print('arms', arms)

# ---- 6-arm GPU ablation: 1 no car glow, 2 no lamp pools, 3 terrain base only, 4 no probe, 5 day ----
if '--ablate6' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, 'renderVehicleGlow();', 'if (Cld::arm != 1) renderVehicleGlow();')
    edit(p, 'updateAndRenderLightPools();', 'if (Cld::arm != 2) updateAndRenderLightPools();')
    edit(p, '    for (TerrainChunk::LayerPass& lp : ch.layerPasses)\n      if (lp.bag && lp.bag->count > 0) renderWorldBag(lp.bag.get());\n',
         '    if (Cld::arm == 3) continue;  // Claude rig: base pass only\n    for (TerrainChunk::LayerPass& lp : ch.layerPasses)\n      if (lp.bag && lp.bag->count > 0) renderWorldBag(lp.bag.get());\n')
    edit(p, '  const bool envWanted = !ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 &&\n',
         '  const bool envWanted = Cld::arm != 4 && !ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 &&\n')
    p = proj / 'src/scripts/district_mood.cpp'
    include(p)
    edit(p, '    const bool night = true;  // Claude night rig', '    const bool night = Cld::arm != 5;  // Claude rig: arm 5 = day')
    print('ablate6')

# ---- terrain pass split (6 arms): 1 no layers, 2 no lightmap passes, 3 base only + no probe, 4 no probe, 5 day ----
if '--terrain6' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '    for (TerrainChunk::LayerPass& lp : ch.layerPasses)\n      if (lp.bag && lp.bag->count > 0) renderWorldBag(lp.bag.get());\n',
         '    if (Cld::arm == 3) continue;  // Claude rig: base pass only\n    for (TerrainChunk::LayerPass& lp : ch.layerPasses)\n      if (Cld::arm != 1 && lp.bag && lp.bag->count > 0) renderWorldBag(lp.bag.get());\n    if (Cld::arm == 2) continue;  // Claude rig: no lightmap passes\n')
    edit(p, '  const bool envWanted = !ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 &&\n',
         '  const bool envWanted = Cld::arm != 3 && Cld::arm != 4 && !ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 &&\n')
    p = proj / 'src/scripts/district_mood.cpp'
    include(p)
    edit(p, '    const bool night = true;  // Claude night rig', '    const bool night = Cld::arm != 5;  // Claude rig: arm 5 = day')
    print('terrain6')

# ---- package route census inside partial bags ----
if '--pkgstat' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    t = p.read_text(encoding='utf-8-sig')
    cls = '(guardBandOnly ? 1 : packages[i].isInFrustum == IN_FRUSTUM ? 0 : packages[i].isInFrustum == PARTIALLY_IN_FRUSTUM ? 2 : 3)'
    a = '    auto doSubpkgs = doClip && !guardBandOnly &&\n'
    assert t.count(a) == 1, ('a', t.count(a))
    t = t.replace(a, '    ++Cld::pk[' + cls + '];\n' + a)
    b = '      if (telemetryEnabled) ++telemetry.packagesStrip;\n'
    assert t.count(b) == 1, ('b', t.count(b))
    # strip loop: count every package (cull branch and partial branch) at the top of the loop body
    c = '    const bool cull = (doClip && packages[i].isInFrustum == IN_FRUSTUM) ||\n                      !doClip || guardBandOnly;\n'
    assert t.count(c) == 1, ('c', t.count(c))
    t = t.replace(c, c + '    ++Cld::pk[4 + ' + cls + '];\n')
    a2 = '    TYRA_ATTRIB_INC(dsPartialBags);\n'
    assert t.count(a2) == 1, ('a2', t.count(a2))
    t = t.replace(a2, a2 + '    ++Cld::pk[bag->stripped ? 9 : 8];\n')
    p.write_bytes(t.encode())
    edit(eng / 'inc/debug/cld_probe.hpp', 'inline U coreCalls = 0;', 'inline U coreCalls = 0;\ninline U64 pk[10];')
    edit(eng / 'inc/debug/cld_probe.hpp', '  printf("CLDK slowN=%u fastN=%u\\n", slowN, fastN);',
         '  printf("CLDPK listBags10=%u stripBags10=%u list in/gb/clip/out x10 %u %u %u %u strip in/gb/clip/out x10 %u %u %u %u\\n", U(pk[8]*10/winN), U(pk[9]*10/winN), U(pk[0]*10/winN), U(pk[1]*10/winN), U(pk[2]*10/winN), U(pk[3]*10/winN), U(pk[4]*10/winN), U(pk[5]*10/winN), U(pk[6]*10/winN), U(pk[7]*10/winN));\n  for (auto& x : pk) x = 0;\n  printf("CLDK slowN=%u fastN=%u\\n", slowN, fastN);')
    print('pkg census')

if '--partscopes' in sys.argv:
    sys.path.insert(0, str(rig))
    import partscopes
    partscopes.apply(eng, edit, include)
    print('part scopes')

# ---- slice clipping A/B: arm 1 = old subpackage route ----
if '--sliceab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n',
         '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n  stapip.core.setSliceClip(Cld::arm != 1);\n')
    print('slice A/B')

# ---- sun-shadow night gate A/B: arm 1 forces the baked sun shadows on ----
if '--sunab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, 'daynight::g_sun[1] >= 0.0F)', 'daynight::g_sun[1] >= 0.0F || Cld::arm == 1)', count=2)
    print('sun A/B')

# ---- next-round ablation (6 arms): 1 day, 2 no probe, 3 no beams+lamp glow, 4 no pools, 5 no car glow ----
if '--next6' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  const bool envWanted = !ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 &&\n',
         '  const bool envWanted = Cld::arm != 2 && !ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 &&\n')
    edit(p, 'updateAndRenderLightBeams();', 'if (Cld::arm != 3) updateAndRenderLightBeams();')
    edit(p, 'renderVehicleLampGlow();', 'if (Cld::arm != 3) renderVehicleLampGlow();')
    edit(p, 'updateAndRenderLightPools();', 'if (Cld::arm != 4) updateAndRenderLightPools();')
    edit(p, 'renderVehicleGlow();', 'if (Cld::arm != 5) renderVehicleGlow();')
    p = proj / 'src/scripts/district_mood.cpp'
    include(p)
    edit(p, '    const bool night = true;  // Claude night rig', '    const bool night = Cld::arm != 1;  // Claude rig: arm 1 = day')
    print('next6')

# ---- terrain streaming scope (production order: before the probe) ----
if '--streamscope' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '    updateTerrainChunks(terrainFocusX, terrainFocusZ,\n',
         '    Cld::S cldTs("T.stream");\n    updateTerrainChunks(terrainFocusX, terrainFocusZ,\n')
    print('stream scope')

# ---- per-phase probe cost ----
if '--probephase' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '    costEnd("Reflections_shared_probe", -1, costSharedEnvStart);\n',
         '    Cld::scope(envFirst && envLast ? "P.whole" : envFirst ? "P.phase0" : "P.phase1", profTicks() - costSharedEnvStart);\n    costEnd("Reflections_shared_probe", -1, costSharedEnvStart);\n')
    print('probe phase')

# ---- day/night alternation: arm 1 = day ----
if '--dayab' in sys.argv:
    p = proj / 'src/scripts/district_mood.cpp'
    include(p)
    edit(p, '    const bool night = true;  // Claude night rig', '    const bool night = Cld::arm != 1;  // Claude rig: arm 1 = day')
    print('day A/B')

# ---- probe internals: sky / terrain / roads / objects ----
if '--probeparts' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '      renderWorldBag(skyDome.bag.get());\n      renderSkyBodies(probeEye, envLook);\n',
         '      { Cld::S cldPs("P.sky"); renderWorldBag(skyDome.bag.get());\n      renderSkyBodies(probeEye, envLook); }\n')
    edit(p, '    if (envFirst) renderTerrain();\n', '    { Cld::S cldPt("P.terrain"); if (envFirst) renderTerrain(); }\n')
    edit(p, '    if (envLast) renderRoadChunks();\n', '    { Cld::S cldPr("P.roads"); if (envLast) renderRoadChunks(); }\n')
    edit(p, '    core.renderer3D.popEnvView(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));\n    core.envMap.end();\n    envPhase',
         '    { Cld::S cldPe("P.popEnd"); core.renderer3D.popEnvView(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));\n    core.envMap.end(); }\n    envPhase')
    print('probe parts')

# ---- sky dome cost: arm 1 = no dome in the main view, arm 2 = no dome in the probe ----
if '--domeab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    t = p.read_text(encoding='utf-8-sig')
    a = '      renderWorldBag(skyDome.bag.get());\n      renderSkyBodies(probeEye, envLook);\n'
    assert t.count(a) == 1
    t = t.replace(a, '      if (Cld::arm != 2) renderWorldBag(skyDome.bag.get());\n      renderSkyBodies(probeEye, envLook);\n')
    b = '    renderWorldBag(skyDome.bag.get());\n    // Stars behind the discs'
    assert t.count(b) == 1, t.count(b)
    t = t.replace(b, '    if (Cld::arm != 1) renderWorldBag(skyDome.bag.get());\n    // Stars behind the discs')
    p.write_bytes(t.encode())
    print('dome A/B')

# ---- probe phase count A/B: arm 1 = 2 phases ----
if '--phaseab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  constexpr int kEnvPhases = 3;', '  const int kEnvPhases = Cld::arm == 1 ? 2 : 3;')
    print('phase A/B')

# ---- terrain stream budget A/B: arm 1 = one chunk per frame ----
if '--budgetab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '                        p2Focus ? players[1].z : 0.0F, p2Focus, 2);',
         '                        p2Focus ? players[1].z : 0.0F, p2Focus, Cld::arm == 1 ? 1 : 2);')
    print('budget A/B')

if '--series' in sys.argv:
    t = (eng / 'inc/debug/cld_probe.hpp').read_text(encoding='utf-8-sig')
    (eng / 'inc/debug/cld_probe.hpp').write_bytes(('#define CLD_SERIES 1\n' + t).encode())
    print('series')

# ---- bag economics: arm 1 = object bags not submitted (game loop still runs), arm 2 = no roads ----
if '--bagecon' in sys.argv:
    p = proj / 'src/gen/game_scene.gen.cpp'
    include(p)
    edit(p, 'void TerrainGame::renderObjectBag(int owner, Tyra::StaPipBag* bag) {\n',
         'void TerrainGame::renderObjectBag(int owner, Tyra::StaPipBag* bag) {\n  if (Cld::arm == 1) return;\n')
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    edit(p, 'void TerrainGame::renderRoadChunks() {\n', 'void TerrainGame::renderRoadChunks() {\n  if (Cld::arm == 2) return;\n')
    print('bag economics')

# ---- heavy-bag timing: per bag, strip (roads) vs list (static batches) ----
if '--heavytime' in sys.argv:
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    edit(p, '    renderWorldBag(bag);\n}\n', '    { const unsigned t0 = Cld::ticks(); renderWorldBag(bag); Cld::scope(bag->stripped ? "H.strip" : "H.list", Cld::ticks() - t0); }\n}\n')
    edit(p, '  for (size_t k = 0; k < want; ++k) renderWorldBag(heavyBags[heavyNext++]);',
         '  for (size_t k = 0; k < want; ++k) { Tyra::StaPipBag* hb = heavyBags[heavyNext++]; const unsigned t0 = Cld::ticks(); renderWorldBag(hb); Cld::scope(hb->stripped ? "H.strip" : "H.list", Cld::ticks() - t0); }')
    print('heavy time')

# ---- stamp economics (lean-safe: ticks only) ----
if '--stamptime' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '''    if (qbufferRenderer.tryStamp(
            bag, vp, rendererCore->renderer3D.frustumPlanes.getAll(),
            guardBand ? stampClipPlanes : nullptr,
            bag->info->dynamicLightReceive &&
                reception != StaPipLightReception::Suppress,
            &kNoSpotLight))
      return;''', '''    const unsigned cldS0 = Cld::ticks();
    const bool cldHit = qbufferRenderer.tryStamp(
            bag, vp, rendererCore->renderer3D.frustumPlanes.getAll(),
            guardBand ? stampClipPlanes : nullptr,
            bag->info->dynamicLightReceive &&
                reception != StaPipLightReception::Suppress,
            &kNoSpotLight);
    Cld::scope(cldHit ? "S.hit" : "S.miss", Cld::ticks() - cldS0);
    if (cldHit) return;''')
    print('stamp time')

if '--stampsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import stampsegs
    stampsegs.apply(eng, edit, include)
    print('stamp segments')

# ---- stamp batching A/B: arm 1 = old size-capped rule ----
if '--stampbatchab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n',
         '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n  stapip.core.setStampBatchAnySize(Cld::arm != 1);\n')
    print('stamp batch A/B')

# ---- probe budget deferral A/B: arm 1 = never defer ----
if '--deferab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  const bool envDefer = envSplit && envLastWorkMs > kEnvHeavyMs &&',
         '  const bool envDefer = Cld::arm != 1 && envSplit && envLastWorkMs > kEnvHeavyMs &&')
    print('defer A/B')

# ---- terrain chunk build cost (lean-safe) ----
if '--buildtime' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    t = p.read_text(encoding='utf-8-sig')
    for a in ['    buildTerrainChunk(slot, bestCx, bestCz);\n', '    buildTerrainChunk(slot, terrainChunks[slot].cx, terrainChunks[slot].cz);\n']:
        assert t.count(a) == 1, a
        t = t.replace(a, '    { const unsigned cb0 = Cld::ticks();\n' + a + '      Cld::scope("T.build", Cld::ticks() - cb0); }\n')
    p.write_bytes(t.encode())
    print('build time')

if '--track' in sys.argv:
    t = (eng / 'inc/debug/cld_probe.hpp').read_text(encoding='utf-8-sig')
    (eng / 'inc/debug/cld_probe.hpp').write_bytes(('#define CLD_TRACK 1\n' + t).encode())
    print('track')

# ---- interleave + stream budget: arm 1 = interleave off, arm 2 = interleave off + budget 1 ----
if '--ilab' in sys.argv:
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    edit(p, '  if (INTERLEAVE_PASSES == 0 || splitPassActive) {', '  if (INTERLEAVE_PASSES == 0 || splitPassActive || Cld::arm != 0) {')
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '                        p2Focus ? players[1].z : 0.0F, p2Focus, 2);',
         '                        p2Focus ? players[1].z : 0.0F, p2Focus, Cld::arm == 2 ? 1 : 2);')
    print('interleave A/B')

if '--firstdraw' in sys.argv:
    sys.path.insert(0, str(rig))
    import firstdraw
    firstdraw.apply(eng, edit, include)
    print('first draw')

if '--firstdraw2' in sys.argv:
    sys.path.insert(0, str(rig))
    import firstdraw2
    firstdraw2.apply(eng, edit, include)
    print('first draw 2')

# ---- terrain LOD off A/B: arm 1 = every chunk at stride 1 ----
if '--lodab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, 'int TerrainGame::terrainLodStep(int cx, int cz) const {\n',
         'int TerrainGame::terrainLodStep(int cx, int cz) const {\n  if (Cld::arm == 1) return 1;\n')
    print('lod A/B')

# ---- LOD off everywhere; arm 1 = interleave always, arm 2 = interleave always + day ----
if '--s8' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, 'int TerrainGame::terrainLodStep(int cx, int cz) const {\n',
         'int TerrainGame::terrainLodStep(int cx, int cz) const {\n  return 1;  // Claude rig: terrain LOD off\n')
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    edit(p, '  if (INTERLEAVE_PASSES == 2) {', '  if (INTERLEAVE_PASSES == 2 || Cld::arm != 0) {')
    p = proj / 'src/scripts/district_mood.cpp'
    include(p)
    edit(p, '    const bool night = true;  // Claude night rig', '    const bool night = Cld::arm != 2;  // Claude rig: arm 2 = day')
    print('s8')

# ---- whole-map resident terrain: arm 0 = as shipped (LOD 70), arm 1 = LOD off, arm 2 = LOD off + day ----
if '--s9' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, 'int TerrainGame::terrainLodStep(int cx, int cz) const {\n',
         'int TerrainGame::terrainLodStep(int cx, int cz) const {\n  if (Cld::arm != 0) return 1;  // Claude rig: terrain LOD off\n')
    p = proj / 'src/scripts/district_mood.cpp'
    include(p)
    edit(p, '    const bool night = true;  // Claude night rig', '    const bool night = Cld::arm != 2;  // Claude rig: arm 2 = day')
    print('s9')

# ---- resident + shade cache: arm 0 = as shipped (LOD 70), arm 1 = LOD off, arm 2 = LOD 70 by day ----
if '--s10' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, 'int TerrainGame::terrainLodStep(int cx, int cz) const {\n',
         'int TerrainGame::terrainLodStep(int cx, int cz) const {\n  if (Cld::arm == 1) return 1;  // Claude rig: terrain LOD off\n')
    p = proj / 'src/scripts/district_mood.cpp'
    include(p)
    edit(p, '    const bool night = true;  // Claude night rig', '    const bool night = Cld::arm != 2;  // Claude rig: arm 2 = day')
    print('s10')

# ---- route counters without the fine spans (lean-safe) ----
if '--routeslean' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '    if (frustumCheck == OUTSIDE_FRUSTUM) {\n', '    if (frustumCheck == OUTSIDE_FRUSTUM) {\n      ++Cld::routes[4];\n')
    edit(p, '  TYRA_ATTRIB_MARK(attribFlushStart);\n',
         '  ++Cld::routes[replayedWhole ? 0 : directRoute ? 1 : bag->stripped ? 3 : 2];\n  TYRA_ATTRIB_MARK(attribFlushStart);\n')
    edit(p, '            &kNoSpotLight))\n      return;\n',
         '            &kNoSpotLight)) {\n      ++Cld::routes[5];\n      return;\n    }\n')
    print('routes lean')

# ---- per-route time inside StaPipCore::render, tracked per frame (lean-safe) ----
if '--routetime' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '  Cld::S cldCore("core");', '  Cld::S cldCore("core");\n  const unsigned cldR0 = Cld::ticks();')
    edit(p, '            &kNoSpotLight))\n      return;\n',
         '            &kNoSpotLight)) {\n      Cld::scope("R.stamp", Cld::ticks() - cldR0);\n      return;\n    }\n')
    edit(p, '  TYRA_ATTRIB_MARK(attribFlushStart);\n',
         '  const char* cldRn = replayedWhole ? "R.repl" : directRoute ? "R.direct" : bag->stripped ? "R.strip" : "R.part";\n  TYRA_ATTRIB_MARK(attribFlushStart);\n')
    edit(p, '  TYRA_ATTRIB_ADD(dsFlushTicks, attribFlushStart);\n',
         '  TYRA_ATTRIB_ADD(dsFlushTicks, attribFlushStart);\n  Cld::scope(cldRn, Cld::ticks() - cldR0);\n')
    t = (eng / 'inc/debug/cld_probe.hpp').read_text(encoding='utf-8-sig')
    (eng / 'inc/debug/cld_probe.hpp').write_bytes(('#define CLD_TRK_NAMES "R.stamp", "R.part", "R.strip", "Objects", "Terrain", "R.repl"\n' + t).encode())
    print('route time')

if '--partsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import partsegs
    partsegs.apply(eng, edit, include)
    print('partial segments')

if '--headsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import headsegs
    headsegs.apply(eng, edit, include)
    print('head segments')

# ---- partial-bag batching A/B: arm 1 = off, arm 2 = on + day ----
if '--pbab' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n',
         '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n  stapip.core.setPartialBatch(Cld::arm != 1);\n')
    p = proj / 'src/scripts/district_mood.cpp'
    include(p)
    edit(p, '    const bool night = true;  // Claude night rig', '    const bool night = Cld::arm != 2;  // Claude rig: arm 2 = day')
    print('partial batch A/B')

if '--mutsplit' in sys.argv:
    sys.path.insert(0, str(rig))
    import mutsplit
    mutsplit.apply(eng, proj, edit, include)
    print('mutable split')

if '--borrowall' in sys.argv:
    sys.path.insert(0, str(rig))
    import borrowall
    borrowall.apply(eng, proj, edit, include)
    print('borrow-all upper bound')

# ---- immutable streams A/B: arm 1 = never register, arm 2 = on + day ----
if '--immutab' in sys.argv:
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '  if (bag->immutableStreams && !bag->immutableRegistered)\n',
         '  if (Cld::arm == 1 && bag->immutableRegistered) bag->releaseImmutableStreams();\n  if (Cld::arm != 1 && bag->immutableStreams && !bag->immutableRegistered)\n')
    p = proj / 'src/scripts/district_mood.cpp'
    include(p)
    edit(p, '    const bool night = true;  // Claude night rig', '    const bool night = Cld::arm != 2;  // Claude rig: arm 2 = day')
    print('immutable A/B')

if '--terrimm' in sys.argv:
    sys.path.insert(0, str(rig))
    import terrimm
    terrimm.apply(eng, proj, edit, include)
    print('terrain immutable A/B')

if '--mutwho' in sys.argv:
    sys.path.insert(0, str(rig))
    import mutwho
    mutwho.apply(eng, proj, edit, include)
    print('mutable owners')

# ---- immutable-span last-hit cache A/B: arm 1 = no cache ----
if '--hitab' in sys.argv:
    p = eng / 'inc/renderer/core/paths/path1/frame_chain_arena.hpp'
    edit(p, '    uint32_t i = lastHit;\n', '    uint32_t i = Cld::arm == 1 ? kCapacity : lastHit;\n')
    print('last-hit A/B')

if '--lightsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import lightsegs
    lightsegs.apply(eng, edit, include)
    print('light segments')

if '--subsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import subsegs
    subsegs.apply(eng, edit, include)
    print('submit segments')

if '--buildsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import buildsegs
    buildsegs.apply(proj, edit, include)
    print('build segments')

# ---- terrain build budget A/B: arm 1 = one build per frame ----
if '--budget1' in sys.argv:
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '                        p2Focus ? players[1].z : 0.0F, p2Focus, 2);',
         '                        p2Focus ? players[1].z : 0.0F, p2Focus, Cld::arm == 1 ? 1 : 2);')
    print('budget A/B')

if '--stripsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import stripsegs
    stripsegs.apply(eng, edit, include)
    print('strip segments')


if '--streamab' in sys.argv:
    sys.path.insert(0, str(rig))
    import streamab
    streamab.apply(proj, edit, include)
    print('stream clip A/B')

if '--shot' in sys.argv:
    sys.path.insert(0, str(rig))
    import shot
    shot.apply(proj, edit, rig)
    print('frame shots')

if '--deferab' in sys.argv:
    sys.path.insert(0, str(rig))
    import deferab
    deferab.apply(proj, edit, include)
    print('stamp deferral A/B')

if '--flushsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import flushsegs
    flushsegs.apply(eng, edit, include)
    print('flush segments')

if '--snapsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import snapsegs
    snapsegs.apply(eng, edit, include)
    print('snapshot segments')

if '--pkgcacheab' in sys.argv:
    sys.path.insert(0, str(rig))
    import pkgcacheab
    pkgcacheab.apply(eng, edit, include)
    print('package cache A/B')

sp = next((a.split('=',1)[1] for a in sys.argv if a.startswith('--spawn=')), None)
if sp:
    sys.path.insert(0, str(rig))
    import spawn
    spawn.apply(proj, edit, sp)
    print('spawn', sp)

if '--partheadab' in sys.argv:
    sys.path.insert(0, str(rig))
    import partheadab
    partheadab.apply(proj, edit, include)
    print('partial batching + head stamp A/B')

if '--headdiag' in sys.argv:
    sys.path.insert(0, str(rig))
    import headdiag
    headdiag.apply(eng, edit, include)
    print('head diag')


if '--os-engine' in sys.argv or '--os-game' in sys.argv:
    sys.path.insert(0, str(rig))
    import osparts
    osparts.apply(eng, proj, sys.argv)

if '--rpcwatch' in sys.argv:
    sys.path.insert(0, str(rig))
    import rpcwatchrig
    rpcwatchrig.apply(eng_root, proj, rig, edit)

if '--rpcscan' in sys.argv:
    sys.path.insert(0, str(rig))
    import rpcscanrig
    rpcscanrig.apply(proj, rig, edit)

if '--fsections' in sys.argv or any(a.startswith('--layout=') for a in sys.argv):
    sys.path.insert(0, str(rig))
    import layout
    layout.apply(eng, proj, sys.argv)

if '--updsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import updsegs
    updsegs.apply(proj, edit)

if '--vehsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import vehsegs
    vehsegs.apply(proj, edit)

if '--camcheck' in sys.argv:
    sys.path.insert(0, str(rig))
    import camcheck
    camcheck.apply(proj, edit)

if '--beamdiag' in sys.argv:
    sys.path.insert(0, str(rig))
    import beamdiag
    beamdiag.apply(proj, edit)

if '--vlsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import vlsegs
    vlsegs.apply(proj, edit)

if '--objcensus' in sys.argv:
    sys.path.insert(0, str(rig))
    import objcensus
    objcensus.apply(proj, edit)

if '--gputl' in sys.argv:
    sys.path.insert(0, str(rig))
    import gputl
    gputl.apply(eng, edit)

if '--vtxstat' in sys.argv:
    sys.path.insert(0, str(rig))
    import vtxstat
    vtxstat.apply(eng, edit)

if '--clipab' in sys.argv:
    sys.path.insert(0, str(rig))
    import clipab
    clipab.apply(proj, edit)

if '--daynightab' in sys.argv:
    sys.path.insert(0, str(rig))
    import dayab
    dayab.apply(proj, edit, include)

if '--vtxcat' in sys.argv:
    sys.path.insert(0, str(rig))
    import vtxcat
    vtxcat.apply(eng, proj, edit)

if '--vtxcat2' in sys.argv:
    sys.path.insert(0, str(rig))
    import vtxcat2
    vtxcat2.apply(eng, proj, edit)

if '--bagcensus' in sys.argv:
    sys.path.insert(0, str(rig))
    import bagcensus
    bagcensus.apply(proj, eng, edit)

if '--roadchunkab' in sys.argv:
    sys.path.insert(0, str(rig))
    import roadchunkab
    include(proj / 'src/gen/game_vehicles.gen.cpp')
    roadchunkab.apply(proj, edit)

if '--submitsegs' in sys.argv:
    sys.path.insert(0, str(rig))
    import submitsegs
    submitsegs.apply(eng, edit)

if '--singlepassab' in sys.argv:
    sys.path.insert(0, str(rig))
    import singlepassab
    singlepassab.apply(eng, edit)

if '--stamplight' in sys.argv:
    sys.path.insert(0, str(rig))
    import stampdiag
    stampdiag.apply(eng, edit)

if '--lightab' in sys.argv:
    sys.path.insert(0, str(rig))
    import lightab
    lightab.apply(proj, edit)
