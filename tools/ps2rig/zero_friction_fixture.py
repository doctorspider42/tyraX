"""Compare exact zero-friction work elimination in the current shared solver."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile

def install(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    metadata=root/'metadata.json';physics=root/'district/src/gen/game_physics.gen.cpp';baseline=root/'district/src/gen/collision_baseline.gen.cpp'
    # Remove the qualified external sampler by restoring its exact inputs.
    with zipfile.ZipFile(root/'before-pc-sample.zip') as z:
        source=z.read('district/src/gen/game_physics.gen.cpp').decode('utf8')
        base=z.read('district/src/gen/collision_baseline.gen.cpp').decode('utf8')
        meta=json.loads(z.read('metadata.json').decode('utf8'))
    assert meta.get('shared_scene_comparison') and not meta.get('zero_friction_comparison')
    with zipfile.ZipFile(root/'before-zero-friction.zip','x',zipfile.ZIP_DEFLATED) as z:
        for p in [physics,baseline,metadata,root/'district/Makefile']:z.write(p,p.relative_to(root))
    sampler=root/'district/src/gen/pc_sample_probe.gen.cpp'
    assert sampler.is_file();sampler.rename(root/'pc_sample_probe.saved.cpp')
    cfg=root/'district/bin/pc-sample.cfg'
    if cfg.exists():cfg.rename(root/'pc-sample-before-zero.cfg')
    begin=source.index('void TerrainGame::updateObjectPhysicsCandidate() {');end=source.index('void TerrainGame::pushPhysicsBodies(',begin)
    body=source[begin:end]
    old='  const bool useShared=!CB::baseline;';assert body.count(old)==1
    body=body.replace(old,'''  const bool useShared=true;
  // Volatile mode keeps the original solve loop shared between both arms.
  const volatile bool skipZeroFriction=CB::ready&&!CB::baseline;
  unsigned zeroFrictionSkipped=0;''',1)
    old='      // friction, against whatever tangential slip is left';assert body.count(old)==1
    body=body.replace(old,'''      // A zero-radius Coulomb disk and no prior tangent impulse imply zero
      // friction change. Keep all normal/position solves and contact records.
      if(skipZeroFriction&&c.jn==0&&c.ft.x==0&&c.ft.y==0&&c.ft.z==0){++zeroFrictionSkipped;continue;}
'''+old,1)
    old='  // Split impulse: the overlap still left after the velocity solve is';assert body.count(old)==1
    body=body.replace(old,'  ZeroFrictionProbe::record(skipZeroFriction,unsigned(nc)*PHYS_ITERATIONS,zeroFrictionSkipped);\n'+old,1)
    source=source[:begin]+body+source[end:]
    marker='#include "debug/world_overlap_probe.hpp"';assert source.count(marker)==1 and base.count(marker)==1
    source=source.replace(marker,marker+'\n#include "debug/zero_friction_probe.hpp"',1)
    base=base.replace(marker,marker+'\n#include "debug/zero_friction_probe.hpp"',1)
    old='StepPoolProbe::flush(CB::phase);';assert base.count(old)==1;base=base.replace(old,old+'ZeroFrictionProbe::flush(CB::phase);',1)
    physics.write_bytes(source.encode('utf8'));baseline.write_bytes(base.encode('utf8'))
    helper=root/'engine/engine/inc/debug/zero_friction_probe.hpp';shutil.copyfile(repo/'tools/ps2rig/zero_friction_probe.hpp',helper)
    meta['lineage_pre_zero_friction_source_sha256']=meta.pop('shared_scene_source_sha256')
    meta.update(zero_friction_comparison=True,shared_scene_comparison=False,candidate='Exact zero-normal/zero-prior-friction guard in legacy EE solver; both arms shared VU0 packets',
        shadow_enabled=True,world_grid_verify=True,world_overlap_verify=True,same_elf_admission_qualified=False,
        expected_phases=[12,13],active_mode='zero-friction-admission')
    meta['zero_friction_source_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [physics,baseline,helper]}
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8');(root/'district/bin/sat-verify.cfg').write_bytes(b'verify\n')
    print('Zero-friction guard comparison installed: same pooled/shared pipeline,6+3 passes unchanged')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',type=Path,required=True)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();install(a.repo.resolve(),a.scratch.resolve())
