"""Install a gate-only shared-body VU0 probe in the existing private rig."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,zipfile

def install(repo,root,sony):
    common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
    allowed=(common.parent/'.tmp').resolve()
    assert root.is_relative_to(allowed) and root!=allowed,'Use primary checkout .tmp/'
    raw=sony.read_bytes();assert raw and len(raw)%8==0 and len(raw)<=4096
    source=root/'district/src/gen/collision_baseline.gen.cpp';meta=root/'metadata.json'
    s=source.read_text(encoding='utf8');assert 'Vu0SharedProbe::run' not in s
    marker=' static bool checked=false;if(!checked)'
    assert s.count(marker)==1
    with zipfile.ZipFile(root/'before-shared-probe.zip','x',zipfile.ZIP_DEFLATED) as z:
        z.write(source,source.relative_to(root));z.write(meta,meta.relative_to(root))
    s='#include "debug/vu0_shared_probe.hpp"\n'+s.replace(marker,
        ' static bool sharedChecked=false;if(!sharedChecked){sharedChecked=true;const bool ok=Vu0SharedProbe::run();printf("VUSHARED DONE accepted=%d\\n",ok);fflush(stdout);CB::finished=true;return;}'+marker,1)
    source.write_bytes(s.encode('utf8'))
    debug=root/'engine/engine/inc/debug';debug.mkdir(parents=True,exist_ok=True)
    for name in ['vu0_shared_packet.hpp','vu0_shared_probe.hpp']:
        (debug/name).write_bytes((repo/'tools/ps2rig'/name).read_bytes())
    words=struct.unpack('<'+'I'*(len(raw)//4),raw)
    (debug/'vu0_shared_sony.hpp').write_bytes(('// Generated private Sony image; SHA256 '+hashlib.sha256(raw).hexdigest()+'\n#pragma once\nalignas(16) static const unsigned SharedSonyCode[]={\n'+','.join(hex(w)+'u' for w in words)+'\n};\n').encode())
    kernel=repo/'tools/ps2rig/vu0_shared_separation.vclpp'
    (root/'engine/engine/src/physics/vu0_shared_separation.vclpp').write_bytes(kernel.read_bytes())
    metadata=json.loads(meta.read_text(encoding='utf8'))
    metadata.update(shared_probe_only=True,shared_sony_sha256=hashlib.sha256(raw).hexdigest(),shared_vcl_sha256=hashlib.sha256(kernel.read_bytes()).hexdigest(),same_elf_admission_qualified=False)
    meta.write_text(json.dumps(metadata,indent=2),encoding='utf8')
    print('Installed gate-only shared VU0 probe; previous timing metadata is not valid for this new ELF')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scratch',type=Path,required=True);p.add_argument('--sony-binary',type=Path,required=True)
    p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args()
    install(a.repo.resolve(),a.scratch.resolve(),a.sony_binary.resolve())
