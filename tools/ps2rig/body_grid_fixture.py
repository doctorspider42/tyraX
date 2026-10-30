"""Install a fixed-snapshot cell-size probe into an existing private micro fixture."""
from pathlib import Path
import argparse,json,shutil,subprocess

def install(repo,root,timing_cell=None):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve()
    assert root.is_relative_to(allowed) and root!=allowed
    meta_path=root/'metadata.json';meta=json.loads(meta_path.read_text(encoding='utf8'))
    assert meta.get('micro_capture')
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
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args()
    install(a.repo.resolve(),a.scratch.resolve(),a.timing_cell)
