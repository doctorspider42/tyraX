"""Install a private shared vehicle/object timeline after the zero-friction rig."""
from pathlib import Path
import argparse, hashlib, json, re, shutil, subprocess, zipfile
from codegen_member import extract


def install(repo, root, refresh=False):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve()
    assert root.is_relative_to(allowed) and root!=allowed
    project=root/'district'; engine=root/'engine/engine'; rig=repo/'tools/ps2rig'
    metadata=root/'metadata.json'
    paths=[project/'src/terrain_game.cpp',project/'inc/terrain_game.hpp',project/'src/gen/game_physics.gen.cpp',
           project/'src/gen/game_vehicles.gen.cpp',project/'src/gen/collision_baseline.gen.cpp',metadata]
    backup=root/'before-unified-timeline.zip'
    if refresh:
        assert json.loads(metadata.read_text(encoding='utf8')).get('unified_timeline_comparison')
        with zipfile.ZipFile(backup) as z:
            saved={path:z.read(path.relative_to(root).as_posix()) for path in paths}
        for path,data in saved.items():path.write_bytes(data)
    meta=json.loads(metadata.read_text(encoding='utf8'))
    assert meta.get('zero_friction_comparison') and not meta.get('unified_timeline_comparison')
    if not refresh:
        with zipfile.ZipFile(backup,'x',zipfile.ZIP_DEFLATED) as z:
            for path in paths:z.write(path,path.relative_to(root))
    header=project/'inc/terrain_game.hpp';s=header.read_text(encoding='utf8')
    token='  void benchTick();';assert s.count(token)==1
    header.write_text(s.replace(token,token+'\n  void stepUnifiedTimeline(float dt);'),encoding='utf8',newline='\n')
    for path in [project/'src/terrain_game.cpp',project/'src/gen/game_physics.gen.cpp',project/'src/gen/game_vehicles.gen.cpp',project/'src/gen/collision_baseline.gen.cpp']:
        s=path.read_text(encoding='utf8');token='#include "game_runtime.gen.hpp"';assert s.count(token)==1
        s=s.replace(token,token+'\n#include "debug/unified_timeline_probe.hpp"',1)
        if path.name=='game_physics.gen.cpp':
            token='const volatile bool skipZeroFriction=CB::ready&&!CB::baseline;';assert s.count(token)==1
            s=s.replace(token,'const volatile bool skipZeroFriction=CB::ready;',1)
            body=extract(s,'updateObjectPhysicsCandidate');new=body
            for name in ['PHYS_REST_SPEED2','PHYS_REST_W2','PHYS_WAKE_SPEED2','PHYS_MAX_SPEED','PHYS_MAX_SPIN']:
                fn='threshold2' if name.endswith('2') else 'threshold'
                new=re.sub(r'\b'+name+r'\b',f'UnifiedTimeline::{fn}({name})',new)
            token='3.0F * gravityPerFrame + 0.004F';assert new.count(token)==1
            new=new.replace(token,'3.0F * gravityPerFrame + UnifiedTimeline::threshold(0.004F)',1)
            s=s.replace(body,new,1)
            body=extract(s,'physPushAt');new=body.replace('PHYS_MAX_SPIN','UnifiedTimeline::threshold(PHYS_MAX_SPIN)')
            s=s.replace(body,new,1)
        if path.name=='game_vehicles.gen.cpp':
            a=s.index('    // PHYSICS BODIES:');b=s.index('    VEH_LAP(4);',a)
            body=s[a:b]
            for old,new in [('mvL * 1.25F + 0.015F','mvL * 1.25F + UnifiedTimeline::threshold(0.015F)'),
                            ('mvL > 0.05F','mvL > UnifiedTimeline::threshold(0.05F)'),
                            ('o.velocityY < 0.02F','o.velocityY < UnifiedTimeline::threshold(0.02F)'),
                            ('(0.025F + 0.15F * mvL)','(UnifiedTimeline::threshold(0.025F) + 0.15F * mvL)')]:
                assert body.count(old)==1,old;body=body.replace(old,new,1)
            s=s[:a]+body+s[b:]
            pos=s.index('void TerrainGame::stepVehicles(')
            s=s[:pos]+(rig/'unified_timeline_body.inc').read_text(encoding='utf8')+'\n'+s[pos:]
        if path.name=='collision_baseline.gen.cpp':
            token='static bool checked=false;if(!checked){checked=true;'
            assert s.count(token)==1;s=s.replace(token,token+'UnifiedTimeline::configure();',1)
            token='ZeroFrictionProbe::flush(CB::phase);';assert s.count(token)==1
            s=s.replace(token,token+'UnifiedTimeline::flush(CB::phase);',1)
        if path.name=='terrain_game.cpp':
            token='  const bool menuActive =';assert s.count(token)==1
            s=s.replace(token,'  g_frameDt=UnifiedTimeline::multiplier/g_frameRate;g_frameScale=g_frameDt*50.0F;\n'+token,1)
            a=s.index('#if TYRA_FRAME_PROFILE\n',s.index('// Broad-phase refresh boundary'))
            b=s.index('  else muteVehicleEngines();',a)
            s=s[:a]+'''  if (!menuActive) {
    stepUnifiedTimeline(g_frameScale * (1.0F / 50.0F));
    updateVehicleSmoke(g_frameScale * (1.0F / 50.0F));
    updateVehicleSkids(g_frameScale * (1.0F / 50.0F));
    updateVehicleDebris(g_frameScale * (1.0F / 50.0F));
  }
'''+s[b:]
        path.write_text(s,encoding='utf8',newline='\n')
    helper=engine/'inc/debug/unified_timeline_probe.hpp';shutil.copyfile(rig/helper.name,helper)
    meta.update(unified_timeline_comparison=True,zero_friction_comparison=False,shared_scene_comparison=True,
        candidate='Shared object/vehicle substeps with frame-displacement adapters; legacy detectors/response',
        shadow_enabled=True,world_grid_verify=True,world_overlap_verify=True,same_elf_admission_qualified=False,
        expected_phases=[12,13],active_mode='unified-timeline-nominal-admission',timeline_multiplier=1)
    meta['unified_timeline_source_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths[:-1]+[helper]}
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8')
    (project/'bin/sat-verify.cfg').write_bytes(b'verify\n')
    (project/'bin/unified-step.cfg').write_bytes(b'1\n')
    print('Installed shared timeline; both arms use shared packets and the admitted friction guard')


def configure(root,mode):
    path=root/'metadata.json';meta=json.loads(path.read_text(encoding='utf8'))
    assert meta.get('unified_timeline_comparison')
    multiplier=2 if mode.startswith('double') else 1;shadow=mode.endswith('admission')
    meta.update(timeline_multiplier=multiplier,shadow_enabled=shadow,world_grid_verify=shadow,
                world_overlap_verify=shadow,active_mode='unified-timeline-'+mode,
                expected_phases=[12,13] if shadow else [10,11,12,13,24,25,26,27])
    meta['elf_sha256']=hashlib.sha256((root/'district/bin/vehicle-playground.elf').read_bytes()).hexdigest()
    path.write_text(json.dumps(meta,indent=2),encoding='utf8')
    (root/'district/bin/unified-step.cfg').write_bytes((str(multiplier)+'\n').encode('ascii'))
    verify=root/'district/bin/sat-verify.cfg'
    if shadow:verify.write_bytes(b'verify\n')
    elif verify.exists():verify.unlink()
    print(mode,meta['elf_sha256'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--refresh',action='store_true',help='Reinstall from the exact saved pre-experiment source')
    p.add_argument('--mode',choices=['nominal-admission','nominal-timing','double-admission','double-timing'])
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args()
    if a.mode:
        assert not a.refresh;configure(a.scratch.resolve(),a.mode)
    else:install(a.repo.resolve(),a.scratch.resolve(),a.refresh)
