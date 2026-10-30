"""Attribute EE work around chained VU0 jobs without changing collision coverage."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile


def install(repo,root):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve();assert root.is_relative_to(allowed) and root!=allowed
    metadata=root/'metadata.json';meta=json.loads(metadata.read_text(encoding='utf8'))
    assert meta.get('world_overlap_comparison') and not meta.get('micro_cost_profile')
    p=root/'district/src/gen/game_physics.gen.cpp';b=root/'district/src/gen/collision_baseline.gen.cpp'
    with zipfile.ZipFile(root/'before-micro-cost.zip','x',zipfile.ZIP_DEFLATED) as z:
        for f in [p,b,metadata]:z.write(f,f.relative_to(root))
    s=p.read_text(encoding='utf8')
    def replace(old,new):
        nonlocal s
        assert s.count(old)==1,old;s=s.replace(old,new,1)
    replace('#include "debug/world_overlap_probe.hpp"','#include "debug/world_overlap_probe.hpp"\n#include "debug/micro_cost_probe.hpp"')
    replace('  const bool overlapWorld=!CB::baseline;','  const bool overlapWorld=false; // profile the existing world-first chained schedule')
    replace('    const int m = rbReduce(cs, n, red); CB::generated += m;',
            '    MicroCost::Scope emitCost(MicroCost::Emit);\n    const int m = rbReduce(cs, n, red); CB::generated += m;')
    replace('    for(int k=0;k<completedCount;++k) {','    MicroCost::Scope consumeCost(MicroCost::Consume);\n    for(int k=0;k<completedCount;++k) {')
    replace('Tyra::finishVu0Sat(results[k],','MicroCost::finish(results[k],')
    # Two finish sites: final drain and chained handoff. Preserve both fences.
    assert s.count('microBatch.finish(results);')==2
    s=s.replace('microBatch.finish(results);','MicroCost::call(MicroCost::Finish,[&]{microBatch.finish(results);});')
    replace('microBatch.start(microJobs[filling],queued);',
            'MicroCost::call(MicroCost::Upload,[&]{microBatch.start(microJobs[filling],queued);});')
    replace('      const float d2 = rbDot(d, d);\n      int n = 0;',
            '      const float d2 = rbDot(d, d);\n      int n = 0;\n      { MicroCost::Scope cornerCost(MicroCost::Corners);')
    replace('#if TYRA_PHYSICS_MICRO\n      if (!n && useMicro && coherence && A.box && Bs.box) {',
            '      } // corner/sphere contacts, before any packet preparation\n#if TYRA_PHYSICS_MICRO\n      if (!n && useMicro && coherence && A.box && Bs.box) {\n        const unsigned prepareStart=MicroCost::begin();')
    replace('if(Tyra::convexSeparatedOnAxis(av,bv,ma,mb,hint,&A.obb,&Bs.obb))continue;',
            'if(Tyra::convexSeparatedOnAxis(av,bv,ma,mb,hint,&A.obb,&Bs.obb)){MicroCost::end(MicroCost::Prepare,prepareStart);continue;}\n        MicroCost::end(MicroCost::Prepare,prepareStart);')
    replace('Tyra::packVu0Sat(microJobs[filling][queued],','MicroCost::pack(microJobs[filling][queued],')
    physics=s;s=b.read_text(encoding='utf8')
    replace('#include "debug/world_overlap_probe.hpp"','#include "debug/world_overlap_probe.hpp"\n#include "debug/micro_cost_probe.hpp"')
    replace('checked=true;FILE* verify=', 'checked=true;MicroCost::configure();FILE* verify=')
    replace('if(Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)CB::phase=12;else CB::phase=10;', 'CB::phase=12;')
    replace('WorldOverlapProbe::flush(CB::phase);','WorldOverlapProbe::flush(CB::phase);MicroCost::flush(CB::phase);')
    replace('if(!Tyra::vu0SatVerify && CB::phase==14)CB::phase=24;','')
    replace('CB::phase>=((Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)?14:CB::phases)','CB::phase>=14')
    p.write_bytes(physics.encode());b.write_bytes(s.encode())
    shutil.copyfile(repo/'tools/ps2rig/micro_cost_probe.hpp',root/'engine/engine/inc/debug/micro_cost_probe.hpp')
    (root/'district/bin/micro-cost.cfg').write_bytes(b'profile\n')
    (root/'district/bin/sat-verify.cfg').write_bytes(b'verify\n')
    if 'world_overlap_source_sha256' in meta:meta['lineage_world_overlap_source_sha256']=meta.pop('world_overlap_source_sha256')
    meta['micro_cost_source_sha256']={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [p,b,root/'engine/engine/inc/debug/micro_cost_probe.hpp']}
    meta.update(micro_cost_profile=True,world_overlap_comparison=False,shadow_enabled=True,world_grid_verify=True,
        world_overlap_verify=True,same_elf_admission_qualified=False,expected_phases=[12,13],
        active_mode='micro-cost-verify',candidate='Private EE attribution around the world-first chained micro backend',
        comparison='Both arms same world-first chained micro schedule. Profiling scopes overlap: consume contains reconstruct/emit; finish includes wait/readback. No performance ranking from this instrumentation.')
    metadata.write_text(json.dumps(meta,indent=2),encoding='utf8')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();install(a.repo.resolve(),a.scratch.resolve())
