"""Install an alternate SAT kernel in a private fixture for same-ELF measurement."""
from pathlib import Path
import argparse,json,subprocess

def install(repo,root,baseline):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve()
    assert root.is_relative_to(allowed) and root!=allowed,'Use primary checkout .tmp/'
    meta_path=root/'metadata.json';meta=json.loads(meta_path.read_text(encoding='utf8'))
    assert meta.get('micro_stream'),'Requires an instrumented --micro-stream fixture'
    revision=subprocess.check_output(['git','-C',str(repo),'rev-parse','--verify',baseline+'^{commit}'],text=True).strip()
    def old(path):return subprocess.check_output(['git','-C',str(repo),'show',revision+':'+path]).decode('utf8')
    h=old('vendor/tyra/engine/inc/physics/vu0_sat.hpp')
    assert 'float q[32][4]' in h and 'Capacity = 7;' in h,'Reference packet layout must match'
    engine=root/'engine/engine'
    kernel=old('vendor/tyra/engine/src/physics/vu0_sat_kernel.vclpp').replace('Vu0SatKernel','Vu0SatReferenceKernel')
    (engine/'src/physics/vu0_sat_reference_kernel.vclpp').write_bytes(kernel.encode())
    p=engine/'inc/physics/vu0_sat.hpp';h=p.read_text(encoding='utf8')
    assert 'TYRA_VU0_SAT_KERNEL_REFERENCE' not in h,'Reference already installed'
    h+='\n#define TYRA_VU0_SAT_KERNEL_REFERENCE 1\nnamespace Tyra {inline bool vu0SatProbeReference=false;}\n'
    p.write_bytes(h.encode())
    p=engine/'src/physics/vu0_sat.cpp';s=p.read_text(encoding='utf8')
    s='extern unsigned Vu0SatReferenceKernel_CodeStart;\nextern unsigned Vu0SatReferenceKernel_CodeEnd;\n'+s
    before='''  if(vu0MicroOwner!=&Vu0SatKernel_CodeStart) {
    const auto* first=&Vu0SatKernel_CodeStart;
    const unsigned words=&Vu0SatKernel_CodeEnd-first;'''
    after='''  const auto* first=vu0SatProbeReference?&Vu0SatReferenceKernel_CodeStart:&Vu0SatKernel_CodeStart;
  const auto* last=vu0SatProbeReference?&Vu0SatReferenceKernel_CodeEnd:&Vu0SatKernel_CodeEnd;
  if(vu0MicroOwner!=first) {
    const unsigned words=last-first;'''
    assert before in s;s=s.replace(before,after,1);p.write_bytes(s.encode())
    meta['micro_kernel_baseline']=revision
    meta_path.write_text(json.dumps(meta,indent=2),encoding='utf8')
    print('Private reference kernel installed:',revision)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--scratch',type=Path,required=True);p.add_argument('--baseline',required=True)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2])
    a=p.parse_args();install(a.repo.resolve(),a.scratch.resolve(),a.baseline)
