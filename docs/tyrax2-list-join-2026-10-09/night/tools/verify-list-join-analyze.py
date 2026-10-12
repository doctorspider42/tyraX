"""Negative controls for A19, using disposable copies of bound evidence."""
import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path)
p.add_argument('output',type=Path)
a=p.parse_args()
tool=Path(__file__).with_name('tyrax2-list-join-analyze.py')
b=json.loads((a.fixture/'native-binding.json').read_text())
cases=[]
with tempfile.TemporaryDirectory(prefix='a19-reader-') as tmp:
    root=Path(tmp)
    files=list(b['files'])+['native-binding.json','list-join-manifest.json',
        'emulator/launch.json','emulator/emulator.log','game/bin/vehicle-playground.elf']
    for pattern in ('command-*.bin','tape-*.bin','three-path.cfg'):
        files += [str(f.relative_to(a.fixture)) for f in (a.fixture/'game/bin').glob(pattern)]
    for rel in files:
        dst=root/rel;dst.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(a.fixture/rel,dst)
    def run():
        return subprocess.run([sys.executable,str(tool),str(root)],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode
    assert run()==0
    def reject(name,rel,change):
        path=root/rel;original=path.read_bytes();changed=change(original)
        assert changed!=original
        path.write_bytes(changed)
        try:
            assert run()!=0,name
            cases.append(name)
        finally:
            path.write_bytes(original)
    log='emulator/emulator.log'
    reject('missing join stage',log,lambda x:x.replace(b'LISTJOIN stage=2',b'LOSTJOIN stage=2'))
    reject('unexpected active control',log,lambda x:x.replace(b'LISTJOIN stage=3 active=0',b'LISTJOIN stage=3 active=1'))
    reject('impossible accepted count',log,lambda x:re.sub(rb'(LISTJOIN stage=2 active=1 calls=)\d+',rb'\g<1>0',x))
    reject('missing checked bytes',log,lambda x:re.sub(rb'(LISTJOIN stage=2[^\r\n]* bytes=)\d+',rb'\g<1>0',x))
    reject('missing completion',log,lambda x:x.replace(b'PRICEDONE',b'INCOMPLETE'))
    reject('changed candidate raster','game/bin/tape-2-main.bin',lambda x:x[:64]+bytes([x[64]^1])+x[65:])
    reject('qualification manifest removed','list-join-manifest.json',lambda x:x.replace(b'"qualificationOnly": true',b'"qualificationOnly": false'))
    assert run()==0
a.output.write_text(json.dumps(dict(passed=True,negativeControls=cases),indent=2)+'\n')
print('A19 positive controls and',len(cases),'negative controls passed')
