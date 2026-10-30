"""Put vehicle/prop pairs through the existing one-pass rigid-body solver."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile
from codegen_member import extract


def replace(s,old,new):
    assert s.count(old)==1,old[:100]
    return s.replace(old,new,1)


def install(repo,root,refresh=False):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    project=root/'district';rig=repo/'tools/ps2rig';metadata=root/'metadata.json'
    paths=[project/'src/terrain_game.cpp',project/'inc/terrain_game.hpp',project/'src/gen/game_physics.gen.cpp',project/'src/gen/game_vehicles.gen.cpp',project/'src/gen/collision_baseline.gen.cpp',metadata]
    current=json.loads(metadata.read_text(encoding='utf8'));assert current.get('unified_timeline_comparison') or current.get('vehicle_contacts_comparison')
    backup=root/'before-vehicle-contacts.zip'
    if not refresh:
        with zipfile.ZipFile(backup,'x',zipfile.ZIP_DEFLATED) as z:
            for path in paths:z.write(path,path.relative_to(root))
    else:assert backup.is_file()
    # Start from the qualified guard source, not the rejected extra-substep
    # prototype. The shared frame-displacement convention remains unchanged.
    with zipfile.ZipFile(root/'before-unified-timeline.zip') as z:
        original={path:z.read(path.relative_to(root).as_posix()).decode('utf8') for path in paths}
    meta=json.loads(original[metadata]);assert meta['zero_friction_comparison']
    assert meta['mesh_collision_objects']==0,'This fixture qualifies existing box colliders only; mesh chassis need a dedicated path'
    header=paths[1];s=original[header]
    s=replace(s,'  void benchTick();','''  void benchTick();
  void stepVehicleBodyContacts(float dt);
  void captureVehicleContactStart();
  void captureVehicleContactPrediction(float dt);
  void prepareVehicleContactShape(int object);
  void finishVehicleContact(int object,Tyra::CollisionV3 velocity,Tyra::CollisionV3 angular,Tyra::CollisionV3 vp,Tyra::CollisionV3 wp);''')
    # CollisionV3 is the shared RbV alias, declared before this class.
    s=replace(s,'#include <tyra>','#include <tyra>\n#include "physics/convex_collision.hpp"')
    header.write_bytes(s.encode())
    for path in [paths[0],paths[2],paths[3],paths[4]]:
        s=replace(original[path],'#include "game_runtime.gen.hpp"','#include "game_runtime.gen.hpp"\n#include "debug/vehicle_contacts_probe.hpp"')
        if path.name=='game_physics.gen.cpp':
            b=extract(s,'updateObjectPhysicsCandidate');n=b
            n=replace(n,'const volatile bool skipZeroFriction=CB::ready&&!CB::baseline;','const volatile bool skipZeroFriction=CB::ready;')
            n=replace(n,'  unsigned predictionStart=CB::ticks();','  ++VehicleContacts::current.fullSteps;\n  unsigned predictionStart=CB::ticks();')
            n=replace(n,'    if (!o.active || !o.data.physics) continue;','    if (!o.active || !o.data.physics || VehicleContacts::isCar(i)) continue;')
            n=replace(n,'  if (bodies.empty()) return;','  if(VehicleContacts::enabled)for(const auto& car:VehicleContacts::cars)bodies.push_back(car.object);\n  if (bodies.empty()) return;')
            n=replace(n,'  for (int i : bodies) physBodyFor(i);','  for (int i : bodies){physBodyFor(i);prepareVehicleContactShape(i);}')
            n=replace(n,'if(!posePool.begin(poseCounts,PHYS_MAX_HULL_VERTS)){updateObjectPhysicsWide();return;}',
                'if(!posePool.begin(poseCounts,PHYS_MAX_HULL_VERTS)){if(VehicleContacts::enabled){printf("CB ERROR vehicle pose allocation\\n");CB::finished=true;return;}updateObjectPhysicsWide();return;}')
            a=n.index('    for (int k = 0; k < 4; ++k) s.q0[k] = b.q[k];');end=n.index('    s.nv = b.verts;',a)
            old=n[a:end]
            custom='''    const int vehicleIndex=VehicleContacts::index(i);
    if(vehicleIndex>=0){const auto& car=VehicleContacts::cars[size_t(vehicleIndex)];
      for(int k=0;k<9;++k){s.R0[k]=car.oldR[k];s.R1[k]=car.R[k];s.invIw[k]=0;}
      s.invIw[4]=car.invYaw;rbMatToQuat(s.R0,s.q0);
      s.x0=car.oldCentre;s.x1=car.centre;s.v=car.velocity;s.w=car.angular;
      s.vp=s.wp=rbV(0,0,0);
    }else{
'''+old+'    }\n'
            n=n[:a]+custom+n[end:]
            check='''    if(vehicleIndex>=0 && Tyra::vu0SatVerify){
      const CollisionBox original=objectCollisionBox(o);
      for(int corner=0;corner<8;++corner){
        const V3 local{original.center[0]+((corner&1)?original.half[0]:-original.half[0]),
          original.center[1]+((corner&2)?original.half[1]:-original.half[1]),
          original.center[2]+((corner&4)?original.half[2]:-original.half[2])};
        const V3 rotatedCorner=boxRotate(local,o.data);
        const RbV expected=rbV(o.data.position[0]+rotatedCorner.x,o.data.position[1]+rotatedCorner.y,o.data.position[2]+rotatedCorner.z);
        for(int axis=0;axis<3;++axis){const float value=VehicleContacts::component(expected,axis);
          const float budget=64.f*1.1920929e-7f*(fabsf(value)+b.radius+1.f);
          const float ratio=fabsf(VehicleContacts::component(s.wv[corner],axis)-value)/budget;
          VehicleContacts::current.maxShapeRatio=std::max(VehicleContacts::current.maxShapeRatio,ratio);
          if(!std::isfinite(ratio) || ratio>1.f)++VehicleContacts::current.errors;
        }
        ++VehicleContacts::current.shapeChecks;
      }
    }
'''
            n=replace(n,'    steps.push_back(s);',check+'    steps.push_back(s);')
            n=replace(n,'s.box = o.data.type == 0 && b.verts == 8;','s.box = (o.data.type == 0 || VehicleContacts::isCar(i)) && b.verts == 8;')
            n=replace(n,'    const int si=nextWorld++;','    const int si=nextWorld++;\n    if(VehicleContacts::isCar(steps[si].obj))return;')
            n=replace(n,'    for (int j : bpNearPhys) {','    for (int j : bpNearPhys) {\n      if(VehicleContacts::isCar(j))continue;')
            n=replace(n,'      if (j == i) continue;','      if (j == i || (VehicleContacts::isCar(i) && VehicleContacts::isCar(j))) continue;')
            n=replace(n,'      const RbV d = rbSub(A.x1, Bs.x1);','      if(VehicleContacts::isCar(i) || VehicleContacts::isCar(j))++VehicleContacts::current.pairs;\n      const RbV d = rbSub(A.x1, Bs.x1);')
            n=replace(n,'      c.other=b>=0?steps[b].obj:worldObject;','''      c.other=b>=0?steps[b].obj:worldObject;
      if(b>=0 && (VehicleContacts::isCar(steps[a].obj) || VehicleContacts::isCar(steps[b].obj))){
        ++VehicleContacts::current.contacts;VehicleContacts::current.maxDepth=std::max(VehicleContacts::current.maxDepth,red[k].depth);
      }''')
            n=replace(n,'  contacts.swap(worldContacts);','  contacts.swap(worldContacts);\n'+(rig/'vehicle_contacts_reference.inc').read_text(encoding='utf8'))
            n=replace(n,'    A.v = rbAdd(A.v, rbMul(P, A.invMass));','    const RbV beforeA=A.v,beforeB=c.b>=0?steps[c.b].v:rbV(0,0,0);\n    A.v = rbAdd(A.v, rbMul(P, A.invMass));')
            n=replace(n,'      Bs.w = rbSub(Bs.w, rbMat(Bs.invIw, rbCross(c.rb, P)));','''      Bs.w = rbSub(Bs.w, rbMat(Bs.invIw, rbCross(c.rb, P)));
      if(VehicleContacts::isCar(A.obj) || VehicleContacts::isCar(Bs.obj))
        VehicleContacts::impulse(beforeA,beforeB,A.v,Bs.v,A.invMass,Bs.invMass,P);''')
            marker='  // --- 5. integrate and write back';a=n.index(marker)
            tail=n[a:];tail=replace(tail,'    RbStep& S = steps[si];','''    RbStep& S = steps[si];
    if(VehicleContacts::isCar(S.obj)){
      const unsigned start=CB::ticks();finishVehicleContact(S.obj,S.v,S.w,S.vp,S.wp);
      VehicleContacts::current.finishTicks+=CB::ticks()-start;continue;
    }''');n=n[:a]+tail
            s=s.replace(b,n,1)
        if path.name=='game_vehicles.gen.cpp':
            s=replace(s,'void TerrainGame::updateVehicles(float dt) {','void TerrainGame::updateVehicles(float dt) {\n  ++VehicleContacts::current.vehicleSteps;')
            s=replace(s,'inThrottle=CB::gas()?1.0F:0.0F;','inThrottle=CB::gas()&&!VehicleContacts::isolation?1.0F:0.0F;')
            # Keep queries/response for walls and cars. Dynamic props are
            # solved exclusively by the shared pair grid and impulse loops.
            s=replace(s,'    CB::pushCandidates += pushN;','    if(!VehicleContacts::enabled)CB::pushCandidates += pushN;')
            a=s.index('    // PHYSICS BODIES:');b=s.index('    VEH_LAP(4);',a)
            body=replace(s[a:b],'    if (pushN > 0) {','    if (!VehicleContacts::enabled && pushN > 0) {');s=s[:a]+body+s[b:]
            # Methods share vehBodyRotation, which is defined earlier here.
            pos=s.index('void TerrainGame::stepVehicles(')
            s=s[:pos]+(rig/'vehicle_contacts_members.inc').read_text(encoding='utf8')+'\n'+s[pos:]
        if path.name=='collision_baseline.gen.cpp':
            s=replace(s,'static bool checked=false;if(!checked){checked=true;','static bool checked=false;if(!checked){checked=true;VehicleContacts::configure();')
            s=replace(s,'ZeroFrictionProbe::flush(CB::phase);','ZeroFrictionProbe::flush(CB::phase);VehicleContacts::flush(CB::phase);')
            s=replace(s,'  if(found!=128)', '''  if(VehicleContacts::isolation)for(int vi=0;vi<vehicleCount_;++vi){auto& v=vehicles_[vi];
    v.pos[0]=140.f+10.f*vi;v.pos[2]=140.f+10.f*vi;
    v.pos[1]=terrainHeightAt(v.pos[0],v.pos[2])+VEHICLE_DEFS[v.def].rideHeight*v.scale;
    v.speed=v.lateral=v.velY=v.spin=0;v.wpCount=0;
    if(v.object>=0){auto& o=runtimeObjects[v.object];for(int k=0;k<3;++k)o.data.position[k]=v.pos[k];o.dirty=true;}
  }
  if(found!=128)''')
        if path.name=='terrain_game.cpp':
            s=replace(s,'  const bool menuActive =','  if(VehicleContacts::clockMultiplier==2){g_frameDt*=2.f;g_frameScale*=2.f;}\n  const bool menuActive =')
            a=s.index('#if TYRA_FRAME_PROFILE\n',s.index('// Broad-phase refresh boundary'));b=s.index('  else muteVehicleEngines();',a)
            s=s[:a]+'''  if (!menuActive) {
    stepVehicleBodyContacts(g_frameDt);
    updateVehicleSmoke(g_frameScale*(1.f/50.f));updateVehicleSkids(g_frameScale*(1.f/50.f));updateVehicleDebris(g_frameScale*(1.f/50.f));
  }
'''+s[b:]
        path.write_bytes(s.encode())
    helper=root/'engine/engine/inc/debug/vehicle_contacts_probe.hpp';shutil.copyfile(rig/helper.name,helper)
    meta.update(vehicle_contacts_comparison=True,zero_friction_comparison=False,shared_scene_comparison=True,
        candidate='Single full physics pass with vehicle/prop contacts and bidirectional impulses in the existing solver',
        shadow_enabled=True,world_grid_verify=True,world_overlap_verify=True,expected_phases=[12,13],
        active_mode='vehicle-contacts-admission',normal_clock_fps_qualified=False,production_integration_qualified=False)
    meta.update(vehicle_contact_isolation=False,vehicle_contact_clock_multiplier=1)
    meta['vehicle_contacts_source_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths[:-1]+[helper]}
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8');(project/'bin/sat-verify.cfg').write_bytes(b'verify\n')
    (project/'bin/vehicle-contacts.cfg').write_bytes(b'0 1\n')
    print('Installed single-pass shared vehicle/prop solver; original world/car responses retained')


def configure(root,mode,double=False):
    p=root/'metadata.json';meta=json.loads(p.read_text(encoding='utf8'));assert meta.get('vehicle_contacts_comparison')
    isolation=mode.startswith('isolation');verify=mode.endswith('admission');multiplier=2 if double else 1
    meta.update(vehicle_contact_isolation=isolation,vehicle_contact_clock_multiplier=multiplier,
        shadow_enabled=verify,world_grid_verify=verify,world_overlap_verify=verify,
        expected_phases=[12,13] if verify else [10,11,12,13,24,25,26,27],
        active_mode='vehicle-contacts-'+mode+('-double' if double else ''),
        elf_sha256=hashlib.sha256((root/'district/bin/vehicle-playground.elf').read_bytes()).hexdigest())
    p.write_text(json.dumps(meta,indent=2),encoding='utf8')
    (root/'district/bin/vehicle-contacts.cfg').write_bytes(f'{int(isolation)} {multiplier}\n'.encode('ascii'))
    cfg=root/'district/bin/sat-verify.cfg'
    if verify:cfg.write_bytes(b'verify\n')
    elif cfg.exists():cfg.unlink()
    print(meta['active_mode'],meta['elf_sha256'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path);p.add_argument('--refresh',action='store_true');p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2])
    p.add_argument('--mode',choices=['isolation-admission','isolation-timing','impact-admission','impact-timing']);p.add_argument('--double',action='store_true');a=p.parse_args()
    if a.mode:
        assert not a.refresh;configure(a.scratch.resolve(),a.mode,a.double)
    else:
        assert not a.double;install(a.repo.resolve(),a.scratch.resolve(),a.refresh)
