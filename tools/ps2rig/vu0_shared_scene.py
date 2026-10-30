"""Compare legacy and shared VU0 packets in one pooled, world-first EE path."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile

def install(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    physics=root/'district/src/gen/game_physics.gen.cpp';baseline=root/'district/src/gen/collision_baseline.gen.cpp';metadata=root/'metadata.json'
    # The preceding gate stopped before all scene phases. Restore exactly its
    # backed-up runtime and metadata, retaining the qualified new micro image.
    with zipfile.ZipFile(root/'before-shared-probe.zip') as z:
        base=z.read('district/src/gen/collision_baseline.gen.cpp').decode('utf8')
        meta=json.loads(z.read('metadata.json').decode('utf8'))
    assert meta.get('step_pool_common_transform') and not meta.get('shared_scene_comparison')
    source=physics.read_text(encoding='utf8')
    with zipfile.ZipFile(root/'before-shared-scene.zip','x',zipfile.ZIP_DEFLATED) as z:
        for p in [physics,baseline,metadata]:z.write(p,p.relative_to(root))
    opening=source.index('void TerrainGame::updateObjectPhysicsCandidate() {')
    end=source.index('void TerrainGame::pushPhysicsBodies(',opening)
    body=source[opening:end]
    def replace(old,new):
        nonlocal body
        assert body.count(old)==1,old
        body=body.replace(old,new,1)
    replace('  if(CB::baseline){updateObjectPhysicsWide();return;}','  const bool useShared=!CB::baseline;')
    replace('  Tyra::Vu0SatBatch microBatch;\n  const bool useMicro = microBatch.begin(); // same backend in both schedules',
'''  Tyra::Vu0SatBatch microBatch;
  Tyra::Vu0SharedBatch sharedBatch;
  static std::vector<Tyra::Vu0SharedBody> sharedBodies;
  static std::vector<unsigned char> sharedReady;
  static Tyra::Vu0SharedPacket sharedPackets[2];
  if(useShared){sharedBodies.resize(bodies.size());sharedReady.assign(bodies.size(),0);sharedPackets[0].clear();sharedPackets[1].clear();}
  const bool useMicro=useShared?sharedBatch.begin():microBatch.begin();''')
    replace('  static PendingSat microPending[2][Tyra::Vu0SatBatch::Capacity];',
            '  static PendingSat microPending[2][Tyra::Vu0SharedPacket::MaxJobs];')
    replace('const Tyra::Vu0SatResult* results) {','const Tyra::Vu0SatResult* results,const Tyra::Vu0SharedResult* sharedResults) {')
    replace('bool hit=Tyra::finishVu0Sat(results[k],microJobs[completed][k],av,bv,rbSub(a.x1,a.x0),rbSub(b.x1,b.x0),c);',
'''bool hit=useShared?Tyra::finishVu0Shared(sharedResults[k],av,bv,a.obb,b.obb,rbSub(a.x1,a.x0),rbSub(b.x1,b.x0),c,true):
          Tyra::finishVu0Sat(results[k],microJobs[completed][k],av,bv,rbSub(a.x1,a.x0),rbSub(b.x1,b.x0),c);''')
    replace('      if(!comparison.accepted) {','''      if(!comparison.accepted) {
        // Diagnostics only, after a failure: never rebuild legacy jobs in timing.
        Tyra::Vu0SatJob captured;Tyra::Vu0SatResult converted;
        if(useShared){Tyra::packVu0Sat(captured,av,bv,a.obb,b.obb,rbSub(a.x1,a.x0),rbSub(b.x1,b.x0));converted=Tyra::legacySharedResult(sharedResults[k]);}
        const auto* diagnostic=useShared?&converted:&results[k];''')
    replace('microJobs[completed][k],results[k],hit,c,referenceHit,reference);',
            'useShared?captured:microJobs[completed][k],*diagnostic,hit,c,referenceHit,reference);')
    # All remaining result field reads are failure logging or axis history.
    log_start=body.index('        if(Tyra::vu0SatValidationErrors<8)printf(')
    log_end=body.index('        ++Tyra::vu0SatValidationErrors;',log_start)
    body=body[:log_start]+body[log_start:log_end].replace('results[k].','diagnostic->')+body[log_end:]
    replace('''} else if(results[k].flags[1]==0 && results[k].flags[2]==0) {
        const int index=int(results[k].indices[2]);
        const auto* v=microJobs[completed][k].q[10+index];
        const RbV axis={v[0],v[1],v[2]};''','''} else if(useShared?(sharedResults[k].interval[2]==0&&sharedResults[k].interval[3]==0):(results[k].flags[1]==0&&results[k].flags[2]==0)) {
        const int index=int(useShared?sharedResults[k].state[1]:results[k].indices[2]);
        RbV axis;
        if(useShared)axis=index<6?av.normals[index]:index<12?bv.normals[index-6]:Tyra::cvCross(av.edges[(index-12)/3],bv.edges[(index-12)%3]);
        else {const auto* v=microJobs[completed][k].q[10+index];axis={v[0],v[1],v[2]};}''')
    old='    Tyra::Vu0SatResult results[Tyra::Vu0SatBatch::Capacity];'
    assert body.count(old)==2
    body=body.replace(old,old+'\n    Tyra::Vu0SharedResult sharedResults[Tyra::Vu0SharedPacket::MaxJobs];')
    replace('    microBatch.finish(results);','    if(useShared)sharedBatch.finish(sharedResults);else microBatch.finish(results);')
    replace('    if(completed>=0)microBatch.finish(results);','    if(completed>=0){if(useShared)sharedBatch.finish(sharedResults);else microBatch.finish(results);}')
    assert body.count('consumeMicro(completed,completedCount,results);')==2
    body=body.replace('consumeMicro(completed,completedCount,results);','consumeMicro(completed,completedCount,results,sharedResults);')
    replace('    microBatch.start(microJobs[filling],queued);worldPacketActive=true;',
            '    if(useShared)sharedBatch.start(sharedPackets[filling]);else microBatch.start(microJobs[filling],queued);worldPacketActive=true;')
    replace('    if(completed>=0)consumeMicro(completed,completedCount,results,sharedResults);',
            '    if(completed>=0)consumeMicro(completed,completedCount,results,sharedResults);\n    if(useShared)sharedPackets[filling].clear();')
    replace('    if(useMicro)microBatch.observeOverlap();','    if(useMicro){if(useShared)sharedBatch.observeOverlap();else microBatch.observeOverlap();}')
    old='''        if(Tyra::packVu0Sat(microJobs[filling][queued],av,bv,A.obb,Bs.obb,ma,mb) &&
            !Tyra::vu0SatScalarPreferred(microJobs[filling][queued])) {'''
    new='''        bool packed=false;
        if(useShared){
          auto prepare=[&](int key,const Tyra::ConvexView& view,const Tyra::CollisionObb& box,RbV motion){
            if(!sharedReady[key])sharedReady[key]=Tyra::packVu0SharedBody(sharedBodies[key],view,box,motion)?1:2;
            return sharedReady[key]==1;
          };
          if(prepare(si,av,A.obb,ma)&&prepare(sj,bv,Bs.obb,mb)){
            const bool common=ma.x==mb.x&&ma.y==mb.y&&ma.z==mb.z;
            auto append=[&]{return sharedPackets[filling].append(si,&sharedBodies[si],sj,&sharedBodies[sj],common);};
            auto status=append();
            if(status==Tyra::Vu0SharedPacket::Append::Full){launchMicro();status=append();}
            packed=status==Tyra::Vu0SharedPacket::Append::Added;
          }
        } else packed=Tyra::packVu0Sat(microJobs[filling][queued],av,bv,A.obb,Bs.obb,ma,mb)&&!Tyra::vu0SatScalarPreferred(microJobs[filling][queued]);
        if(packed) {'''
    replace(old,new)
    replace('          if(queued==Tyra::Vu0SatBatch::Capacity)launchMicro();',
            '          if(queued==(useShared?Tyra::Vu0SharedPacket::MaxJobs:Tyra::Vu0SatBatch::Capacity))launchMicro();')
    source=source[:opening]+body+source[end:]
    assert source.count('#include "game_runtime.gen.hpp"')==1
    source=source.replace('#include "game_runtime.gen.hpp"','#include "debug/vu0_shared_batch.hpp"\n#include "game_runtime.gen.hpp"',1)
    physics.write_bytes(source.encode('utf8'))
    # Keep all existing legacy admission/replay gates and add the shared image
    # gate before scene execution. No timing is taken from this startup probe.
    base='#include "debug/vu0_shared_probe.hpp"\n'+base
    old='!TyraSolverProbe::run() || !Vu0SatProbe::run()';assert base.count(old)==1
    base=base.replace(old,'!Vu0SharedProbe::run() || '+old,1);baseline.write_bytes(base.encode('utf8'))
    for name in ['vu0_shared_packet.hpp','vu0_shared_batch.hpp','vu0_shared_probe.hpp']:
        shutil.copyfile(repo/'tools/ps2rig'/name,root/'engine/engine/inc/debug'/name)
    meta.update(shared_scene_comparison=True,shared_probe_only=False,step_pool_comparison=False,world_overlap_comparison=False,
        candidate='Shared 16-byte pair descriptors versus legacy512-byte VU0 jobs; same pooled EE path and world-first schedule',
        data_layout='Both arms use pooled312-byte EE steps and common compiled pose functions',
        shadow_enabled=True,world_grid_verify=True,world_overlap_verify=True,world_overlap_timing_probe=False,
        same_elf_admission_qualified=False,expected_phases=[12,13],active_mode='shared-scene-admission')
    tracked=[physics,baseline]+[root/'engine/engine/inc/debug'/n for n in ['vu0_shared_packet.hpp','vu0_shared_batch.hpp','vu0_shared_probe.hpp']]
    meta['lineage_pre_shared_scene_source_sha256']=meta.pop('step_pool_source_sha256')
    meta['shared_scene_source_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8')
    (root/'district/bin/sat-verify.cfg').write_bytes(b'verify\n')
    print('Shared/legacy scene comparison installed; same pooled EE path, world-first chained schedule')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',type=Path,required=True)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();install(a.repo.resolve(),a.scratch.resolve())
