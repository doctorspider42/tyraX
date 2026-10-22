"""Build-input preparation for a private no-vehicle camera rotation census."""
import importlib.util
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('quiet', ROOT/'tools/tyrax2-quiet-fixture.py')
quiet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quiet)
out = Path(sys.argv[1]).resolve()
reflection_trial = '--reflection-isolation' in sys.argv[2:]
source_project = ROOT/'examples/vehicle-playground'
if '--source-project' in sys.argv:
    source_project = Path(sys.argv[sys.argv.index('--source-project')+1]).resolve()
lifetime_trial = '--lifetime' in sys.argv[2:]
freeze_trial = '--freeze-capture' in sys.argv[2:] or reflection_trial
breakdown_trial = '--packet-breakdown' in sys.argv[2:]
fix_trial = '--submit-fix' in sys.argv[2:]
sweep_trial = '--geometry-sweep' in sys.argv[2:]
prepared_trial = '--prepared-static' in sys.argv[2:]
production_defaults = '--production-defaults' in sys.argv[2:] or reflection_trial
assert not production_defaults or sweep_trial or reflection_trial, 'Unsupported default-route validation'
geometry_trial = '--geometry-reuse' in sys.argv[2:] or sweep_trial or prepared_trial
partial_trial = '--partial-baked' in sys.argv[2:] or geometry_trial
partial_verify = '--verify-partial' in sys.argv[2:]
followup_trial = '--clip-followup' in sys.argv[2:]
expansion_verify = '--verify-expansion' in sys.argv[2:]
assert not expansion_verify or followup_trial or geometry_trial
assert not partial_verify or partial_trial
scope_trial = '--packet-scopes' in sys.argv[2:] or breakdown_trial
assert not (scope_trial and freeze_trial), 'Separate interventions'
assert not (fix_trial and (scope_trial or freeze_trial or lifetime_trial)), 'Separate interventions'
assert not (partial_trial and (fix_trial or scope_trial or freeze_trial or lifetime_trial)), 'Separate interventions'
assert not (followup_trial and (partial_trial or partial_verify or fix_trial or scope_trial or freeze_trial or lifetime_trial))
drive_trial = not sweep_trial and ('--drive' in sys.argv[2:] or freeze_trial or scope_trial or fix_trial or partial_trial or followup_trial)
assert not out.exists(), 'Refuse to overwrite evidence'
game = out/'game'
engine = out/'tyra/engine'
def ignore_project(path,names):
    skipped=quiet.ignore(path,names)
    if Path(path)==source_project: skipped+=['bin']
    return skipped
shutil.copytree(source_project, game, ignore=ignore_project)
shutil.copytree(ROOT/'vendor/tyra', out/'tyra', ignore=quiet.ignore)
if production_defaults:
    config=(engine/'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp').read_text()
    for flag in ('TYRA_STAPIP_PARTIAL_BAKED','TYRA_STAPIP_EXPANSION_CACHE'):
        assert '#define '+flag+' 1' in config, flag+' is not enabled by default'
else:
    # Preserve historical control arms after the production defaults change.
    config_path=engine/'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp'
    config=config_path.read_text()
    for flag in ('TYRA_STAPIP_PARTIAL_BAKED','TYRA_STAPIP_EXPANSION_CACHE'):
        config,n=re.subn(r'(#define '+flag+r') [01]\b',r'\g<1> 0',config)
        assert n==1,flag
    config_path.write_bytes(config.encode())
if prepared_trial:
    config_path=engine/'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp'
    config=config_path.read_text()
    for flag in ('TYRA_STAPIP_PARTIAL_BAKED','TYRA_STAPIP_EXPANSION_CACHE','TYRA_STAPIP_PREPARED_STATIC'):
        config,n=re.subn(r'(#define '+flag+r') [01]\b',r'\g<1> 1',config)
        assert n==1,flag
    config_path.write_bytes(config.encode())
if fix_trial:
    patch=ROOT/'docs/tyrax2-full-throttle-2026-10-07/submit-fix/engine-candidate.patch'
    # Rejected candidates belong only in this private fixture, never production.
    subprocess.run(['git','apply','--check','-p3',str(patch)],cwd=out/'tyra',check=True)
    subprocess.run(['git','apply','-p3',str(patch)],cwd=out/'tyra',check=True)
