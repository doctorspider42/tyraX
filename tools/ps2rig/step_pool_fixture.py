"""Same-ELF wide versus pooled pose storage; all collision arithmetic retained."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile


def install(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    metadata=root/'metadata.json';meta=json.loads(metadata.read_text(encoding='utf8'));assert meta.get('micro_cost_profile') and not meta.get('step_pool_comparison')
    project=root/'district';physics=project/'src/gen/game_physics.gen.cpp';baseline=project/'src/gen/collision_baseline.gen.cpp'
    runtime=project/'inc/game_runtime.gen.hpp';terrain=project/'inc/terrain_game.hpp'
    with zipfile.ZipFile(root/'before-step-pool.zip','x',zipfile.ZIP_DEFLATED) as z:
        for f in [physics,baseline,runtime,terrain,metadata]:z.write(f,f.relative_to(root))
    with zipfile.ZipFile(root/'before-micro-cost.zip') as z:
        s=z.read('district/src/gen/game_physics.gen.cpp').decode()
        base=z.read('district/src/gen/collision_baseline.gen.cpp').decode()
    # Restore the uninstrumented world-first schedule in both layouts.
    old='  const bool overlapWorld=!CB::baseline;';assert s.count(old)==1
    s=s.replace(old,'  const bool overlapWorld=false;',1)
    assert 'MicroCost::' not in s and 'MicroCost::' not in base
    begin=s.index('void TerrainGame::updateObjectPhysicsCandidate() {')
    end=s.index('void TerrainGame::pushPhysicsBodies(',begin)
    original=s[begin:end];wide=original.replace('updateObjectPhysicsCandidate()','updateObjectPhysicsWide()',1)
    compact=original.replace('void TerrainGame::updateObjectPhysicsCandidate() {',
        '''void TerrainGame::updateObjectPhysicsCandidate() {
  if(CB::baseline){updateObjectPhysicsWide();return;}
  using RbStep=RbPoolStep;''',1)
    old='  for (int i : bodies) physBodyFor(i);';assert compact.count(old)==1
    compact=compact.replace(old,old+'''
  static Tyra::StepVertexPool<RbV> posePool;
  static std::vector<int> poseCounts;
  poseCounts.assign(size_t(count),0);
  for(int i:bodies)poseCounts[size_t(i)]=physBodies[physSlotOf[i]].verts;
  if(!posePool.begin(poseCounts,PHYS_MAX_HULL_VERTS)){updateObjectPhysicsWide();return;}
''',1)
    old='    RbStep s;';assert compact.count(old)==1
    compact=compact.replace(old,old+'''
    s.wv0=posePool.previous(size_t(i));s.wv=posePool.predicted(size_t(i));''',1)
    s=s[:begin]+wide+compact+s[end:]
    s='#include "debug/step_vertex_pool.hpp"\n'+s
    header=runtime.read_text(encoding='utf8');start=header.index('struct RbStep {');stop=header.index('\n};',start)+3
    record=header[start:stop].replace('struct RbStep {','struct RbPoolStep {',1)
    for old,new in [('RbV wv0[PHYS_MAX_HULL_VERTS];','RbV* wv0;'),('RbV wv[PHYS_MAX_HULL_VERTS];','RbV* wv;')]:
        assert record.count(old)==1;record=record.replace(old,new,1)
    record+='\nstatic_assert(sizeof(RbStep)==880,"wide pose ABI changed");\nstatic_assert(sizeof(void*)!=4 || sizeof(RbPoolStep)==312,"pooled EE pose ABI changed");\n'
    header=header[:stop]+'\n'+record+header[stop:]
    declarations=terrain.read_text(encoding='utf8');old='void updateObjectPhysicsCandidate();';assert declarations.count(old)==1
    declarations=declarations.replace(old,old+' void updateObjectPhysicsWide();',1)
    # Both layouts retain canonical micro validation in the initial gate.
    physics.write_bytes(s.encode());baseline.write_bytes(base.encode());runtime.write_bytes(header.encode());terrain.write_bytes(declarations.encode())
    shutil.copyfile(repo/'tools/ps2rig/step_vertex_pool.hpp',root/'engine/engine/inc/debug/step_vertex_pool.hpp')
    (project/'bin/sat-verify.cfg').write_bytes(b'verify\n')
    if 'micro_cost_source_sha256' in meta:meta['lineage_micro_cost_source_sha256']=meta.pop('micro_cost_source_sha256')
    meta.update(step_pool_comparison=True,micro_cost_profile=False,shadow_enabled=True,world_grid_verify=True,
        world_overlap_verify=True,same_elf_admission_qualified=False,expected_phases=[12,13],active_mode='step-pool-verify',
        candidate='Private312-byte EE steps with exact shared pose spans versus original880-byte steps',
        data_layout='Shared PhysBody;880-byte wide EE step versus312-byte pointer step plus exact per-body pose spans',
        comparison='Both layouts use the same world-first chained micro backend,8m body grid,cached world and6+3 solver. Every original vertex retained; invalid counts fall back to the wide function. No arithmetic or contact-cap change.')
    meta['step_pool_source_sha256']={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [physics,baseline,runtime,terrain,root/'engine/engine/inc/debug/step_vertex_pool.hpp']}
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8')


def diagnose(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    metadata=root/'metadata.json';meta=json.loads(metadata.read_text(encoding='utf8'));assert meta.get('step_pool_comparison')
    p=root/'district/src/gen/game_physics.gen.cpp';s=p.read_text(encoding='utf8');assert 'StepPoolProbe::' not in s
    before='#include "debug/world_overlap_probe.hpp"';assert s.count(before)==1
    s=s.replace(before,before+'\n#include "debug/step_pool_probe.hpp"',1)
    before='  WorldOverlapProbe::capture(contacts);';assert s.count(before)==2
    s=s.replace(before,before+'\n  StepPoolProbe::capture(steps,contacts,physBodies);');p.write_bytes(s.encode())
    b=root/'district/src/gen/collision_baseline.gen.cpp';s=b.read_text(encoding='utf8');assert s.count('#include "debug/world_overlap_probe.hpp"')==1
    s=s.replace('#include "debug/world_overlap_probe.hpp"','#include "debug/world_overlap_probe.hpp"\n#include "debug/step_pool_probe.hpp"',1)
    before='WorldOverlapProbe::flush(CB::phase);';assert s.count(before)==1
    s=s.replace(before,before+'StepPoolProbe::flush(CB::phase);',1)
    old='void TerrainGame::updateObjectPhysics(){CB::PhysicsScope scope;updateObjectPhysicsCandidate();}'
    assert s.count(old)==1;s=s.replace(old,'void TerrainGame::updateObjectPhysics(){if(CB::finished)return;CB::PhysicsScope scope;updateObjectPhysicsCandidate();}',1);b.write_bytes(s.encode())
    probe=root/'engine/engine/inc/debug/step_pool_probe.hpp';shutil.copyfile(repo/'tools/ps2rig/step_pool_probe.hpp',probe)
    for f in [p,b,probe]:meta['step_pool_source_sha256'][str(f.relative_to(root))]=hashlib.sha256(f.read_bytes()).hexdigest()
    meta.update(step_pool_diagnostic=True,step_pool_snapshot_version=2,stop_physics_after_done=True,same_elf_admission_qualified=False,active_mode='step-pool-first-divergence',shadow_enabled=True,world_grid_verify=True,world_overlap_verify=True)
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8')


def local_inputs(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    metadata=root/'metadata.json';meta=json.loads(metadata.read_text(encoding='utf8'));assert meta.get('step_pool_diagnostic')
    p=root/'district/src/gen/game_physics.gen.cpp';s=p.read_text(encoding='utf8');old='StepPoolProbe::capture(steps,contacts);';assert s.count(old)==2
    s=s.replace(old,'StepPoolProbe::capture(steps,contacts,physBodies);');p.write_bytes(s.encode())
    probe=root/'engine/engine/inc/debug/step_pool_probe.hpp';shutil.copyfile(repo/'tools/ps2rig/step_pool_probe.hpp',probe)
    b=root/'district/src/gen/collision_baseline.gen.cpp';s=b.read_text(encoding='utf8')
    old='void TerrainGame::updateObjectPhysics(){CB::PhysicsScope scope;updateObjectPhysicsCandidate();}'
    assert s.count(old)==1;s=s.replace(old,'void TerrainGame::updateObjectPhysics(){if(CB::finished)return;CB::PhysicsScope scope;updateObjectPhysicsCandidate();}',1);b.write_bytes(s.encode())
    for f in [p,b,probe]:meta['step_pool_source_sha256'][str(f.relative_to(root))]=hashlib.sha256(f.read_bytes()).hexdigest()
    meta.update(step_pool_snapshot_version=2,stop_physics_after_done=True,same_elf_admission_qualified=False,active_mode='step-pool-local-inputs')
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8')


def common_transform(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    metadata=root/'metadata.json';meta=json.loads(metadata.read_text(encoding='utf8'))
    assert meta.get('step_pool_diagnostic') and not meta.get('step_pool_common_transform')
    clean=root/'clean-step-pool-inputs';clean.mkdir(exist_ok=False)
    files=['district/src/gen/game_physics.gen.cpp','district/src/gen/collision_baseline.gen.cpp','district/inc/game_runtime.gen.hpp','district/inc/terrain_game.hpp']
    with zipfile.ZipFile(root/'before-step-pool.zip') as z:
        for name in files+['metadata.json']:
            p=clean/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(z.read(name))
    (clean/'engine/engine/inc/debug').mkdir(parents=True);(clean/'district/bin').mkdir(parents=True)
    shutil.copyfile(root/'before-micro-cost.zip',clean/'before-micro-cost.zip')
    # Regenerate from byte-preserved inputs through explicit UTF-8 readers.
    install(repo,clean);diagnose(repo,clean)
    for name in files:shutil.copyfile(clean/name,root/name)
    for name in ['step_vertex_pool.hpp','step_pool_probe.hpp']:
        shutil.copyfile(repo/'tools/ps2rig'/name,root/'engine/engine/inc/debug'/name)
    p=root/files[0];s=p.read_text(encoding='utf8');old='#include "game_runtime.gen.hpp"';assert s.count(old)==1
    s=s.replace(old,old+'\n#include "debug/step_pose_reference.hpp"',1)
    assert s.count('Tyra::collisionTransformPoints(')==4;s=s.replace('Tyra::collisionTransformPoints(','StepPoseReference::transform(')
    old='''    if (s.box) {
      float lo[3]={1e30F,1e30F,1e30F}, hi[3]={-1e30F,-1e30F,-1e30F};
      for (int v=0;v<b.verts;++v) for(int k=0;k<3;++k) {
        lo[k]=std::min(lo[k],b.lv[v*3+k]);hi[k]=std::max(hi[k],b.lv[v*3+k]);
      }
      s.obb.centre=rbAdd(s.x1,rbMat(s.R1,rbV((lo[0]+hi[0])*.5F,
          (lo[1]+hi[1])*.5F,(lo[2]+hi[2])*.5F)));
      for(int k=0;k<3;++k) s.obb.halfAxes[k]=rbMul(
          rbV(s.R1[k],s.R1[k+3],s.R1[k+6]),(hi[k]-lo[k])*.5F);
    }'''
    assert s.count(old)==2;s=s.replace(old,'    if(s.box)StepPoseReference::box(b.lv,b.verts,s.R1,s.x1,s.obb);');p.write_bytes(s.encode('utf8'))
    helper=root/'engine/engine/inc/debug/step_pose_reference.hpp';shutil.copyfile(repo/'tools/ps2rig/step_pose_reference.hpp',helper)
    meta['lineage_pre_common_pose_source_sha256']=meta.pop('step_pool_source_sha256')
    tracked=[root/f for f in files]+[root/'engine/engine/inc/debug'/name for name in ['step_vertex_pool.hpp','step_pool_probe.hpp','step_pose_reference.hpp']]
    meta['step_pool_source_sha256']={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest()for f in tracked}
    meta.update(step_pool_common_transform=True,source_regenerated_utf8=True,stop_physics_after_done=True,
        same_elf_admission_qualified=False,active_mode='step-pool-common-pose-verify',
        comparison='Both wide and pooled layouts now share one compiled EE vertex transform and OBB builder. This is a new common pose baseline; old inline trajectories and performance do not qualify it. All vertices, contacts, canonical positive confirmations and6+3 passes retained.')
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--diagnose',action='store_true');p.add_argument('--local-inputs',action='store_true');p.add_argument('--common-transform',action='store_true');a=p.parse_args()
    if a.common_transform:common_transform(a.repo.resolve(),a.scratch.resolve())
    elif a.local_inputs:local_inputs(a.repo.resolve(),a.scratch.resolve())
    elif a.diagnose:diagnose(a.repo.resolve(),a.scratch.resolve())
    else:install(a.repo.resolve(),a.scratch.resolve())
