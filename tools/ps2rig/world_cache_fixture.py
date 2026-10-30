"""Install the canonical cached-static/compact-moving world index in a private rig."""
from pathlib import Path
import argparse,json,shutil,subprocess
from codegen_member import extract

def install(repo,root,verify):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    p=root/'metadata.json';meta=json.loads(p.read_text(encoding='utf8'))
    assert meta.get('world_grid_comparison') and meta.get('shared_physics') and meta.get('world_body_cell')==8
    engine=root/'engine/engine';project=root/'district'
    for rel in ['inc/tyra','inc/physics/world_broad_phase.hpp','src/physics/world_broad_phase.cpp']:
        shutil.copyfile(repo/'vendor/tyra/engine'/rel,engine/rel)
    if not meta.get('world_cache'):
        canonical=(repo/'src/game_templates.inc').read_text(encoding='utf8')
        p=project/'inc/terrain_game.hpp';s=p.read_text(encoding='utf8')
        before='  void broadPhaseNear(const Tyra::BroadBox& area, std::vector<int>& out) const;';assert s.count(before)==1
        s=s.replace(before,before+'\n  void broadPhaseWorldNear(const Tyra::BroadBox& area, std::vector<int>& out) const;',1)
        before='  mutable Tyra::BroadPhase broadPhase;';assert s.count(before)==1
        s=s.replace(before,before+'\n  mutable Tyra::WorldBroadPhase worldBroadPhase;',1);header_path=p;header_text=s
        p=project/'src/gen/game_collision.gen.cpp';s=p.read_text(encoding='utf8')
        old=extract(s,'broadPhaseRefreshCandidate')
        new=extract(canonical,'broadPhaseRefresh').replace('TerrainGame::broadPhaseRefresh(', 'TerrainGame::broadPhaseRefreshCandidate(',1)
        s=s.replace(old,new,1)
        method=extract(canonical,'broadPhaseWorldNear')
        before='  worldBroadPhase.queryBox(area, out);';assert method.count(before)==1
        method=method.replace(before,'''  const unsigned before = worldBroadPhase.staticBuildCount()+worldBroadPhase.movingBuildCount();
'''+before+'''
  WorldGridProbe::builds += worldBroadPhase.staticBuildCount()+worldBroadPhase.movingBuildCount()-before;''',1)
        s+='\nnamespace Vehicle_playground {\n'+method+'\n}\n';collision_path=p;collision_text=s
        p=project/'src/gen/game_physics.gen.cpp';s=p.read_text(encoding='utf8')
        start=s.index('  // Reuse the general collider cache; retain scripted moving world solids.')
        end=s.index('  for (int si = 0; si < awakeCount; ++si)',start)
        s=s[:start]+s[end:]
        before=('if(CB::baseline || awakeCount<16)broadPhaseNear(area,bpNearPhys);else worldPairs.queryBox(area,bpNearPhys);'
            if meta.get('world_index_min_awake') else 'if(CB::baseline)broadPhaseNear(area,bpNearPhys);else worldPairs.queryBox(area,bpNearPhys);')
        assert s.count(before)==1
        s=s.replace(before,'if(CB::baseline)broadPhaseNear(area,bpNearPhys);else broadPhaseWorldNear(area,bpNearPhys);',1)
        s=s.replace(',Tyra::vu0SatVerify,awakeCount>=16);',',Tyra::vu0SatVerify);')
        header_path.write_bytes(header_text.encode());collision_path.write_bytes(collision_text.encode());p.write_bytes(s.encode())
    cfg=project/'bin/sat-verify.cfg'
    if verify:cfg.write_bytes(b'verify\n')
    elif cfg.exists():cfg.unlink()
    meta.pop('world_index_min_awake',None)
    meta.update(world_cache=True,world_grid_verify=verify,shadow_enabled=verify,same_elf_admission_qualified=False,
        comparison='Shared scalar8m body grid: combined world query vs cached static world index and compact current movers; no active-body threshold')
    (root/'metadata.json').write_text(json.dumps(meta,indent=2),encoding='utf8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--verify',action='store_true');p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args()
    install(a.repo.resolve(),a.scratch.resolve(),a.verify)