manifest = game/'vehicle-playground.tyra'
model = json.loads(manifest.read_text(encoding='utf-8-sig'))
if reflection_trial:
    assert not any(json.loads((game/'objects'/(oid+'.json')).read_text(encoding='utf-8-sig')).get('type')=='road'
                   for oid in model['scenes'][0]['objects']), 'Expected a road-free main scene'
    (out/'authored-manifest.json').write_bytes(manifest.read_bytes())
    reference=json.loads((ROOT/'examples/vehicle-playground/vehicle-playground.tyra').read_text(encoding='utf-8-sig'))
    shine={v['id']:v.get('bodyShine',0) for v in reference['vehicles']}
    for v in model['vehicles']:
        assert shine.get(v['id'],0)>0, v['id']
        v['bodyShine']=shine[v['id']]
        v['bodyReflMap']=''
removed = []
for scene in model['scenes']:
    kept = []
    for oid in scene['objects']:
        p = game/'objects'/(oid+'.json')
        obj = json.loads(p.read_text(encoding='utf-8-sig'))
        if obj.get('type') == 'vehicle' and not drive_trial:
            removed.append(oid)
        else:
            kept.append(oid)
    scene['objects'] = kept
assert removed or drive_trial, 'No vehicle objects found; inspect project schema'
for key in ('showFps','showMemory','showProfiler','showAreas','showCollision','liveLink','liveDebug','liveLogic','timeMachine','remotePad','inputRecorder','keyboardMouse'):
    model['settings'][key] = False
for value in model['saveValues']:
    if value['name']=='district-night': value['default']=0
manifest.write_text(json.dumps(model,indent=2)+'\n',encoding='utf8')
result = subprocess.run([str(ROOT/'build/tyrax-editor.exe'),'--refresh-gen',str(game)],capture_output=True,text=True)
(out/'generation.log').write_text(result.stdout+result.stderr,encoding='utf8')
assert result.returncode==0, 'Generation failed'

def edit(path, old, new, count=1):
    text=path.read_text(encoding='utf-8-sig')
    assert text.count(old)==count,(str(path),old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())

def include(path):
    text=path.read_text(encoding='utf-8-sig')
    path.write_bytes(('#include "debug/motion_probe.hpp"\n'+text).encode())

shutil.copy2(ROOT/('tools/tyrax2-drive-probe.hpp' if drive_trial else 'tools/tyrax2-motion-probe.hpp'),engine/'inc/debug/motion_probe.hpp')
if sweep_trial:
    p=engine/'inc/debug/motion_probe.hpp'
    p.write_bytes(b'#define MOTION_GEOMETRY_SWEEP 1\n'+p.read_bytes())
if freeze_trial:
    p=engine/'inc/debug/motion_probe.hpp'
    p.write_bytes(b'#define MOTION_FREEZE_TRIAL 1\n'+p.read_bytes())
if scope_trial:
    p=engine/'inc/debug/motion_probe.hpp'
    p.write_bytes(b'#define MOTION_SCOPE_TRIAL 1\n'+p.read_bytes())
if fix_trial:
    p=engine/'inc/debug/motion_probe.hpp'
    p.write_bytes(b'#define MOTION_FIX_TRIAL 1\n'+p.read_bytes())
if partial_trial:
    p=engine/'inc/debug/motion_probe.hpp'
    p.write_bytes(b'#define MOTION_PARTIAL_TRIAL 1\n'+p.read_bytes())
if followup_trial:
    p=engine/'inc/debug/motion_probe.hpp'
    p.write_bytes(b'#define MOTION_FOLLOWUP_TRIAL 1\n'+p.read_bytes())
if lifetime_trial and not drive_trial:
    p=engine/'inc/debug/motion_probe.hpp'
    p.write_bytes(b'#define MOTION_LIFETIME_TRIAL 1\n'+p.read_bytes())
