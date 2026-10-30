"""Install the private normal-clock100 separated-prop/manifold A/B experiment."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import zipfile

from codegen_member import extract
from vehicle_contacts_fixture import replace


def install(repo, root):
    common = Path(subprocess.check_output([
        'git', '-C', str(repo), 'rev-parse', '--path-format=absolute',
        '--git-common-dir'], text=True).strip())
    allowed = (common.parent / '.tmp').resolve()
    assert root != allowed and root.is_relative_to(allowed)
    project = root / 'district'
    engine = root / 'engine/engine'
    metadata = root / 'metadata.json'
    meta = json.loads(metadata.read_text(encoding='utf8'))
    assert meta.get('vehicle_contacts_comparison') and not meta.get('road_boxes_comparison')
    physics = project / 'src/gen/game_physics.gen.cpp'
    vehicles = project / 'src/gen/game_vehicles.gen.cpp'
    control = project / 'src/gen/collision_baseline.gen.cpp'
    main = project / 'src/terrain_game.cpp'
    bench = engine / 'inc/debug/collision_bench.hpp'
    paths = [physics, vehicles, control, main, bench, metadata]
    with zipfile.ZipFile(root / 'before-road-boxes.zip', 'x', zipfile.ZIP_DEFLATED) as z:
        for p in paths:
            z.write(p, p.relative_to(root))
    # Both arms retain the same shared solver and micro backend. Only the
    # candidate replaces dynamic box corner manifolds with canonical patches.
    s = physics.read_text(encoding='utf8')
    old = extract(s, 'updateObjectPhysicsCandidate')
    new = replace(old, '      } else {\n        // hull against hull: each one\'s corners inside the other',
                  '      } else if (!(BoxPatch::active && A.box && Bs.box)) {\n        // hull against hull: each one\'s corners inside the other')
    new = replace(new, '    const int m = rbReduce(cs, n, red); CB::generated += m;', '''    RbCand patch[16];
    if (BoxPatch::active && b >= 0 && steps[a].box && steps[b].box) {
      ++BoxPatch::current.tested;
      const auto& A=steps[a]; const auto& B=steps[b];
      const auto& ba=physBodies[A.slot]; const auto& bb=physBodies[B.slot];
      RbV an[6],bn[6],ae[3],be[3];
      if(ba.planes==6 && bb.planes==6 && ba.edges==3 && bb.edges==3){
        for(int k=0;k<6;++k){an[k]=rbMat(A.R1,rbV(ba.lp[k*4],ba.lp[k*4+1],ba.lp[k*4+2]));bn[k]=rbMat(B.R1,rbV(bb.lp[k*4],bb.lp[k*4+1],bb.lp[k*4+2]));}
        for(int k=0;k<3;++k){ae[k]=rbMat(A.R1,rbV(ba.le[k*3],ba.le[k*3+1],ba.le[k*3+2]));be[k]=rbMat(B.R1,rbV(bb.le[k*3],bb.le[k*3+1],bb.le[k*3+2]));}
        const Tyra::ConvexView av={A.wv,A.nv,an,6,ae,3},bv={B.wv,B.nv,bn,6,be,3};
        Tyra::ConvexContact discrete;
        // A second full static SAT is intentional in this correctness arm.
        // It prevents treating a sweep-only hit as an end-pose face patch.
        if(Tyra::scalarMotionContactReference(av,bv,rbV(0,0,0),rbV(0,0,0),discrete)){
          BoxFaceManifold::Point points[16];
          const int count=BoxFaceManifold::clip(A.obb,B.obb,discrete,points);
          if(count){for(int k=0;k<count;++k)patch[k]={points[k].n,points[k].p,points[k].depth};cs=patch;n=count;++BoxPatch::current.patches;BoxPatch::current.points+=count;}
          else ++BoxPatch::current.fallback;
        }else ++BoxPatch::current.fallback;
      }else ++BoxPatch::current.fallback;
    }
    const int m = rbReduce(cs, n, red); CB::generated += m;''')
    s = s.replace(old, new, 1)
    s = '#include "debug/box_face_manifold.hpp"\n#include "debug/box_patch_probe.hpp"\n' + s
    physics.write_bytes(s.encode())
    s = vehicles.read_text(encoding='utf8')
    s = replace(s, 'VehicleContacts::enabled=!CB::baseline;VehicleContacts::begin();',
                'VehicleContacts::enabled=true;BoxPatch::begin(!CB::baseline,unsigned(CB::frame));VehicleContacts::begin();')
    s = replace(s, '  VehicleContacts::rows.push_back(VehicleContacts::current);',
                '  VehicleContacts::rows.push_back(VehicleContacts::current);BoxPatch::finish();')
    vehicles.write_bytes(('#include "debug/box_patch_probe.hpp"\n'+s).encode())
    s = main.read_text(encoding='utf8')
    a = s.index('  // Private controlled-workload benchmark, never emitted by production codegen.')
    b = s.index('  if(VehicleContacts::clockMultiplier==2)', a)
    s = s[:a] + '  // ROAD100: retain the production wall-clock timestep.\n' + s[b:]
    main.write_bytes(s.encode())
    s = bench.read_text(encoding='utf8')
    s = replace(s, 'inline void configure() {', '''inline void configure() {
  repeat=phase/2;solverArm=phase<2?phase:3-phase;baseline=solverArm==0;
  added=100;layout=0;return;''')
    bench.write_bytes(s.encode())
    s = control.read_text(encoding='utf8')
    s = replace(s, 'if(Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)CB::phase=12;else CB::phase=10;', 'CB::phase=0;')
    s = replace(s, '++CB::phase;if(!Tyra::vu0SatVerify && CB::phase==14)CB::phase=24;', 'BoxPatch::flush(CB::phase);++CB::phase;')
    s = replace(s, 'CB::phase>=((Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)?14:CB::phases)', 'CB::phase>=4')
    s = replace(s, 'CB::frame=CB::phase<2?-300:0;', 'CB::frame=0;')
    placement_start = s.index('   float x,z,y;if(CB::layout)')
    placement_end = s.index('\n   o.data.position[0]', placement_start)
    s = s[:placement_start] + '   const float x=-3.f+float(index%4)*2.f,z=-48.f+float(index/4)*4.f,y=.45f;' + s[placement_end:]
    s = replace(s, '  if(found!=128)', '''  // Validate actual resolved collider gaps, not just object-origin spacing.
  float minimumGap=1e30f;unsigned overlaps=0;
  char placementFile[80];snprintf(placementFile,sizeof(placementFile),"road-placement-%d.csv",CB::phase);
  FILE* placement=fopen(placementFile,"w");
  if(placement)fprintf(placement,"object,x,y,z,half_x,half_y,half_z\\n");
  for(int i=135;i<235;++i){const auto& d=runtimeObjects[i].data;const auto a=objectCollisionBox(runtimeObjects[i]);
    if(placement)fprintf(placement,"%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\\n",i,d.position[0]+a.center[0],d.position[1]+a.center[1],d.position[2]+a.center[2],a.half[0],a.half[1],a.half[2]);
    for(int j=i+1;j<235;++j){const auto& e=runtimeObjects[j].data;const auto b=objectCollisionBox(runtimeObjects[j]);
      const float dx=fabsf(d.position[0]+a.center[0]-e.position[0]-b.center[0])-a.half[0]-b.half[0];
      const float dz=fabsf(d.position[2]+a.center[2]-e.position[2]-b.center[2])-a.half[2]-b.half[2];
      const float gap=std::max(dx,dz);minimumGap=std::min(minimumGap,gap);if(!(gap>0))++overlaps;
    }
  }
  if(placement)fclose(placement);
  printf("ROAD100 phase=%d placed=100 initial_prop_overlaps=%u minimum_horizontal_gap=%g normal_clock=1\\n",CB::phase,overlaps,minimumGap);fflush(stdout);
  if(overlaps || !placement){CB::finished=true;printf("CB ERROR road placement\\n");return;}
  if(found!=128)''')
    control.write_bytes(('#include "debug/box_patch_probe.hpp"\n'+s).encode())
    for name in ['box_face_manifold.hpp', 'box_patch_probe.hpp']:
        shutil.copyfile(repo/'tools/ps2rig'/name, engine/'inc/debug'/name)
    meta.update(road_boxes_comparison=True, active_mode='road100-manifold-admission',
                candidate='Canonical discrete SAT face patches; common shared vehicle/prop solver',
                phases=4, expected_phases=[0,1,2,3], normal_clock=True,
                normal_clock_fps_qualified=False, production_integration_qualified=False,
                vehicle_contact_isolation=False, vehicle_contact_clock_multiplier=1,
                shadow_enabled=True, initial_placed_props=100,
                road_layout={'columns':4,'rows':25,'x0':-3,'z0':-48,'dx':2,'dz':4})
    meta['road_boxes_source_sha256'] = {
        str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in paths[:-1] + [engine/'inc/debug/box_face_manifold.hpp', engine/'inc/debug/box_patch_probe.hpp']}
    metadata.write_text(json.dumps(meta, indent=2)+'\n', encoding='utf8')
    (project/'bin/vehicle-contacts.cfg').write_bytes(b'0 1\n')
    (project/'bin/sat-verify.cfg').write_bytes(b'verify\n')
    print('Installed4-phase normal-clock road100 correctness comparison; build/admission pending')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scratch', required=True, type=Path)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    install(args.repo.resolve(), args.scratch.resolve())
