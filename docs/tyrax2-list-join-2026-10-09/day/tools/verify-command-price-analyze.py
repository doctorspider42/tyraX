"""Negative controls for the A18 pricing evidence reader; never edit original evidence."""
import argparse,json,shutil,subprocess,sys,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();tool=Path(__file__).with_name('tyrax2-command-price-analyze.py')
b=json.loads((a.fixture/'native-binding.json').read_text())
with tempfile.TemporaryDirectory(prefix='a18-reader-') as tmp:
    root=Path(tmp)
    files=list(b['files'])+['native-binding.json','emulator/launch.json','emulator/emulator.log','game/bin/vehicle-playground.elf']
    files += [str(f.relative_to(a.fixture)) for f in (a.fixture/'game/bin').glob('command-*.bin')]
    files += [str(f.relative_to(a.fixture)) for f in (a.fixture/'game/bin').glob('tape-*.bin')]
    if(a.fixture/'game/bin/three-path.cfg').exists():files.append('game/bin/three-path.cfg')
    for rel in files:
        dst=root/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(a.fixture/rel,dst)
    def run():return subprocess.run([sys.executable,str(tool),str(root)],stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode
    assert run()==0,'Positive control failed'
    cases=[]
    def check(name,rel,change):
        path=root/rel;original=path.read_bytes();changed=change(original);assert changed!=original
        path.write_bytes(changed)
        try:assert run()!=0,name;cases.append(name)
        finally:path.write_bytes(original)
    check('missing completion','emulator/emulator.log',lambda x:x.replace(b'PRICEDONE',b'INCOMPLETE'))
    check('capture rejected','emulator/emulator.log',lambda x:x.replace(b'COMMANDCAP valid=1',b'COMMANDCAP valid=0'))
    check('descriptor mismatch','emulator/emulator.log',lambda x:x.replace(b'mismatches=0 mask=0',b'mismatches=1 mask=1',1))
    check('unexpected frozen file IO','emulator/emulator.log',lambda x:x+b'\nopen name host:/log.txt flag 602\n')
    check('wrong sample count','game/bin/command-times.bin',lambda x:x[:-24])
    check('wrong sample order','game/bin/command-times.bin',lambda x:x[24:48]+x[:24]+x[48:])
    check('changed raster','game/bin/tape-4-main.bin',lambda x:x[:64]+bytes([x[64]^1])+x[65:])
    check('wrong raw image stage','game/bin/tape-4-env.bin',lambda x:x[:12]+bytes([0])+x[13:])
    check('changed resident program','game/bin/command-program-after.bin',lambda x:bytes([x[0]^1])+x[1:])
    check('changed ELF','game/bin/vehicle-playground.elf',lambda x:bytes([x[0]^1])+x[1:])
    check('changed source',next(iter(b['files'])),lambda x:x+b'\n')
    assert run()==0,'Final positive control failed'
result=dict(passed=True,negativeControls=cases,analyzerSha256=__import__('hashlib').sha256(tool.read_bytes()).hexdigest())
a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