quiet.switch(engine/'inc/debug/frame_profile.hpp','TYRA_FRAME_PROFILE')
quiet.switch(engine/'inc/debug/hardware_trace.hpp','TYRA_HARDWARE_TRACE')
edit(engine/'inc/debug/debug.hpp','#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n")','#define TYRA_LOG(...) ((void)0) // Private motion diagnostic')
edit(engine/'src/renderer/core/paths/path1/vif1_queue.cpp','if (++frames % 120 == 0) {','if ((++frames % 120 == 0) && false) {')
p=engine/'src/engine.cpp'; include(p)
edit(p,'void Engine::realLoop() {','void Engine::realLoop() {\n  MotionProbe::begin(renderer.core.getStallTotal());')
edit(p,'  HardwareTrace::endFrame();','  HardwareTrace::endFrame();\n  MotionProbe::end(renderer.core.getStallTotal());')
p=engine/'src/renderer/core/renderer_core.cpp'
t=quiet.present_hooks(p.read_text(encoding='utf-8-sig')).replace('debug/quiet_cadence.hpp','debug/motion_probe.hpp').replace('QuietCadence::onPresent','MotionProbe::onPresent')
for name,num in [('Pipeline',1),('Synchronous',0),('Synthetic',2)]:t=t.replace('QuietCadence::PresentKind::'+name,str(num))
p.write_bytes(t.encode())
p=game/'src/terrain_game.cpp'; include(p)
edit(p,'void TerrainGame::init() {','void TerrainGame::init() {\n  MotionProbe::init();')
camera='''  // Private repeatable angular sweep at the ordinary spawn eye.
  static Vec4 probeEye, probeLook;
  if(MotionProbe::frame==200) { probeEye=cameraPosition; probeLook=cameraLookAt; probeLook.y=probeEye.y; scriptCtx.hudSuppressed=true; }
  if(MotionProbe::frame>=200) {
    const float a=float(MotionProbe::angle())*0.01745329252F;
    const float dx=probeLook.x-probeEye.x, dz=probeLook.z-probeEye.z;
    cameraPosition=probeEye;
    cameraLookAt.set(probeEye.x+dx*cosf(a)-dz*sinf(a),probeLook.y,probeEye.z+dx*sinf(a)+dz*cosf(a));
  }
  if(MotionProbe::offset==250 && MotionProbe::phase<6) {
    printf("LOG: MOTIONCONTEXT phase=%u vehicles=0 scene=%d eye=%f,%f,%f look=%f,%f,%f envUsers=%d hour=%f paused=%u\\n",MotionProbe::phase,currentScene,cameraPosition.x,cameraPosition.y,cameraPosition.z,cameraLookAt.x,cameraLookAt.y,cameraLookAt.z,g_dynamicEnvUsers,daynight::g_hour,unsigned(daynight::g_paused));fflush(stdout);
  }
  MotionProbe::renderBegin();
'''
needle='  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));'
if drive_trial:
    camera='''  if(vehicleDriver_>=0 && vehicleDriver_<vehicleCount_) {
    const auto& v=vehicles_[vehicleDriver_];
    MotionProbe::pose({v.pos[0],v.pos[1],v.pos[2],v.speed,v.yaw,g_frameDt,v.damage,cameraPosition.x,cameraPosition.y,cameraPosition.z,cameraLookAt.x,cameraLookAt.y,cameraLookAt.z,vehicleDriver_,vehicleCount_});
  }
  MotionProbe::renderBegin();
'''
    edit(p,'  inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);','''  inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);
  { Tyra::PadButtons neutral{}; engine->pad.setState(neutral,neutral,128,128,128,128); }
''')
    edit(p,'  if (scriptCtx.requestScene >= 0) {','''  if(MotionProbe::resetRequested) { scriptCtx.requestScene=0; MotionProbe::resetRequested=false; }
  if (scriptCtx.requestScene >= 0) {''')
    veh=game/'src/gen/game_vehicles.gen.cpp';include(veh)
    edit(veh,'    } else if (v.wpCount > 0 && (s.damageMechanical <= 0.5F || v.damage < 0.999F)) {','''      // Private full-pedal input; ordinary simulation and chase camera remain active.
      inThrottle=MotionProbe::gas()?1.0F:0.0F; inBrake=0.0F;
      inSteer=0.0F; inHand=MotionProbe::gas()?0:1; inNos=0;
    } else if (v.wpCount > 0 && (s.damageMechanical <= 0.5F || v.damage < 0.999F)) {''')
