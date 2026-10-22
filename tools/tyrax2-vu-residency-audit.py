"""Audit the selected private ring/paint resident set in a bound symbol ELF."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('--nm',type=Path,required=True)
    a=p.parse_args();r=a.fixture;b=json.loads((r/'native-binding.json').read_text())
    assert sha(r/'game/bin/vehicle-playground.elf')==b['elfSha256']
    for n,h in b['files'].items():assert sha(r/n)==h,n
    q=(r/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_text()
    assert 'enabled?(i<2?&StaPipVU1Clip_C_ring_hybrid_fold_CodeStart:' in q
    assert '&StaPipVU1Clip_TC_ring_hybrid_fold_paint_CodeStart):original[i][0]' in q
    flags=(r/'tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_vu1_experiments.hpp').read_text()
    for n in ('EE_LIGHT_FOLD','ENV_NORMALIZED','PAINT'):assert '#define TYRA_VU1_EXP_'+n+' 1' in flags
    text=subprocess.check_output([str(a.nm),str(r/'game/bin/vehicle-playground.elf.sym')],text=True)
    symbols={name:int(address,16) for address,name in re.findall(r'^([0-9a-fA-F]+)\s+\w\s+(\S+)$',text,re.M)}
    selected=['StaPipVU1Cull_C','StaPipVU1Cull_D_FOLD','StaPipVU1Cull_TD_FOLD',
      'StaPipVU1Cull_TC','StaPipVU1Cull_TCE_ENVN_PAINT',
      'StaPipVU1Clip_C_ring_hybrid_fold','StaPipVU1Clip_TC_ring_hybrid_fold_paint',
      'StaPipVU1Billboard_C','StaPipVU1Billboard_T']
    sizes={}
    for n in selected+['VU1DrawFinish']:
        size=symbols[n+'_CodeEnd']-symbols[n+'_CodeStart'];assert size>0 and size%8==0;sizes[n]=size//8
    resident=sum(sizes[n] for n in selected);finish=sizes['VU1DrawFinish'];assert resident+finish<=2048
    log=(r/'game/bin/log.txt').read_text(errors='replace')
    assert '12 programs, billboards resident' in log
    record=dict(valid=True,elfSha256=b['elfSha256'],symbolElfSha256=sha(r/'game/bin/vehicle-playground.elf.sym'),
      nativeBindingSha256=sha(r/'native-binding.json'),readerSha256=sha(Path(__file__)),
      logicalPrograms=12,uniqueResidentImages=len(selected),instructionWords=sizes,residentWords=resident,
      finishWords=finish,capacityWords=2048,freeWords=2048-resident-finish,
      limits=['Selected private ring/fold/paint image set, not all linked symbols or a production-default budget.',
              'Source selection plus linked lengths and resident runtime witness; no utilization or offload speed claim.'])
    (r/'vu-residency.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))


if __name__=='__main__':main()
