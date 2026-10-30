"""Install deferred failure capture and optional precision stress in a private fixture."""
from pathlib import Path
import argparse,json,shutil,subprocess

def install(repo,root,precision=False):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve()
    assert root.is_relative_to(allowed) and root!=allowed
    meta_path=root/'metadata.json';meta=json.loads(meta_path.read_text(encoding='utf8'))
    assert meta.get('micro_comparison')
    engine=root/'engine/engine';rig=repo/'tools/ps2rig'
    for name in ['vu0_sat_capture.hpp','vu0_sat_precision.hpp']:shutil.copyfile(rig/name,engine/'inc/debug'/name)
    p=root/'district/src/gen/game_physics.gen.cpp';s=p.read_text(encoding='utf8')
    # Fixture feature defines must precede physics headers and their defaults.
    s=s.replace('#include "debug/vu0_sat_capture.hpp"\n','')
    include='#include "game_runtime.gen.hpp"'
    assert include in s
    s=s.replace(include,'#include "debug/vu0_sat_capture.hpp"\n'+include,1)
    if 'Vu0SatCapture::remember' not in s:
        job='microJobs[completed][k]' if 'microJobs[completed][k]' in s else 'microJobs[inflight][k]'
        before='      if(!comparison.accepted) {'
        assert before in s and job in s
        s=s.replace(before,before+'\n        Vu0SatCapture::remember(a.obj,b.obj,av,bv,'+job+',results[k],hit,c,referenceHit,reference);',1)
        before='  // --- 4. sequential impulses'
        assert before in s;s=s.replace(before,'  Vu0SatCapture::flush(); // all micro packets are drained here\n\n'+before,1)
    p.write_bytes(s.encode())
    p=engine/'inc/debug/collision_bench.hpp';text=p.read_text(encoding='utf8')
    if 'VUSATCLASS' not in text:
        before='  used = 0;\n}'
        assert before in text
        code='  if(FILE* cls=fopen(("vu0-class-"+std::to_string(phase)+".csv").c_str(),"wb")) {\n    const auto& v=Tyra::vu0SatMetrics;\n    fprintf(cls,"phase,jobs,proved_negative,confirmed_negative,confirmed_ccd,confirmed_tie,confirmed_parallel\\n%d,%u,%u,%u,%u,%u,%u\\n",phase,v.jobs,v.provedNegative,v.confirmedNegative,v.confirmedCcd,v.confirmedTie,v.confirmedParallel);fclose(cls);\n    printf("VUSATCLASS phase=%d proved=%u negative_full=%u ccd=%u tie=%u parallel=%u\\n",phase,v.provedNegative,v.confirmedNegative,v.confirmedCcd,v.confirmedTie,v.confirmedParallel);fflush(stdout);\n  }\n'
        code=code.replace('confirmed_parallel\\n%d,%u,%u,%u,%u,%u,%u\\n','confirmed_parallel,confirmed_positive\\n%d,%u,%u,%u,%u,%u,%u,%u\\n')
        code=code.replace('v.confirmedParallel);fclose(cls);','v.confirmedParallel,v.confirmedPositive);fclose(cls);')
        text=text.replace(before,code+before,1);p.write_bytes(text.encode())
    if precision:
        p=root/'district/src/gen/collision_baseline.gen.cpp';s=p.read_text(encoding='utf8')
        if '!Vu0SatPrecision::run()' not in s:
            assert '!Vu0SatStream::run()' in s
            s='#include "debug/vu0_sat_precision.hpp"\n'+s
            s=s.replace('!Vu0SatStream::run()','!Vu0SatPrecision::run() || !Vu0SatStream::run()',1)
            p.write_bytes(s.encode())
    meta.update(micro_capture=True,micro_precision=precision or meta.get('micro_precision',False))
    meta_path.write_text(json.dumps(meta,indent=2),encoding='utf8')
    print('Deferred capture installed; precision stress:',meta['micro_precision'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',required=True,type=Path)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--precision',action='store_true')
    a=p.parse_args();install(a.repo.resolve(),a.scratch.resolve(),a.precision)
