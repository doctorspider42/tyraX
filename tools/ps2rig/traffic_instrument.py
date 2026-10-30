"""Instrument a fully baked traffic-scaling fixture; use native builds afterwards."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import zipfile

from vehicle_contacts_fixture import replace


def install(repo, project):
    common = Path(subprocess.check_output([
        'git', '-C', str(repo), 'rev-parse', '--path-format=absolute',
        '--git-common-dir'], text=True).strip())
    allowed = (common.parent/'.tmp').resolve()
    assert project != allowed and project.is_relative_to(allowed)
    meta_path = project.parent/'traffic-scaling-metadata.json'
    meta = json.loads(meta_path.read_text(encoding='utf8'))
    assert meta['hq_total'] == 5 and meta['civilian_total'] == 8
    assert not meta.get('instrumented')
    # The optional clipping-setter cache diagnostic must not replace custom
    # resident microprogram overrides. This fixture has none.
    for name in ['vu_programs.gen.hpp', 'vu_scripts.gen.hpp']:
        assert 'constexpr bool ENABLED = false;' in (
            project/'inc/scripts'/name).read_text(encoding='utf8'), name
    source = project/'src/gen'
    main = project/'src/terrain_game.cpp'
    header = project/'inc/terrain_game.hpp'
    vehicles = source/'game_vehicles.gen.cpp'
    physics = source/'game_physics.gen.cpp'
    scene = source/'game_scene.gen.cpp'
    collision = source/'game_collision.gen.cpp'
    lighting = source/'game_lighting.gen.cpp'
    paths = [main, header, vehicles, physics, scene, collision, lighting]
    with zipfile.ZipFile(project.parent/'before-traffic-instrument.zip',
                         'x', zipfile.ZIP_DEFLATED) as z:
        for p in paths:
            z.write(p, p.relative_to(project))
    engine = project.parent/'traffic-engine'
    assert not engine.exists()
    shutil.copytree(repo/'vendor/tyra', engine,
                    ignore=shutil.ignore_patterns('.git', 'obj', 'bin'))
    debug = engine/'engine/inc/debug'
    shutil.copyfile(repo/'tools/ps2rig/traffic_bench.hpp', debug/'traffic_bench.hpp')
    # Periodic diagnostic output is not part of the player's frame. Error and
    # assertion macros remain intact; all benchmark summaries are explicit.
    p = debug/'debug.hpp'
    s = p.read_text(encoding='utf8')
    s = replace(s, '#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n")',
                '#define TYRA_LOG(...) ((void)0) // private quiet traffic measurement')
    p.write_bytes(s.encode())
    p = engine/'engine/src/renderer/core/paths/path1/vif1_queue.cpp'
    s = p.read_text(encoding='utf8')
    s = replace(s, 'if (++frames % 120 == 0) {',
                'if (false && ++frames % 120 == 0) { // private quiet timing')
    p.write_bytes(s.encode())
    p = engine/'engine/src/pad/pad.cpp'
    s = p.read_text(encoding='utf8')
    for signature, body in [
        ('void Pad::init() {', '\n  actuators=0;port=slot=0;buttons={};reset();resetJoys();return;\n'),
        ('void Pad::update() {', '\n  return; // private deterministic controls\n'),
        ('void Pad::setActuators(const bool& smallMotor, const u8& bigPower) {', '\n  return; // no benchmark rumble RPC\n')]:
        s = replace(s, signature, signature+body)
    p.write_bytes(s.encode())
    s = header.read_text(encoding='utf8')
    s = replace(s, '  void buildScene();', '''  void buildScene();
  bool benchTrafficBegin();
  void benchTrafficSetup();
  void benchTrafficEnd();''')
    header.write_bytes(s.encode())
    s = main.read_text(encoding='utf8')
    s = '#include "debug/traffic_bench.hpp"\n'+s
    s = replace(s, '  const bool menuActive =', '  if(!benchTrafficBegin())return;\n  const bool menuActive =')
    s = replace(s, '  engine->renderer.endFrame();', '''  {TrafficBench::Scope scope(TrafficBench::End);engine->renderer.endFrame();}
  benchTrafficEnd();''')
    main.write_bytes(s.encode())
    s = vehicles.read_text(encoding='utf8')
    s = '#include "debug/traffic_bench.hpp"\n'+s
    for signature, counter in [('void TerrainGame::stepVehicles(float dt) {', 'Vehicle'),
                                ('void TerrainGame::trafficFrame(float dt) {', 'Core'),
                                ('void TerrainGame::trafficDrive(int vi, float& throttle, float& brake, float& steer) {', 'Core'),
                                ('void TerrainGame::renderVehicleWheels() {', 'Wheels'),
                                ('void TerrainGame::renderVehicleGlass() {', 'Glass')]:
        s = replace(s, signature, signature+f'\n  TrafficBench::Scope trafficScope(TrafficBench::{counter});')
    s = replace(s, 'void TerrainGame::updateVehicles(float dt) {',
                'void TerrainGame::updateVehicles(float dt) {\n  ++TrafficBench::current.c[TrafficBench::Steps];')
    s = replace(s, '  int target = (int)(TRAFFIC_DENSITY * tfLaneNear_ / 100.0F + 0.5F);',
                '  int target = TrafficBench::ready?TrafficBench::wantedCiv:0;')
    s = replace(s, '  if (dx * dx + dz * dz < nearD * nearD) return false;',
                '  if (dx * dx + dz * dz < nearD * nearD) return false;\n  ++TrafficBench::current.c[TrafficBench::Kinematic];')
    s = replace(s, '    const float spd0 = v.speed;',
                '    unsigned trafficLapTime=TrafficBench::detailed?TrafficBench::ticks():0;\n    const float spd0 = v.speed;')
    # Keep ordinary AI steering/throttle in measured time. Warm cars with
    # brakes through the SAME complete model; no pose freezing or sleeping.
    s = replace(s, '    if (v.lightsOn < 0) v.lightsOn = s.headlights ? 1 : 0;', '''    if(TrafficBench::ready){
      if(TrafficBench::warm()){
        inThrottle=0;inBrake=1;inSteer=0;inHand=inNos=0;
        // Holding the warmup brake must not arm the ordinary AI's escape
        // manoeuvre. All measured frames use its unmodified state machine.
        v.aiStuckT=v.aiRevT=0;v.aiUnstick=0;v.wpCur=0;
      }
      else if(vi==vehicleDriver_){inThrottle=TrafficBench::driving()?.65f:0;inBrake=TrafficBench::driving()?0:1;inSteer=0;inHand=inNos=0;}
    }
    if (v.lightsOn < 0) v.lightsOn = s.headlights ? 1 : 0;''')
    for k in range(7):
        before = ''
        if k == 0:
            before = '    ++TrafficBench::current.c[TrafficBench::FullModels];\n'
        if k == 3:
            before = '''    TrafficBench::high(TrafficBench::WallMax,unsigned(wallBoxN));
    TrafficBench::high(TrafficBench::FloorMax,unsigned(floorBoxN));
    TrafficBench::high(TrafficBench::MeshMax,unsigned(nearMeshN));
    TrafficBench::high(TrafficBench::PropMax,unsigned(pushN));
'''
        s = replace(s, f'    VEH_LAP({k});', before+f'    TrafficBench::lap({k},trafficLapTime);\n    VEH_LAP({k});')
    from codegen_member import extract
    old = extract(s, 'vehiclePaintGate')
    new = replace(old, '    return true;', '    TrafficBench::paint(i,driven);\n    return true;')
    s = s.replace(old, new, 1)
    # Methods live after vehBodyRotation and before the ordinary step wrapper.
    s = replace(s, 'void TerrainGame::stepVehicles(float dt) {',
                (repo/'tools/ps2rig/traffic_bench_members.inc').read_text(encoding='utf8')+
                '\nvoid TerrainGame::stepVehicles(float dt) {')
    vehicles.write_bytes(s.encode())
    s = physics.read_text(encoding='utf8')
    s = '#include "debug/traffic_bench.hpp"\n'+s
    s = replace(s, 'void TerrainGame::updateObjectPhysics() {', '''void TerrainGame::updateObjectPhysics() {
  TrafficBench::Scope trafficScope(TrafficBench::Physics);
  ++TrafficBench::current.c[TrafficBench::PhysicsSteps];''')
    s = replace(s, 'void TerrainGame::renderScene() {',
                'void TerrainGame::renderScene() {\n  TrafficBench::Scope trafficScope(TrafficBench::Scene);')
    physics.write_bytes(s.encode())
    s = scene.read_text(encoding='utf8')
    s = '#include "debug/traffic_bench.hpp"\n'+s
    s = replace(s, 'void TerrainGame::renderObjectBag(int owner, Tyra::StaPipBag* bag) {',
                'void TerrainGame::renderObjectBag(int owner, Tyra::StaPipBag* bag) {\n  TrafficBench::ObjectScope allocationOwner(owner);\n  if(bag && bag->count)TrafficBench::submitted(owner);')
    scene.write_bytes(s.encode())
    s = '#include "debug/traffic_bench.hpp"\n'+collision.read_text(encoding='utf8')
    s = replace(s, 'void TerrainGame::rebuildObjectGeometry(int index, bool localSpace) {',
                'void TerrainGame::rebuildObjectGeometry(int index, bool localSpace) {\n  TrafficBench::ObjectScope allocationOwner(index,true);')
    collision.write_bytes(s.encode())
    s = '#include "debug/traffic_bench.hpp"\n'+lighting.read_text(encoding='utf8')
    s = replace(s, 'void TerrainGame::updateAndRenderBlobShadows() {',
                'void TerrainGame::updateAndRenderBlobShadows() {\n  TrafficBench::Scope trafficScope(TrafficBench::BlobShadows);')
    lighting.write_bytes(s.encode())
    # Configuration is read once, outside the first measured phase.
    p = vehicles
    s = p.read_text(encoding='utf8')
    s = replace(s, '  if(finished){fflush(stdout);SleepThread();return false;}', r'''  if(finished){fflush(stdout);SleepThread();return false;}
  static bool configured=false;if(!configured){configured=true;
    std::set_new_handler([](){
      printf("TRAFFICBENCH ERROR allocation phase=%d frame=%d stage=%d object=%d\n",phase,frame,allocationStage,allocationOwner);
      fflush(stdout);finished=true;for(;;)SleepThread();
    });
    FILE* f=fopen("traffic-bench-detail.cfg","r");detailed=f!=nullptr;if(f)fclose(f);
    f=fopen("traffic-bench-phase.cfg","r");int first=0;
    if(f){if(fscanf(f,"%d",&first)==1 && first>=0 && first<10)phase=firstPhase=first;fclose(f);}
    f=fopen("traffic-bench-reload-cache.cfg","r");dropReloadCache=f!=nullptr;if(f)fclose(f);
    printf("TRAFFICBENCH CONFIG detail=%d normal_clock=1 start_phase=%d drop_cache=%d\n",int(detailed),phase,int(dropReloadCache));fflush(stdout);
  }''')
    p.write_bytes(s.encode())
    meta.update(instrumented=True, detailed=False,
                source_sha256={str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
                input_manifest_sha256=hashlib.sha256((project/'vehicle-playground.tyra').read_bytes()).hexdigest(),
                engine_source_sha256={str(p.relative_to(engine)): hashlib.sha256(p.read_bytes()).hexdigest() for p in [debug/'traffic_bench.hpp',debug/'debug.hpp',engine/'engine/src/pad/pad.cpp',engine/'engine/src/renderer/core/paths/path1/vif1_queue.cpp']},
                status='Instrumented; native build and physical admission pending.',
                normal_clock_fps_qualified=False)
    meta_path.write_text(json.dumps(meta, indent=2)+'\n', encoding='utf8')
    print('Installed normal-clock ten-phase traffic probe; native build only from here')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project', required=True, type=Path)
    p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    a = p.parse_args()
    install(a.repo.resolve(), a.project.resolve())
