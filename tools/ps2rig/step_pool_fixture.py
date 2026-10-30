"""Same-ELF wide versus pooled pose storage; all collision arithmetic retained."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile


def install(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    metadata=root/'metadata.json';meta=json.loads(metadata.read_text());assert meta.get('micro_cost_profile') and not meta.get('step_pool_comparison')
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
    header=runtime.read_text();start=header.index('struct RbStep {');stop=header.index('\n};',start)+3
    record=header[start:stop].replace('struct RbStep {','struct RbPoolStep {',1)
    for old,new in [('RbV wv0[PHYS_MAX_HULL_VERTS];','RbV* wv0;'),('RbV wv[PHYS_MAX_HULL_VERTS];','RbV* wv;')]:
        assert record.count(old)==1;record=record.replace(old,new,1)
    record+='\nstatic_assert(sizeof(RbStep)==880,"wide pose ABI changed");\nstatic_assert(sizeof(void*)!=4 || sizeof(RbPoolStep)==312,"pooled EE pose ABI changed");\n'
    header=header[:stop]+'\n'+record+header[stop:]
    declarations=terrain.read_text();old='void updateObjectPhysicsCandidate();';assert declarations.count(old)==1
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
    metadata=root/'metadata.json';meta=json.loads(metadata.read_text());assert meta.get('step_pool_comparison')
    p=root/'district/src/gen/game_physics.gen.cpp';s=p.read_text();assert 'StepPoolProbe::' not in s
    before='#include "debug/world_overlap_probe.hpp"';assert s.count(before)==1
    s=s.replace(before,before+'\n#include "debug/step_pool_probe.hpp"',1)
    before='  WorldOverlapProbe::capture(contacts);';assert s.count(before)==2
    s=s.replace(before,before+'\n  StepPoolProbe::capture(steps,contacts,physBodies);');p.write_bytes(s.encode())
    b=root/'district/src/gen/collision_baseline.gen.cpp';s=b.read_text();assert s.count('#include "debug/world_overlap_probe.hpp"')==1
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
    metadata=root/'metadata.json';meta=json.loads(metadata.read_text());assert meta.get('step_pool_diagnostic')
    p=root/'district/src/gen/game_physics.gen.cpp';s=p.read_text();old='StepPoolProbe::capture(steps,contacts);';assert s.count(old)==2
    s=s.replace(old,'StepPoolProbe::capture(steps,contacts,physBodies);');p.write_bytes(s.encode())
    probe=root/'engine/engine/inc/debug/step_pool_probe.hpp';shutil.copyfile(repo/'tools/ps2rig/step_pool_probe.hpp',probe)
    b=root/'district/src/gen/collision_baseline.gen.cpp';s=b.read_text()
    old='void TerrainGame::updateObjectPhysics(){CB::PhysicsScope scope;updateObjectPhysicsCandidate();}'
    assert s.count(old)==1;s=s.replace(old,'void TerrainGame::updateObjectPhysics(){if(CB::finished)return;CB::PhysicsScope scope;updateObjectPhysicsCandidate();}',1);b.write_bytes(s.encode())
    for f in [p,b,probe]:meta['step_pool_source_sha256'][str(f.relative_to(root))]=hashlib.sha256(f.read_bytes()).hexdigest()
    meta.update(step_pool_snapshot_version=2,stop_physics_after_done=True,same_elf_admission_qualified=False,active_mode='step-pool-local-inputs')
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--diagnose',action='store_true');p.add_argument('--local-inputs',action='store_true');a=p.parse_args()
    if a.local_inputs:local_inputs(a.repo.resolve(),a.scratch.resolve())
    elif a.diagnose:diagnose(a.repo.resolve(),a.scratch.resolve())
    else:install(a.repo.resolve(),a.scratch.resolve())
