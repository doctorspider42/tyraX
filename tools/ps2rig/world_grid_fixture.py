"""Install an experimental world-only index into the private scalar body-grid rig."""
from pathlib import Path
import argparse,json,shutil,subprocess

def install(repo,root,verify,body_cell=8):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    meta_path=root/'metadata.json';meta=json.loads(meta_path.read_text(encoding='utf8'))
    assert meta.get('body_grid_timing_cell') in [2,8] and meta.get('micro_dispatch') is False
    if meta.get('world_grid_comparison'):assert meta.get('world_body_cell')==body_cell
    engine=root/'engine/engine';rig=repo/'tools/ps2rig'
    shutil.copyfile(rig/'world_grid_probe.hpp',engine/'inc/debug/world_grid_probe.hpp')
    p=root/'district/src/gen/game_physics.gen.cpp';s=p.read_text(encoding='utf8')
    if 'static Tyra::BroadPhase worldPairs;' not in s:
        s='#include "debug/world_grid_probe.hpp"\n'+s
        before='  // --- 2. contacts against the world';assert s.count(before)==1
        start=s.index(before);end=s.index('\n',start)+1
        shared=bool(meta.get('shared_physics'))
        code='''  // Reuse the general collider cache; retain scripted moving world solids.
  static Tyra::BroadPhase worldPairs;
  if(awakeCount>0){
    if(bpFrame!=g_frameStamp)broadPhaseRefresh();
    if(worldPairs.kind()!=broadPhase.kind())worldPairs=Tyra::BroadPhase(broadPhase.kind());
    worldPairs.begin(int(runtimeObjects.size()));
    for(size_t index=0;index<bpCache.size();++index){const auto& entry=bpCache[index];
      if(entry.present && !runtimeObjects[index].data.physics)
        worldPairs.set(int(index),entry.box,entry.moving);
    }
    worldPairs.commit();
  }
'''
        if shared:
            code=code.replace('if(awakeCount>0){','if(!CB::baseline && awakeCount>0){')
        s=s[:end]+code+s[end:]
        before='{CB::StageScope q(4);broadPhaseNear(area,bpNearPhys);}';assert s.count(before)==1
        query=('{CB::StageScope q(4);if(CB::baseline)broadPhaseNear(area,bpNearPhys);else worldPairs.queryBox(area,bpNearPhys);}\n      if(!CB::baseline)WorldGridProbe::inspect(area,bpNearPhys,bpCache,runtimeObjects,Tyra::vu0SatVerify);' if shared else
            '{CB::StageScope q(4);worldPairs.queryBox(area,bpNearPhys);}\n      WorldGridProbe::inspect(area,bpNearPhys,bpCache,runtimeObjects,Tyra::vu0SatVerify);')
        s=s.replace(before,query,1)
        if shared:
            before='CB::baseline?8.0F:2.0F';assert s.count(before)==1;s=s.replace(before,f'{body_cell}.0F',1)
        else:
            before='  static Tyra::BroadPhase bodyPairs(Tyra::BroadPhase::kGrid,2.0F);';assert s.count(before)==1
            s=s.replace(before,f'  static Tyra::BroadPhase bodyPairs(Tyra::BroadPhase::kGrid,{body_cell}.0F);',1)
        candidate_path=p;candidate_text=s
        # Both arms use the same body index; isolate the world index change.
        p=root/'district/src/gen/collision_baseline.gen.cpp';s=p.read_text(encoding='utf8')
        if not shared:
            before='  static Tyra::BroadPhase bodyPairs;';assert s.count(before)==1
            s=s.replace(before,f'  static Tyra::BroadPhase bodyPairs(Tyra::BroadPhase::kGrid,{body_cell}.0F);',1)
        baseline_path=p;baseline_text=s
        p=engine/'inc/debug/collision_bench.hpp';s=p.read_text(encoding='utf8')
        s='#include "debug/world_grid_probe.hpp"\n'+s
        before='  used = 0;\n}';assert before in s
        s=s.replace(before,'  WorldGridProbe::flush(phase);\n'+before,1)
        # Validate all insertion points before mutating the generated fixture.
        candidate_path.write_bytes(candidate_text.encode());baseline_path.write_bytes(baseline_text.encode());p.write_bytes(s.encode())
    cfg=root/'district/bin/sat-verify.cfg'
    if verify:cfg.write_bytes(b'verify\n')
    elif cfg.exists():cfg.unlink()
    meta.update(world_grid_comparison=True,world_grid_verify=verify,body_grid_probe=False,
        body_grid_timing_cell=body_cell,world_body_cell=body_cell,
        shadow_enabled=verify,scene_timing_qualified=False,
        comparison=f'{body_cell}m body grid/canonical scalar in both arms; original combined world query versus world-only index, retaining nonphysics moving solids')
    meta_path.write_text(json.dumps(meta,indent=2),encoding='utf8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--body-cell',type=int,choices=[2,8],default=8,help='Identical body-grid cells in both arms; default8m retains production policy')
    p.add_argument('--verify',action='store_true');p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args()
    install(a.repo.resolve(),a.scratch.resolve(),a.verify,a.body_cell)