edit(p,needle,camera+needle)
if prepared_trial:
    edit(p,'  inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);','''  if (MotionProbe::offset == 0) stapip.core.releasePreparedGeometry();
  if (MotionProbe::offset == 1000 && MotionProbe::phase < 6) {
    unsigned tried = 0, ready = 0;
    const unsigned start = MotionProbe::ticks();
    if (MotionProbe::variant()) for (auto& batch : staticBatches) {
      if (batch.dirty) rebuildStaticBatch(batch);
      if (!batch.bag || !batch.bag->count) continue;
      ++tried;
      if (stapip.core.prepareStaticGeometry(batch.bag.get())) ++ready;
    }
    printf("PREPAREDSTATIC phase=%u tried=%u ready=%u ticks=%u\\n",
        MotionProbe::phase, tried, ready, MotionProbe::ticks() - start);
    fflush(stdout);
  }
  inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);''')
physics=game/'src/gen/game_physics.gen.cpp'; include(physics)
if sweep_trial:
    edit(game/'src/terrain_game.cpp','  if (scriptCtx.requestScene >= 0) {','''  // Correctness-only lifecycle continuation after the six camera phases.
  if (MotionProbe::frame >= 7200 && MotionProbe::frame < 9000 &&
      MotionProbe::frame % 600 == 0)
    scriptCtx.requestScene = int(((MotionProbe::frame - 7200) / 600 + 1) % 3);
  if (MotionProbe::frame >= 7500 && MotionProbe::frame < 9000 &&
      MotionProbe::frame % 600 == 300) {
    const int expected = int(((MotionProbe::frame - 7500) / 600 + 1) % 3);
    TYRA_ASSERT(currentScene == expected, "Geometry lifecycle scene did not load");
    printf("GEOMETRYSCENE frame=%u scene=%d\\n", MotionProbe::frame, currentScene);
    fflush(stdout);
  }
  if (MotionProbe::frame == 9000) {
    printf("GEOMETRYSCENEDONE\\n"); fflush(stdout);
  }
  if (scriptCtx.requestScene >= 0) {''')
if freeze_trial:
    edit(physics,'''  if (!ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 && skyDome.bag &&
      (refreshEnvMap || !sharedEnvBasisValid) && !envReuseOk &&
      !splitPassActive) {''','''  const bool probeEligible = !ENV_PROBE_REFLECTED && g_dynamicEnvUsers > 0 && skyDome.bag &&
      (refreshEnvMap || !sharedEnvBasisValid) && !envReuseOk && !splitPassActive;
  const bool freezeProbe = MotionProbe::freezeCapture() && sharedEnvBasisValid;
  MotionProbe::captureFrozen = freezeProbe;
  if (probeEligible && freezeProbe) MotionProbe::add(MotionProbe::CaptureBlocked);
  if (probeEligible && !freezeProbe) {''')
edit(physics,'    const u32 costSharedEnvStart = costStart();','    MotionProbe::add(MotionProbe::ProbeCapture);\n    const u32 costSharedEnvStart = costStart();')
edit(physics,'      if (paintChanged) {','      if (paintChanged) {\n        MotionProbe::add(MotionProbe::Paint);')
for p in (game/'src').rglob('*.cpp'):
    t=p.read_text(encoding='utf-8-sig')
    if 'void TerrainGame::rebuildObjectGeometry(' in t:
        first,last=quiet.body_span(t,'TerrainGame::rebuildObjectGeometry')
        t=t[:first+1]+'\n  MotionProbe::add(MotionProbe::Rebuild);'+t[first+1:]
        p.write_bytes(('#include "debug/motion_probe.hpp"\n'+t).encode())
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'; include(core)
t=core.read_text();first,last=quiet.body_span(t,'StaPipCore::render');t=t[:first+1]+'\n  MotionProbe::add(MotionProbe::Bags);'+t[first+1:];core.write_bytes(t.encode())
edit(core,'      recordOutsideBag(bag);','      MotionProbe::add(MotionProbe::Outside);\n      recordOutsideBag(bag);')
edit(core,'    if (directRoute) { TYRA_ATTRIB_INC(dsDirectBags); }','    if (directRoute) { MotionProbe::add(MotionProbe::Direct); TYRA_ATTRIB_INC(dsDirectBags); }')
edit(core,'    replayedWhole = directRoute && bakeBag && qbufferRenderer.replayWholeBakedBag();','    replayedWhole = directRoute && bakeBag && qbufferRenderer.replayWholeBakedBag();\n    if(replayedWhole) MotionProbe::add(MotionProbe::Replay);')
if partial_trial and not (production_defaults or prepared_trial):
    edit(core,'if (directRoute || TYRA_STAPIP_PARTIAL_BAKED) {','if (directRoute || MotionProbe::variant()) {')
