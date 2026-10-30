"""Profile outer scalar physics stages on physical EE; never a timing gate."""
from pathlib import Path
import argparse,json,shutil,subprocess

def install(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    p=root/'metadata.json';meta=json.loads(p.read_text(encoding='utf8'))
    assert meta.get('world_cache') and meta.get('shared_physics') and not meta.get('world_grid_verify')
    assert not meta.get('physics_pmu'),'Use the previously installed probe without reinstrumenting it'
    engine=root/'engine/engine';project=root/'district'
    p=engine/'inc/debug/collision_bench.hpp';s=p.read_text(encoding='utf8')
    s='#include "debug/physics_pmu_probe.hpp"\n'+s
    before='  phys = awake = contacts = drops = pushDrops = pushes = 0;';assert s.count(before)==1
    s=s.replace(before,'  PhysicsPmu::frame(frame,contacts,awake,ready && !finished && frame>=warm && frame<total);\n'+before,1)
    before='  WorldGridProbe::flush(phase);';assert s.count(before)==1
    s=s.replace(before,'  PhysicsPmu::flush(phase);\n'+before,1)
    before='  explicit StageScope(int s):start(ticks()),stage(s) {}';assert s.count(before)==1
    s=s.replace(before,'  PhysicsPmu::Scope pmu;\n  explicit StageScope(int s):start(ticks()),stage(s),pmu(s<=3?s+1:s==6?6:-1) {}',1)
    before='  SolverScope() : start(ticks()) {}';assert s.count(before)==1
    s=s.replace(before,'  PhysicsPmu::Scope pmu{5};\n'+before,1)
    before='struct PhysicsScope {\n  U start;\n  PhysicsScope() : start(ticks()) {}';assert s.count(before)==1
    s=s.replace(before,'struct PhysicsScope {\n  PhysicsPmu::Owner owner;\n  PhysicsPmu::Scope pmu;\n  U start;\n  PhysicsScope() : owner(frame%3),pmu(0),start(ticks()) {}',1)
    header=s
    p=project/'src/gen/collision_baseline.gen.cpp';s=p.read_text(encoding='utf8')
    before='printf("VUSAT mode=%s\\n",Tyra::vu0SatVerify?"verify":"timing");';assert s.count(before)==1
    s=s.replace(before,before+'CB::phase=12;if(!PhysicsPmu::calibrate()){CB::finished=true;printf("PHYSICSPMU calibration rejected\\n");return;}',1)
    before='((Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)?14:CB::phases)';assert s.count(before)==1
    s=s.replace(before,'14',1)
    shutil.copyfile(repo/'tools/ps2rig/physics_pmu_probe.hpp',engine/'inc/debug/physics_pmu_probe.hpp')
    (engine/'inc/debug/collision_bench.hpp').write_bytes(header.encode());p.write_bytes(s.encode())
    meta.update(physics_pmu=True,active_mode='physics-pmu-profile',expected_phases=[12,13],scene_timing_qualified=False,
        admission_reference_elf=meta.get('elf_sha256'),same_elf_admission_qualified=False,
        pmu_modes=['I-cache misses / D-cache misses','single / dual issue','completed instructions / COP1 completed'],
        pmu_stages=['physics','prediction','world','body','grid','solver','writeback','unused'],
        pmu_scope='Outer stage PMU instrumentation only; query/SAT high-frequency scopes retain their existing CP0-only timing. This ELF cannot qualify production frame performance.')
    (root/'metadata.json').write_text(json.dumps(meta,indent=2),encoding='utf8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args()
    install(a.repo.resolve(),a.scratch.resolve())
