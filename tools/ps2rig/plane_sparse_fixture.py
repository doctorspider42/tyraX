"""Same-ELF exact sparse-plane experiment, retaining generic fallback and6+3."""
from pathlib import Path
import argparse,json,shutil,subprocess

PROBE=r'''
#pragma once
#include "debug/plane_sparse.hpp"
namespace Vehicle_playground { namespace PlaneSparseProbe {
using namespace runtime_detail;
inline unsigned calls=0,checked=0,errors=0,shapes=0,eligibleShapes=0,guardFallback=0;
inline bool classify(const float* planes,int count){
  const bool eligible=PlaneSparse::classify(planes,count);
  if(Tyra::vu0SatVerify && !CB::baseline){++shapes;eligibleShapes+=eligible;}
  return eligible;
}
inline unsigned bits(float x){unsigned u;memcpy(&u,&x,4);return u;}
inline int inside(const float* lp,int np,RbV l,float* depth,bool shape){
  if(CB::baseline)return rbInsideHull(lp,np,l,depth);
  const bool verify=Tyra::vu0SatVerify;
  if(verify)++calls;
  if(!shape)return rbInsideHull(lp,np,l,depth);
  if(!PlaneSparse::normalPoint(l)){
    if(verify)++guardFallback;
    return rbInsideHull(lp,np,l,depth);
  }
  float candidateDepth=0;const int candidate=PlaneSparse::inside(lp,l,&candidateDepth);
  if(!verify){if(candidate>=0)*depth=candidateDepth;return candidate;}
  ++checked;float referenceDepth=0;const int reference=rbInsideHull(lp,np,l,&referenceDepth);
  if(candidate!=reference || (candidate>=0 && bits(candidateDepth)!=bits(referenceDepth))){
    if(errors++<4)printf("PLANESPARSEDIFF point=%.9g,%.9g,%.9g face=%d/%d depth=%.9g/%.9g bits=%08x/%08x\n",l.x,l.y,l.z,candidate,reference,candidateDepth,referenceDepth,bits(candidateDepth),bits(referenceDepth));
  }
  // Shadow mode always returns the original result, even if the candidate fails.
  if(reference>=0)*depth=referenceDepth;
  return reference;
}
inline void flush(int phase){
  char name[64];snprintf(name,sizeof(name),"plane-sparse-%d.csv",phase);
  FILE* f=fopen(name,"wb");if(!f)++errors;
  if(f){fprintf(f,"phase,calls,checked,errors,shapes,eligible_shapes,guard_fallback\n%d,%u,%u,%u,%u,%u,%u\n",phase,calls,checked,errors,shapes,eligibleShapes,guardFallback);fclose(f);}
  printf("PLANESPARSE phase=%d calls=%u checked=%u errors=%u shapes=%u eligible_shapes=%u guard_fallback=%u file=%d\n",phase,calls,checked,errors,shapes,eligibleShapes,guardFallback,f!=nullptr);fflush(stdout);
  calls=checked=errors=shapes=eligibleShapes=guardFallback=0;
}
static_assert(sizeof(RbStep)==880,"Sparse flag must use existing bool padding");
} } // Vehicle_playground::PlaneSparseProbe
'''