if (followup_trial or geometry_trial) and not (production_defaults or prepared_trial):
    expansion_sites=core.read_text().count('TYRA_STAPIP_EXPANSION_CACHE &&')
    assert expansion_sites in (1,2), expansion_sites
    edit(core,'TYRA_STAPIP_EXPANSION_CACHE &&','(MotionProbe::variant() & 1) &&',expansion_sites)
if followup_trial:
    adapter=engine/'inc/renderer/core/paths/path1/frame_submission.hpp';include(adapter)
    edit(adapter,'    if (channel == DMA_CHANNEL_GIF && Vif1Queue::recordGif(packet)) return;',
         '    if (channel == DMA_CHANNEL_GIF) { if (Vif1Queue::recordGif(packet)) return; MotionProbe::add(MotionProbe::EnvFallback); }')
    queue=engine/'src/renderer/core/paths/path1/vif1_queue.cpp';include(queue)
    edit(queue,'  if (nativeBatches - frameBatchStart > 1) ++frameSplitCount;',
         '  if (nativeBatches - frameBatchStart > 1) { ++frameSplitCount; MotionProbe::add(MotionProbe::EnvFallback); }')
edit(core,'    TYRA_ATTRIB_INC(dsPartialBags);','    MotionProbe::add(MotionProbe::Partial);\n    TYRA_ATTRIB_INC(dsPartialBags);')
q=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp';include(q)
edit(q,'  size = triCount * 3;','  size = triCount * 3;\n  MotionProbe::add(MotionProbe::ExpandVerts,size);')
for needle,bag in [('  allocateDynamicData(size, pkg1.bag);','pkg1.bag'),('  allocateDynamicData(size, pkg.bag);','pkg.bag'),('  allocateDynamicData(static_cast<u16>(size), pkg.bag);','pkg.bag')]:
    edit(q,needle,needle+'\n  MotionProbe::add(MotionProbe::CopyBytes,size*16*(1+unsigned('+bag+'->texture!=nullptr)+unsigned('+bag+'->color->many!=nullptr)+unsigned('+bag+'->lighting!=nullptr)));',2 if 'pkg1' in bag else 1)
p=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp';include(p)
if followup_trial or geometry_trial:
    edit(p,'    for (u32 t = 0; t < pkg.size - 2u; t += triangles) {','    MotionProbe::add(MotionProbe::ExpandBuilds);\n    for (u32 t = 0; t < pkg.size - 2u; t += triangles) {')
    edit(p,'  buffer->fillByPointer(cached);','  MotionProbe::add(MotionProbe::ExpandCachedVerts,count);\n  buffer->fillByPointer(cached);')
    if expansion_verify:p.write_bytes(b'#define TYRA_STAPIP_EXPANSION_VERIFY 1\n'+p.read_bytes())
if followup_trial:
    env=engine/'src/renderer/core/envmap/renderer_core_envmap.cpp';include(env)
    edit(env,'  if (!allocated) return;', '  if (!allocated) return;',2)
    edit(env,'TYRA_ENVMAP_COALESCE_BARRIER && Vif1Queue::recordingFrame()', '(MotionProbe::variant() & 2) && Vif1Queue::recordingFrame()',2)
    edit(env,'  packet2_reset(', '''  MotionProbe::add(Vif1Queue::recordingFrame() ? MotionProbe::EnvRecorded : MotionProbe::EnvFallback);
  if (path1->isVU1Configured() && (MotionProbe::variant() & 2) && Vif1Queue::recordingFrame()) MotionProbe::add(MotionProbe::BarrierOmitted);
  packet2_reset(''',2)
if partial_trial:
    edit(p,'  lastProgramName = program->getName();\n  baked.countHit();',
         '  lastProgramName = program->getName();\n  MotionProbe::add(MotionProbe::PartialReplay);\n  MotionProbe::add(MotionProbe::PartialBytes,(size-4)*16);\n  baked.countHit();')
    edit(p,'  const prim_t savedPrim = *prim;', '  MotionProbe::add(MotionProbe::PartialBuild);\n  const prim_t savedPrim = *prim;')
    if partial_verify:p.write_bytes(b'#define TYRA_STAPIP_PARTIAL_BAKED_VERIFY 1\n'+p.read_bytes())
