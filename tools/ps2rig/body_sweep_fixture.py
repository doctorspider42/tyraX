"""Install the sorted-prefix body index in the qualified shared scalar rig."""
from pathlib import Path
import argparse,json,shutil,subprocess

def install(repo,root,verify):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    p=root/'metadata.json';meta=json.loads(p.read_text(encoding='utf8'))
    assert meta.get('world_cache') and meta.get('shared_physics') and meta.get('fixed_simulation_step')
    engine=root/'engine/engine';project=root/'district'
    if not meta.get('body_sweep_comparison'):
        assert meta.get('plane_dense_control') and meta.get('plane_sparse_comparison')
        p=project/'inc/game_runtime.gen.hpp';s=p.read_text(encoding='utf8');before='  bool sparseBox; // exact local plane pattern, recomputed once per step\n';assert s.count(before)==1
        runtime=s.replace(before,'',1)
        p=project/'src/gen/game_physics.gen.cpp';s=p.read_text(encoding='utf8')
        before='#include "debug/plane_sparse_probe.hpp"';assert s.count(before)==1;s=s.replace(before,'#include "debug/body_sweep_probe.hpp"',1)
        before='    s.sparseBox=PlaneSparseProbe::classify(b.lp,b.planes);\n';assert s.count(before)==1;s=s.replace(before,'',1)
        for before,after in [('PlaneSparseProbe::inside(B.lp,B.planes,lc,&depth,S.sparseBox)','rbInsideHull(B.lp,B.planes,lc,&depth)'),
            ('PlaneSparseProbe::inside(Bj.lp,Bj.planes,rbMatT(Bs.R1,rel),&depth,Bs.sparseBox)','rbInsideHull(Bj.lp,Bj.planes,rbMatT(Bs.R1,rel),&depth)'),
            ('PlaneSparseProbe::inside(Ba.lp,Ba.planes,rbMatT(A.R1,rel),&depth,A.sparseBox)','rbInsideHull(Ba.lp,Ba.planes,rbMatT(A.R1,rel),&depth)')]:
            assert s.count(before)==1;s=s.replace(before,after,1)
        before='''  static Tyra::BroadPhase bodyPairs;
  static int bodyPairPhase=-1;
  if(bodyPairPhase!=CB::phase){
    bodyPairs=Tyra::BroadPhase(Tyra::BroadPhase::kGrid,8.0F);
    bodyPairPhase=CB::phase;
  }''';assert s.count(before)==1;s=s.replace(before,'  static BodySweepProbe::Index bodyPairs;',1)
        physics=s
        p=project/'src/gen/collision_baseline.gen.cpp';s=p.read_text(encoding='utf8');assert s.count('#include "debug/plane_sparse_probe.hpp"')==1
        s=s.replace('#include "debug/plane_sparse_probe.hpp"','#include "debug/body_sweep_probe.hpp"',1)
        before='PlaneSparseProbe::flush(CB::phase);';assert s.count(before)==1;s=s.replace(before,'BodySweepProbe::flush(CB::phase);',1)
        (project/'inc/game_runtime.gen.hpp').write_bytes(runtime.encode());(project/'src/gen/game_physics.gen.cpp').write_bytes(physics.encode());p.write_bytes(s.encode())
        for name in ['body_sweep.hpp','body_sweep_probe.hpp']:shutil.copyfile(repo/'tools/ps2rig'/name,engine/'inc/debug'/name)
    cfg=project/'bin/sat-verify.cfg'
    if verify:cfg.write_bytes(b'verify\n')
    elif cfg.exists():cfg.unlink()
    for name in ['plane_sparse_comparison','plane_sparse_verify','plane_dense_control','plane_source_sha256']:meta.pop(name,None)
    if 'source_sha256' in meta:meta['lineage_source_sha256']=meta.pop('source_sha256')
    meta.update(body_sweep_comparison=True,body_sweep_verify=verify,world_grid_verify=verify,shadow_enabled=verify,
        candidate='e46985acc selected scalar pipeline plus private sorted-prefix body index',same_elf_admission_qualified=False,
        active_mode='body-sweep-verify' if verify else 'body-sweep-timing',scene_timing_qualified=False,
        comparison='Shared cached-world/body scalar physics, generic planes:8m grid vs sorted inclusive body boxes/prefix maxima; linear storage, original IDs and full fallback, unchanged6+3 solver',
        expected_phases=[10,11,12,13,24,25,26,27])
    (root/'metadata.json').write_text(json.dumps(meta,indent=2),encoding='utf8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path);p.add_argument('--verify',action='store_true')
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();install(a.repo.resolve(),a.scratch.resolve(),a.verify)