def install(repo,root,verify):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    p=root/'metadata.json';meta=json.loads(p.read_text(encoding='utf8'))
    assert meta.get('world_cache') and meta.get('shared_physics') and not meta.get('fixed_simulation_step')
    engine=root/'engine/engine';project=root/'district'
    if not meta.get('plane_sparse_comparison'):
        assert meta.get('solver_scratchpad')
        p=project/'inc/game_runtime.gen.hpp';s=p.read_text(encoding='utf8')
        before='  bool sphere;\n  float sphereR;';assert s.count(before)==1
        s=s.replace(before,'  bool sphere;\n  bool sparseBox; // exact local plane pattern, recomputed once per step\n  float sphereR;',1)
        runtime=s
        p=project/'src/gen/game_physics.gen.cpp';s=p.read_text(encoding='utf8')
        for before in ['#include "debug/solver_layout_probe.hpp"\n','  if(CB::frame==300 || CB::frame==600)SolverLayoutProbe::run(steps,contacts,CB::phase,CB::frame);\n','  SolverLayoutProbe::after(steps,contacts,CB::phase,CB::frame);\n']:
            assert s.count(before)==1;s=s.replace(before,'',1)
        before='#include "game_runtime.gen.hpp"';assert s.count(before)==1
        s=s.replace(before,before+'\n#include "debug/plane_sparse_probe.hpp"',1)
        before='    s.box = o.data.type == 0 && b.verts == 8;';assert s.count(before)==1
        s=s.replace(before,'    s.sparseBox=PlaneSparseProbe::classify(b.lp,b.planes);\n'+before,1)
        changes=[('rbInsideHull(B.lp, B.planes, lc, &depth)','PlaneSparseProbe::inside(B.lp,B.planes,lc,&depth,S.sparseBox)'),
                 ('rbInsideHull(Bj.lp, Bj.planes, rbMatT(Bs.R1, rel), &depth)','PlaneSparseProbe::inside(Bj.lp,Bj.planes,rbMatT(Bs.R1,rel),&depth,Bs.sparseBox)'),
                 ('rbInsideHull(Ba.lp, Ba.planes, rbMatT(A.R1, rel), &depth)','PlaneSparseProbe::inside(Ba.lp,Ba.planes,rbMatT(A.R1,rel),&depth,A.sparseBox)')]
        for before,after in changes:assert s.count(before)==1;s=s.replace(before,after,1)
        physics=s
        p=engine/'inc/debug/collision_bench.hpp';s=p.read_text(encoding='utf8')
        before='#include "debug/physics_pmu_probe.hpp"\n';assert s.count(before)==1;s=s.replace(before,'',1)
        assert 'PhysicsPmu::' not in s
        bench=s
        p=project/'src/gen/collision_baseline.gen.cpp';s=p.read_text(encoding='utf8')
        before='#include "game_runtime.gen.hpp"';assert s.count(before)==1
        s=s.replace(before,before+'\n#include "debug/plane_sparse_probe.hpp"',1)
        before='CB::phase=12;if(!PhysicsPmu::calibrate()){CB::finished=true;printf("PHYSICSPMU calibration rejected\\n");return;}';assert s.count(before)==1
        s=s.replace(before,'',1)
        before='if(CB::phase>=14)';assert s.count(before)==1
        s=s.replace(before,'if(CB::phase>=((Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)?14:CB::phases))',1)
        before='CB::flush();++CB::phase;';assert s.count(before)==1
        s=s.replace(before,'CB::flush();PlaneSparseProbe::flush(CB::phase);++CB::phase;',1)
        baseline=s
        (project/'inc/game_runtime.gen.hpp').write_bytes(runtime.encode());(project/'src/gen/game_physics.gen.cpp').write_bytes(physics.encode())
        (engine/'inc/debug/collision_bench.hpp').write_bytes(bench.encode());p.write_bytes(baseline.encode())
        (engine/'inc/debug/plane_sparse_probe.hpp').write_bytes(PROBE.encode())
        shutil.copyfile(repo/'tools/ps2rig/plane_sparse.hpp',engine/'inc/debug/plane_sparse.hpp')
    cfg=project/'bin/sat-verify.cfg'
    if verify:cfg.write_bytes(b'verify\n')
    elif cfg.exists():cfg.unlink()
    for name in ['solver_scratchpad','solver_layout','physics_pmu','layout_source_sha256','pmu_source_sha256','scratchpad_scope','pmu_scope']:
        meta.pop(name,None)
    meta.update(plane_sparse_comparison=True,plane_sparse_verify=verify,world_grid_verify=verify,shadow_enabled=verify,
        active_mode='plane-sparse-verify' if verify else 'plane-sparse-timing',same_elf_admission_qualified=False,expected_phases=list(range(28)),
        comparison='Shared cached-world/body8m scalar physics: generic plane dot products vs exact signed-axis planes with full fallback, same six/three solver passes; no replay or PMU instrumentation')
    (root/'metadata.json').write_text(json.dumps(meta,indent=2),encoding='utf8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path);p.add_argument('--verify',action='store_true')
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();install(a.repo.resolve(),a.scratch.resolve(),a.verify)
