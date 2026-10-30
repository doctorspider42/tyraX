"""Install a fixed-snapshot cell-size probe into an existing private micro fixture."""
from pathlib import Path
import argparse,json,shutil,subprocess

def install(repo,root,timing_cell=None,fixed_step=False,shared_physics=False,dense_only=False):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve()
    assert root.is_relative_to(allowed) and root!=allowed
    meta_path=root/'metadata.json';meta=json.loads(meta_path.read_text(encoding='utf8'))
    assert meta.get('micro_capture')
    if shared_physics:
        assert meta.get('fixed_simulation_step') and meta.get('body_grid_timing_cell')==2
        assert not meta.get('shared_physics') and meta.get('micro_dispatch') is False
        p=root/'district/src/gen/game_physics.gen.cpp';s=p.read_text(encoding='utf8')
        before='  static Tyra::BroadPhase bodyPairs(Tyra::BroadPhase::kGrid,2.0F);';assert s.count(before)==1
        s=s.replace(before,'''  static Tyra::BroadPhase bodyPairs;
  static int bodyPairPhase=-1;
  if(bodyPairPhase!=CB::phase){
    bodyPairs=Tyra::BroadPhase(Tyra::BroadPhase::kGrid,CB::baseline?8.0F:2.0F);
    bodyPairPhase=CB::phase;
  }''',1);candidate_path=p;candidate_text=s
        p=root/'district/src/gen/collision_baseline.gen.cpp';s=p.read_text(encoding='utf8')
        for name,args in [('updateObjectPhysics',''),('broadPhaseRefresh',''),('broadPhaseNear','b,out'),('broadPhaseAlong','a,b,r,out')]:
            before=f'if(CB::baseline){name}Baseline({args});else {name}Candidate({args});'
            assert s.count(before)==1,(name,before)
            s=s.replace(before,f'{name}Candidate({args});',1)
        before='CB::frame=CB::phase==0?-300:0;';assert s.count(before)==1
        s=s.replace(before,'CB::frame=CB::phase<2?-300:0;',1)
        if dense_only:
            before='if(Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)CB::phase=12;';assert s.count(before)==1
            s=s.replace(before,before+'else CB::phase=10;',1)
            before='CB::flush();++CB::phase;CB::ready=false;';assert s.count(before)==1
            s=s.replace(before,'CB::flush();++CB::phase;if(!Tyra::vu0SatVerify && CB::phase==14)CB::phase=24;CB::ready=false;',1)
        candidate_path.write_bytes(candidate_text.encode());p.write_bytes(s.encode())
        meta.update(shared_physics=True,expected_phases=[10,11,12,13,24,25,26,27]if dense_only else list(range(28)),
            scene_timing_qualified=False,comparison='One shared physics/query implementation, changing only minimum body-grid cells at phase boundaries; canonical scalar in both arms')
        meta_path.write_text(json.dumps(meta,indent=2),encoding='utf8');return
    if fixed_step:
        assert meta.get('body_grid_timing_cell') and meta.get('micro_dispatch') is False
        p=root/'district/src/terrain_game.cpp';s=p.read_text(encoding='utf8')
        if 'FIXEDSTEP benchmark_nominal_display_step' not in s:
            before='  benchTick();';assert s.count(before)==1
            s=s.replace(before,before+'''
  // Private controlled-workload benchmark, never emitted by production codegen.
  if(CB::ready){g_frameDt=1.0F/g_frameRate;g_frameScale=g_frameDt*50.0F;
    static bool logged=false;if(!logged){logged=true;printf("FIXEDSTEP benchmark_nominal_display_step hz=%g\\n",g_frameRate);fflush(stdout);}}
''',1);p.write_bytes(s.encode())
        meta.update(fixed_simulation_step=True,scene_timing_qualified=False,
            simulation_clock='Private benchmark uses nominal display step in both arms; CP0 work/frame measurements remain real wall time. Production clock unchanged.')
        meta_path.write_text(json.dumps(meta,indent=2),encoding='utf8');return
    engine=root/'engine/engine';rig=repo/'tools/ps2rig'
    shutil.copyfile(rig/'body_grid_probe.hpp',engine/'inc/debug/body_grid_probe.hpp')
    for rel in ['inc/physics/broad_phase.hpp','src/physics/broad_phase.cpp']:
        shutil.copyfile(repo/'vendor/tyra/engine'/rel,engine/rel)
    p=root/'district/src/gen/game_physics.gen.cpp';s=p.read_text(encoding='utf8')
    if timing_cell is not None:
        assert meta.get('body_grid_probe') and 'BodyGridProbe::begin' in s
        lines=s.splitlines(keepends=True)
        s=''.join(line for line in lines if 'BodyGridProbe::' not in line and '"debug/body_grid_probe.hpp"' not in line)
        before='  static Tyra::BroadPhase bodyPairs;';assert s.count(before)==1
        s=s.replace(before,f'  static Tyra::BroadPhase bodyPairs(Tyra::BroadPhase::kGrid,{timing_cell}.0F);')
        before='const bool useMicro = CB::solverArm == 1 && !TYRA_COLLISION_VU0 && microBatch.begin();';assert s.count(before)==1
        s=s.replace(before,'const bool useMicro = false; // grid A/B: canonical EE physics in both arms',1)
        # Do not let a previous diagnostic cfg silently select only phases12/13.
        cfg=root/'district/bin/sat-verify.cfg'
        if cfg.exists():cfg.unlink()
        p.write_bytes(s.encode())
        meta.update(body_grid_probe=False,body_grid_timing_cell=timing_cell,micro_dispatch=False,
            active_mode='body-grid-timing',shadow_enabled=False,scene_timing_qualified=False,
            comparison='Original8m body grid vs tuned body grid; canonical scalar SAT/CCD and unchanged6+3 solver in both arms')
        meta_path.write_text(json.dumps(meta,indent=2),encoding='utf8')
        return
    if 'BodyGridProbe::begin' not in s:
        s='#include "debug/body_grid_probe.hpp"\n'+s
        before='  bodyPairs.begin((int)bodies.size());';assert before in s
        s=s.replace(before,'  BodyGridProbe::begin(CB::phase,CB::frame,bodies.size());\n'+before,1)
        before='    bodyPairs.set((int)pairIndex, box, false);';assert before in s
        s=s.replace(before,'    if(BodyGridProbe::capture)BodyGridProbe::boxes[pairIndex]=box;\n'+before,1)
        before='    {CB::StageScope q(4);bodyPairs.queryBox(pairArea,nearBodies);}';assert before in s
        s=s.replace(before,'    if(BodyGridProbe::capture)BodyGridProbe::queries.push_back(pairArea);\n'+before,1)
        before='  Vu0SatCapture::flush();';assert before in s
        s=s.replace(before,'  BodyGridProbe::run(CB::phase,CB::frame); // drained, diagnostic timing only\n'+before,1)
        p.write_bytes(s.encode())
    meta.update(body_grid_probe=True,scene_timing_qualified=False)
    meta_path.write_text(json.dumps(meta,indent=2),encoding='utf8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--timing-cell',type=int,choices=[1,2,4],help='Remove probes and select the candidate cell size; disables micro dispatch in both arms')
    p.add_argument('--fixed-step',action='store_true',help='After a normal-clock timing run, pin only the private benchmark simulation to the nominal display step')
    p.add_argument('--shared-physics',action='store_true',help='Use one physics/query implementation in both arms, varying only body-grid size at phase boundaries')
    p.add_argument('--dense-only',action='store_true',help='With --shared-physics, measure64/128 piles in both repeat orders')
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args()
    assert sum([a.fixed_step,a.timing_cell is not None,a.shared_physics])<=1
    assert not a.dense_only or a.shared_physics
    install(a.repo.resolve(),a.scratch.resolve(),a.timing_cell,a.fixed_step,a.shared_physics,a.dense_only)