edit(p,'          ++churnSkips;','          MotionProbe::add(MotionProbe::Churn);\n          ++churnSkips;')
edit(p,'      countMiss(reason);','''      MotionProbe::add(MotionProbe::BakeMiss);
      if (item.bboxVersion != key.bboxVersion) MotionProbe::add(MotionProbe::MissBBox);
      else if (item.contentVersion != key.contentVersion) MotionProbe::add(MotionProbe::MissContent);
      else if (item.primKey != key.primKey || item.singleColor != key.singleColor || item.stripped != key.stripped) MotionProbe::add(MotionProbe::MissPrim);
      else if (item.sts != key.sts || item.colors != key.colors || item.normals != key.normals) MotionProbe::add(MotionProbe::MissStreams);
      else if (item.program != key.program || item.programAddr != key.programAddr) MotionProbe::add(MotionProbe::MissProgram);
      else if (item.depthScaleBits != key.depthScaleBits) MotionProbe::add(MotionProbe::MissDepth);
      else MotionProbe::add(MotionProbe::MissSize);
      countMiss(reason);''')
edit(p,'  countMiss(MissNewEntry);','  MotionProbe::add(MotionProbe::BakeMiss);\n  MotionProbe::add(MotionProbe::MissNew);\n  countMiss(MissNewEntry);')
edit(p,'void StaPipBakedStreams::onFrameEnd() {','void StaPipBakedStreams::onFrameEnd() {\n  MotionProbe::occupancy(usedQwords, storage.size());')
edit(p,'      if (item->framesLeftToDestroy <= 0) retire(*item);','      if (item->framesLeftToDestroy <= 0) { MotionProbe::add(MotionProbe::Expired); retire(*item); }')
edit(p,'    retire(*storage[victim]);','    MotionProbe::add(MotionProbe::Evicted);\n    retire(*storage[victim]);')
edit(p,'bool StaPipBakedStreams::reserve(u32 qwords) {\n  if (qwords > kMaxQwords) return false;\n  if (usedQwords + qwords > kMaxQwords && !evictFor(qwords)) return false;\n  usedQwords += qwords;', '''bool StaPipBakedStreams::reserve(u32 qwords) {
  if (qwords > kMaxQwords) { MotionProbe::add(MotionProbe::ReserveFailed); return false; }
  if (usedQwords + qwords > kMaxQwords && !evictFor(qwords)) { MotionProbe::add(MotionProbe::ReserveFailed); return false; }
  usedQwords += qwords;
  MotionProbe::occupancy(usedQwords, storage.size());''')
if lifetime_trial or drive_trial:
    t=p.read_text()
    start=t.index('bool StaPipBakedStreams::evictFor(')
    end=t.index('#endif  // TYRA_STAPIP_BAKED_STREAM',start)
    part=t[start:end]
    assert part.count('kLifetimeFrames')==3
    t=t[:start]+part.replace('kLifetimeFrames','int(MotionProbe::lifetime())')+t[end:]
    p.write_bytes(t.encode())
