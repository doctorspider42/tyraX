"""Sample current EE/VU0 pipeline and solver costs without changing solve order."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile

def install(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    metadata=root/'metadata.json';meta=json.loads(metadata.read_text(encoding='utf8'))
    assert meta.get('shared_scene_comparison') and not meta.get('pipeline_cost_profile')
    p=root/'district/src/gen/game_physics.gen.cpp';b=root/'district/src/gen/collision_baseline.gen.cpp'
    with zipfile.ZipFile(root/'before-pipeline-cost.zip','x',zipfile.ZIP_DEFLATED) as z:
        for path in [p,b,metadata]:z.write(path,path.relative_to(root))
    source=p.read_text(encoding='utf8');start=source.index('void TerrainGame::updateObjectPhysicsCandidate() {');stop=source.index('void TerrainGame::pushPhysicsBodies(',start)
    body=source[start:stop]
    def replace(old,new):
        nonlocal body
        assert body.count(old)==1,old;body=body.replace(old,new,1)
    replace('  const bool useShared=!CB::baseline;','  PipelineCost::Frame costFrame;\n  const bool useShared=!CB::baseline;')
    replace('    const int m = rbReduce(cs, n, red); CB::generated += m;',
            '    PipelineCost::Scope emitCost(PipelineCost::Emit);\n    const int m = rbReduce(cs, n, red); CB::generated += m;')
    replace('    for(int k=0;k<completedCount;++k) {','    PipelineCost::Scope consumeCost(PipelineCost::Consume);\n    for(int k=0;k<completedCount;++k) {')
    replace('      bool hit=useShared?', '      const unsigned reconstructStart=PipelineCost::begin();\n      bool hit=useShared?')
    replace('#if TYRA_PHYSICS_MICRO_VERIFY\n      if(Tyra::vu0SatVerify)', '      PipelineCost::end(PipelineCost::Reconstruct,reconstructStart);\n#if TYRA_PHYSICS_MICRO_VERIFY\n      if(Tyra::vu0SatVerify)')
    assert body.count('sharedBatch.finish(sharedResults);')==2 and body.count('microBatch.finish(results);')==2
    for call in ['sharedBatch.finish(sharedResults);','microBatch.finish(results);']:
        body=body.replace(call,'PipelineCost::call(PipelineCost::Finish,[&]{'+call+'});')
    for call in ['sharedBatch.start(sharedPackets[filling]);','microBatch.start(microJobs[filling],queued);']:
        replace(call,'PipelineCost::call(PipelineCost::Upload,[&]{'+call+'});')
    replace('      int n = 0;\n      if (A.sphere', '      int n = 0;\n      { PipelineCost::Scope cornerCost(PipelineCost::Corners);\n      if (A.sphere')
    replace('#if TYRA_PHYSICS_MICRO\n      if (!n && useMicro && coherence && A.box && Bs.box)',
            '      } // sphere/corner tests complete\n#if TYRA_PHYSICS_MICRO\n      if (!n && useMicro && coherence && A.box && Bs.box)')
    replace('        const auto& ga=transformedShape(A,Ba);const auto& gb=transformedShape(Bs,Bj);',
            '        const unsigned prepareStart=PipelineCost::begin();\n        const auto& ga=transformedShape(A,Ba);const auto& gb=transformedShape(Bs,Bj);')
    replace('if(Tyra::convexSeparatedOnAxis(av,bv,ma,mb,hint,&A.obb,&Bs.obb))continue;',
            'if(Tyra::convexSeparatedOnAxis(av,bv,ma,mb,hint,&A.obb,&Bs.obb)){PipelineCost::end(PipelineCost::Prepare,prepareStart);continue;}\n        PipelineCost::end(PipelineCost::Prepare,prepareStart);')
    replace('Tyra::packVu0SharedBody(sharedBodies[key],view,box,motion)',
            'PipelineCost::call(PipelineCost::SharedPack,[&]{return Tyra::packVu0SharedBody(sharedBodies[key],view,box,motion);})')
    replace('return sharedPackets[filling].append(si,&sharedBodies[si],sj,&sharedBodies[sj],common);',
            'return PipelineCost::call(PipelineCost::Append,[&]{return sharedPackets[filling].append(si,&sharedBodies[si],sj,&sharedBodies[sj],common);});')
    replace('Tyra::packVu0Sat(microJobs[filling][queued],av,bv,A.obb,Bs.obb,ma,mb)',
            'PipelineCost::call(PipelineCost::LegacyPack,[&]{return Tyra::packVu0Sat(microJobs[filling][queued],av,bv,A.obb,Bs.obb,ma,mb);})')
    # Coarse solver intervals preserve the original sequential dependency chain.
    replace('#else\n  for (int k = 0; k < nc; ++k) {', '#else\n  { PipelineCost::Scope prepareCost(PipelineCost::SolverPrepare);\n  for (int k = 0; k < nc; ++k) {')
    replace('  auto applyImpulse = [&](RbContact& c, RbV P) {','  }\n  auto applyImpulse = [&](RbContact& c, RbV P) {')
    replace('  if(warmStart)for(auto& c:contacts)applyImpulse(c,rbAdd(rbMul(c.n,c.jn),c.ft));',
            '  { PipelineCost::Scope warmCost(PipelineCost::SolverWarm);\n  if(warmStart)for(auto& c:contacts)applyImpulse(c,rbAdd(rbMul(c.n,c.jn),c.ft));\n  }\n  { PipelineCost::Scope velocityCost(PipelineCost::Velocity);')
    replace('      RbV vrel = relVel(c);','      const unsigned normalStart=PipelineCost::begin();\n      RbV vrel = relVel(c);')
    replace('      // friction, against whatever tangential slip is left',
            '      PipelineCost::end(PipelineCost::Normal,normalStart);\n      PipelineCost::Scope frictionCost(PipelineCost::Friction);\n      // friction, against whatever tangential slip is left')
    replace('  // Split impulse: the overlap still left after the velocity solve is',
            '  }\n  { PipelineCost::Scope positionCost(PipelineCost::Position);\n  // Split impulse: the overlap still left after the velocity solve is')
    replace('#endif\n  CB::warmHits=', '  }\n#endif\n  { PipelineCost::Scope historyCost(PipelineCost::History);\n  CB::warmHits=')
    replace('  // A component sleeps together only after every member has met its authored',
            '  }\n  // A component sleeps together only after every member has met its authored')
    source=source[:start]+body+source[stop:]
    marker='#include "debug/world_overlap_probe.hpp"';assert source.count(marker)==1
    source=source.replace(marker,marker+'\n#include "debug/pipeline_cost_probe.hpp"',1);p.write_bytes(source.encode('utf8'))
    s=b.read_text(encoding='utf8');assert s.count(marker)==1;s=s.replace(marker,marker+'\n#include "debug/pipeline_cost_probe.hpp"',1)
    old='checked=true;FILE* verify=';assert s.count(old)==1;s=s.replace(old,'checked=true;PipelineCost::configure();FILE* verify=',1)
    old='if(Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)CB::phase=12;else CB::phase=10;';assert s.count(old)==1;s=s.replace(old,'CB::phase=12;',1)
    old='StepPoolProbe::flush(CB::phase);';assert s.count(old)==1;s=s.replace(old,old+'PipelineCost::flush(CB::phase);',1)
    old='if(!Tyra::vu0SatVerify && CB::phase==14)CB::phase=24;';assert s.count(old)==1;s=s.replace(old,'',1)
    old='CB::phase>=((Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)?14:CB::phases)';assert s.count(old)==1;s=s.replace(old,'CB::phase>=14',1)
    b.write_bytes(s.encode('utf8'))
    helper=root/'engine/engine/inc/debug/pipeline_cost_probe.hpp';shutil.copyfile(repo/'tools/ps2rig/pipeline_cost_probe.hpp',helper)
    meta['lineage_pre_pipeline_source_sha256']=meta.pop('shared_scene_source_sha256')
    meta['pipeline_cost_source_sha256']={str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in [p,b,helper]}
    meta.update(pipeline_cost_profile=True,pipeline_cost_enabled=True,pipeline_sample_stride=16,shadow_enabled=True,
        world_grid_verify=True,world_overlap_verify=True,same_elf_admission_qualified=False,expected_phases=[12,13],
        active_mode='pipeline-cost-admission',performance_qualified=False)
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8')
    (root/'district/bin/pipeline-cost.cfg').write_bytes(b'profile\n');(root/'district/bin/sat-verify.cfg').write_bytes(b'verify\n')
    print('Installed sampled current-pipeline attribution: original6+3 solve order retained')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',type=Path,required=True)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();install(a.repo.resolve(),a.scratch.resolve())
