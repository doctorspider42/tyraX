"""Install external PC sampling without editing the physics translation unit."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile

def install(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    metadata=root/'metadata.json';meta=json.loads(metadata.read_text(encoding='utf8'))
    assert meta.get('shared_scene_comparison') and not meta.get('pipeline_cost_profile') and not meta.get('external_pc_sample')
    physics=root/'district/src/gen/game_physics.gen.cpp';baseline=root/'district/src/gen/collision_baseline.gen.cpp'
    physics_hash=hashlib.sha256(physics.read_bytes()).hexdigest()
    with zipfile.ZipFile(root/'before-pc-sample.zip','x',zipfile.ZIP_DEFLATED) as z:
        for p in [physics,baseline,metadata,root/'district/Makefile']:z.write(p,p.relative_to(root))
    s=baseline.read_text(encoding='utf8')
    declarations='''extern "C" bool pcSampleConfigure();
extern "C" void pcSampleBegin(int,int,bool);
extern "C" void pcSampleEnd();
extern "C" void pcSampleFlush(int);
'''
    s=declarations+s
    old='void TerrainGame::updateObjectPhysics(){if(CB::finished)return;CB::PhysicsScope scope;updateObjectPhysicsCandidate();}'
    assert s.count(old)==1
    s=s.replace(old,'void TerrainGame::updateObjectPhysics(){if(CB::finished)return;pcSampleBegin(CB::phase,CB::frame,CB::ready);{CB::PhysicsScope scope;updateObjectPhysicsCandidate();}pcSampleEnd();}',1)
    old='checked=true;FILE* verify=';assert s.count(old)==1
    s=s.replace(old,'checked=true;if(!pcSampleConfigure()){CB::finished=true;return;}FILE* verify=',1)
    old='if(Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)CB::phase=12;else CB::phase=10;';assert s.count(old)==1;s=s.replace(old,'CB::phase=12;',1)
    old='StepPoolProbe::flush(CB::phase);';assert s.count(old)==1;s=s.replace(old,old+'pcSampleFlush(CB::phase);',1)
    old='if(!Tyra::vu0SatVerify && CB::phase==14)CB::phase=24;';assert s.count(old)==1;s=s.replace(old,'',1)
    old='CB::phase>=((Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)?14:CB::phases)';assert s.count(old)==1;s=s.replace(old,'CB::phase>=14',1)
    baseline.write_bytes(s.encode('utf8'))
    source=root/'district/src/gen/pc_sample_probe.gen.cpp';shutil.copyfile(repo/'tools/ps2rig/pc_sample_probe.cpp',source)
    assert hashlib.sha256(physics.read_bytes()).hexdigest()==physics_hash
    meta['lineage_pre_pc_sample_source_sha256']=meta.pop('shared_scene_source_sha256')
    meta.update(external_pc_sample=True,pc_sample_enabled=True,pc_sample_period_bus=149381,pc_sample_capacity=65536,
        measured_physics_source_sha256=physics_hash,shadow_enabled=True,world_grid_verify=True,world_overlap_verify=True,
        same_elf_admission_qualified=False,expected_phases=[12,13],active_mode='external-pc-sample-admission')
    meta['pc_sample_source_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [physics,baseline,source,root/'district/Makefile']}
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8')
    (root/'district/bin/pc-sample.cfg').write_bytes(b'enabled\n');(root/'district/bin/sat-verify.cfg').write_bytes(b'verify\n')
    print('External PC sampler installed; physics source remains byte-identical. Verify its object after build.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',type=Path,required=True)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();install(a.repo.resolve(),a.scratch.resolve())
