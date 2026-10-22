"""Read source-bound A44 compiler execution only; no raster/FPS qualification."""
import argparse,hashlib,json,re
from pathlib import Path

def read_witness(log):
    pattern=(r'A44SDK checks=(\d+) passes=(\d+) parts=(\d+) controls=(\d+) '
             r'sourceDelivery=PASS execution=EE compileOnly=0 gpuExecuted=0')
    rows=re.findall(pattern,log)
    assert len(rows)==1,('completion witnesses',len(rows))
    checks,passes,parts,controls=map(int,rows[0])
    assert checks==12 and passes==60 and parts>60 and controls==54,rows[0]
    return dict(cells=checks,passes=passes,parts=parts,negativeControls=controls)

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true')
a=p.parse_args();root=a.fixture.resolve()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
assert binding['requiredSymbol']=='WorldCellSdkCheck::checks'
assert sha(root/'game/bin/vehicle-playground.elf')==binding['elfSha256']
for rel,value in binding['files'].items():assert sha(root/rel)==value,rel
manifest=json.loads((root/'world-cell-sdk-manifest.json').read_text())
assert manifest['experiment']=='A44-sdk-execution' and manifest['hybridBuffers']==2
assert manifest['worldRuntimeChanged'] is False
if a.physical:
    logpath=root/'physical/console.log'
else:
    launch=json.loads((root/'emulator/launch.json').read_text())
    assert launch['elfSha256']==binding['elfSha256']
    logpath=root/'emulator/emulator.log'
log=logpath.read_text(errors='replace')
witness=read_witness(log)
line=re.search(r'A44SDK[^\n\r]*',log)[0]
controls={'missing':'','duplicate':line+'\n'+line,
          'incomplete':line.replace('checks=12','checks=11'),
          'passes':line.replace('passes=60','passes=59'),
          'controls':line.replace('controls=54','controls=53'),
          'compile-only':line.replace('compileOnly=0','compileOnly=1'),
          'gpu-claim':line.replace('gpuExecuted=0','gpuExecuted=1')}
for name,text in controls.items():
    try:read_witness(text)
    except AssertionError:continue
    raise AssertionError('Reader accepted invalid witness: '+name)
report={'valid':True,'experiment':'A44-sdk-execution',
        'platform':'physical' if a.physical else 'emulator',**witness,
        'elfSha256':binding['elfSha256'],'logSha256':sha(logpath),
        'readerNegativeControls':list(controls),'promotionAllowed':False,
        'limits':['EE compiler execution and decoded source delivery only.',
                  'New bodies were NOT submitted to VU1/GS; no raster gate.',
                  'No new world runtime, live head/VRAM/lease or FPS qualification.']}
dest=root/('cell-sdk-physical.json' if a.physical else 'cell-sdk-emulator.json')
dest.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