p=engine/'inc/renderer/core/paths/path1/frame_chain_arena.hpp';include(p)
edit(p,'if (!copyQwords(target + payload,ref,size)) return {};','if (!copyQwords(target + payload,ref,size)) { return {}; }\n                MotionProbe::add(MotionProbe::SnapshotBytes,size*16);')
edit(p,'if (!copyQwords(target + payload, ref, size)) return {};','if (!copyQwords(target + payload, ref, size)) { return {}; }\n            MotionProbe::add(MotionProbe::SnapshotBytes,size*16);')
p=engine/'src/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.cpp';include(p)
# Count materialized package records, including nested subpackages.
t=p.read_text()
import re
t,n=re.subn(r'(  \*(o_size|o_count) = [^;]+;)',r'\1\n  MotionProbe::add(MotionProbe::Packages,*\2);',t)
assert n==2,n
p.write_bytes(t.encode())
if scope_trial:
    # Narrow scopes, ordinary call order and original wait bodies preserved.
    edit(core,'  MotionProbe::add(MotionProbe::Bags);',
         '  MotionProbe::Scope probeCore;\n  MotionProbe::add(MotionProbe::Bags);')
    edit(core,'  TYRA_ATTRIB_MARK(attribReplayStart);',
         '  MotionProbe::Scope probePartial(true, checkYesFrustumPartialClipYes || checkYesFrustumPartialClipNo);\n  TYRA_ATTRIB_MARK(attribReplayStart);')
    wait_sites=[]
    for rel,needles in [
        ('src/renderer/core/paths/path1/vif1_queue.cpp',[
            ('dma_channel_wait(DMA_CHANNEL_VIF1, 0);',3),
            ('while (tail - head >= kDepth) advanceIfIdle();',1),
            ('while (tail - head >= kRingDepth) advanceIfIdle();',1),
            ('while (static_cast<s32>(completed - sequence) < 0) advanceIfIdle();',1),
            ('while (running) advanceIfIdle();',1),
            ('while (*stat & 0x1f000003) {}',1)]),
        ('inc/renderer/core/paths/path1/frame_submission.hpp',[
            ('dma_channel_wait(channel, timeout);',1),
            ('dma_channel_wait(channel, 0);',2),
            ('draw_wait_finish();',1),
            ('while (*stat & 0x1f000003) {}',1)]),
        ('src/renderer/core/renderer_core_sync.cpp',[
            ('while (!check()) {\n  }',1)]),
        ('src/renderer/core/renderer_core.cpp',[
            ('while (*vif1Stat & kVif1Busy) {\n    }',1),
            ('graph_wait_vsync();',3)]),
        ('src/renderer/core/gs/renderer_core_gs.cpp',[
            ('while (pendingBuffer >= 0) graph_wait_vsync();',2)])]:
        path=engine/rel
        if '#include "debug/motion_probe.hpp"' not in path.read_text():include(path)
        for needle,count in needles:
            edit(path,needle,'{ MotionProbe::Wait probeWait; '+needle+' }',count)
            wait_sites.append(dict(file=rel,source=needle,count=count))
    (out/'wait-sites.json').write_text(json.dumps(wait_sites,indent=2)+'\n')
if breakdown_trial:
    for rel,needle,category,count in [
        ('src/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.cpp','  HardwareTrace::Scope trace("Package_create");','Create',2),
        ('src/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.cpp','  HardwareTrace::Scope trace("Package_classify");','Classify',1),
        ('src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp','  HardwareTrace::Scope trace("QBuffer_copy");','Fill',4),
        ('src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp','void StaPipQBuffer::fillByPointer(const StaPipBagPackage& pkg) {','Fill',1),
        ('src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp','  HardwareTrace::Scope trace("Packet_build");','Packet',1),
        ('src/renderer/core/paths/path1/vif1_queue.cpp','u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {','Submit',1),
        ('inc/renderer/core/paths/path1/frame_chain_arena.hpp','  static bool copyQwords(void* destination, const void* source, uint32_t count) {','Payload',1)]:
        edit(engine/rel,needle,needle+'\n  MotionProbe::Bucket probeBucket(MotionProbe::'+category+'Ticks);',count)
if fix_trial:
    queue=engine/'src/renderer/core/paths/path1/vif1_queue.cpp';include(queue)
    edit(queue,'if (frameRecording && nativeWriter && sourceCopied && !nativeInFlight) {',
         'if ((MotionProbe::variant() & 1) && frameRecording && nativeWriter && sourceCopied && !nativeInFlight) {')
    edit(queue,'      if (fused.chain) {','      if (fused.chain) {\n        MotionProbe::add(MotionProbe::FusedSubmit);')
    arena=engine/'inc/renderer/core/paths/path1/frame_chain_arena.hpp'
    edit(arena,'  bool borrow(uint32_t address, uint32_t qwords, uint32_t bankMask) {', '''  bool borrow(uint32_t address, uint32_t qwords, uint32_t bankMask) {
    if (!(MotionProbe::variant() & 2)) {
      if (!contains(address, qwords) || !bankMask || (bankMask & ~3u)) return false;
      const uint32_t at = lowerBound(address);
      Span& span = at < count && spans[at].base == address ? spans[at] : spans[at-1];
      span.readers |= bankMask;
      return true;
    }''')
(out/'source-manifest.json').write_text(json.dumps({'removedVehicles':removed,'files':quiet.source_inputs(out,manifest.name)},indent=2)+'\n')
print(out)
