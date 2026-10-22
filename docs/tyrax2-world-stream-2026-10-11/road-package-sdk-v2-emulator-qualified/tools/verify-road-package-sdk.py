"""Qualify the actual-EE SDK split marker and current source binding, not GPU/FPS."""
import argparse
import hashlib
import json
import re
from pathlib import Path

def decode(log):
    assert 'Assertion failed' not in log and 'road package split differs' not in log
    rows=re.findall(r'ROADSDKDONE bodies=(\d+) sdkComparisons=(\d+) controls=(\d+) owned=(\d+) masks=64 nativeExecuted=1 gpuExecuted=0 performance=0',log)
    assert len(rows)==1
    assert tuple(map(int,rows[0]))==(48,4128,192,48)
    assert log.count('ROADSDKDONE')==1
    return dict(zip(('bodies','sdkComparisons','controls','owned'),map(int,rows[0])))

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path)
p.add_argument('--log',type=Path)
a=p.parse_args()
root=a.fixture
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
assert sha(root/'game/bin/vehicle-playground.elf')==binding['elfSha256']
assert sha(root/'tyra/engine/bin/libtyra.a')==binding['engineArchiveSha256']
manifest=json.loads((root/'road-package-sdk-manifest.json').read_text())
for source,dest in [('tyrax2-road-package-sdk.hpp','road_package_sdk.hpp'),
                    ('tyrax2-road-package-sdk-check.hpp','road_package_sdk_check.hpp')]:
    rel='tyra/engine/inc/debug/'+dest
    assert binding['files'][rel]==manifest['headers'][source]==sha(root/rel)
path=a.log or root/'emulator/emulator.log'
log=path.read_text(errors='replace')
counts=decode(log)
good='ROADSDKDONE bodies=48 sdkComparisons=4128 controls=192 owned=48 masks=64 nativeExecuted=1 gpuExecuted=0 performance=0\n'
bad=['',good+good,good.replace('bodies=48','bodies=47'),
     good.replace('sdkComparisons=4128','sdkComparisons=4127'),
     good.replace('controls=192','controls=191'),good.replace('owned=48','owned=47'),
     good.replace('gpuExecuted=0','gpuExecuted=1'),good+'Assertion failed\n']
for value in bad:
    try:decode(value)
    except AssertionError:pass
    else:raise AssertionError('malformed SDK completion accepted')
report={'valid':True,'execution':'actual compiled EE SDK routines',
        'gpuExecuted':False,'physicalPrice':False,'elfSha256':binding['elfSha256'],
        'engineArchiveSha256':binding['engineArchiveSha256'],'logSha256':sha(path),
        'manifestSha256':sha(root/'road-package-sdk-manifest.json'),
        'readerSha256':sha(Path(__file__)),'counts':counts,'readerControls':len(bad),
        'limits':manifest['limits']}
(root/'road-package-sdk-qualified.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS actual EE SDK header/body split',counts,'parser controls',len(bad),'; no GPU/lifetime/FPS claim')
