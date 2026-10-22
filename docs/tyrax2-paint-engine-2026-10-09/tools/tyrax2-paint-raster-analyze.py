"""Strict GS-buffer comparison for the isolated A07 paint harness."""
import hashlib
import argparse
import json
import re
import struct
import sys
from pathlib import Path
import numpy as np
from PIL import Image

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('fixture',type=Path)
parser.add_argument('--output',type=Path,help='Preserve an earlier fixture by writing reports elsewhere')
args=parser.parse_args()
root=args.fixture
output=args.output or root
output.mkdir(parents=True,exist_ok=True)
folder = root/'game/bin'
binding=json.loads((root/'native-binding.json').read_text(encoding='utf-8-sig'))
launch=json.loads((root/'emulator/launch.json').read_text(encoding='utf-8-sig'))
elf=hashlib.sha256((folder/'vehicle-playground.elf').read_bytes()).hexdigest()
assert elf==binding['elfSha256']==launch['elfSha256']
for name,digest in binding['files'].items():
    assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,name
logpath = root/'emulator/emulator.log'
text = logpath.read_text(errors='replace')
manifest=json.loads((root/'paint-raster-manifest.json').read_text())
cases=manifest['cases']
assert cases in (27,54)
build_enabled=manifest.get('paintBuildEnabled',True)
if 'paintBuildEnabled' in manifest:
    assert binding.get('requiredSymbol')=='paintRasterCull'
    caps=re.findall(r'PAINTCAP enabled=(\d+) checks=(\d+) nullRejected=1 valid=1',text)
    assert caps==[(str(int(build_enabled)),'17')],caps
texture=hashlib.sha256((folder/'paint-raster.png').read_bytes()).hexdigest()
if 'textureSha256' in manifest: assert texture==manifest['textureSha256']
assert f'PAINTRASTERDONE cases={cases} valid=1' in text, 'Incomplete target run'
records = re.findall(r'PAINTRASTER step=(\d+) mode=(\d+) shape=(\d+) basis=(\d+) cull=(\d+) clip=(\d+) bytes=(\d+) valid=1',text)
assert len(records)==cases, len(records)
records = [tuple(map(int,r)) for r in records]
assert [r[0] for r in records]==list(range(cases))
if cases==54:
    fog=re.findall(r'PAINTFOG step=(\d+) enabled=(\d+) scale=([-\d.]+) offset=([-\d.]+)',text)
    assert len(fog)==cases
    for i,(step,enabled,scale,offset) in enumerate(fog):
        assert int(step)==i and int(enabled)==int(i>=27)
        if i>=27: assert float(scale)<0 and float(offset)>255
        else: assert float(scale)==0 and float(offset)==255
images=[]; files={}; routes=[]
for i,record in enumerate(records):
    p=folder/f'paint-raster-{i:02}.bin'
    data=p.read_bytes()
    magic,step,w,h,psm,mode,cull,clip=struct.unpack_from('<8I',data)
    assert magic==0x31545250 and step==i and mode==int(i%3==1 and build_enabled) and psm==0
    assert 0<w<=1024 and 0<h<=1024 and len(data)==32+w*h*4
    assert record==(i,mode,(i//9)%3,(i//3)%3,cull,clip,len(data)),record
    assert cull+clip>0,'No actual qbuffer input'
    if (i//9)%3==0: assert cull>0 and clip==0, 'Inside fixture did not use cull'
    if (i//9)%3==1: assert clip>0,'Near-plane clipping was not exercised'
    a=np.frombuffer(data[32:],dtype=np.uint8).reshape(h,w,4).copy()
    # A uniform clear or failed transfer is never an equivalence pass.
    assert len(np.unique(a[:,:,:3].reshape(-1,3),axis=0))>128,'Blank or trivial image'
    images.append(a)
    files[p.name]=hashlib.sha256(data).hexdigest()
    routes.append(dict(step=i,cull=cull,clip=clip))

def compare(data):
    result=[]
    for group in range(cases//3):
        a,b,c=data[group*3:group*3+3]
        assert a.shape==b.shape==c.shape
        assert np.array_equal(a,c),f'EE/EE drift in group {group}'
        delta=np.abs(a.astype(np.int16)-b.astype(np.int16))
        result.append(dict(group=group,shape=(group//3)%3,basis=group%3,fog=group>=9,
                           changedPixels=int(np.any(delta,axis=2).sum()),
                           maxRGBA=int(delta.max()),meanRGBA=float(delta.mean())))
    return result

result=compare(images)
if cases==54:
    def require_fog(data):
        assert all(not np.array_equal(data[i],data[i+27]) for i in range(27)), 'Fog has no raster effect'
    require_fog(images)
    try: require_fog(images[:27]+images[:27])
    except AssertionError: pass
    else: raise AssertionError('Fog no-op negative control escaped')
control=[a.copy() for a in images]
control[2][0,0,0]^=1
try: compare(control)
except AssertionError: pass
else: raise AssertionError('Off/off corruption negative control escaped')
control=[a.copy() for a in images]
control[1]=control[0].copy()
control[1][0,0,0]^=1
assert compare(control)[0]['changedPixels']==1
assert compare(control)[0]['maxRGBA']==1
for i in ([0,1,9,10,18,19]+([27,28,36,37,45,46] if cases==54 else [])):
    Image.fromarray(images[i][:,:,:3]).save(output/f'paint-raster-{i:02}.png')
report=dict(scope='Target-assembled emulator shader output only; no physical/game lifecycle/fallback qualification',
            elfSha256=elf,nativeSourceFiles=len(binding['files']),
            textureSha256=texture,textureBound='textureSha256' in manifest,
            paintBuildEnabled=build_enabled,
            sourceHashes=files,logSha256=hashlib.sha256(logpath.read_bytes()).hexdigest(),
            routes=routes,comparisons=result,exact=all(r['changedPixels']==0 for r in result),
            negativeControls=['off-off pixel corruption rejected','one candidate pixel mutation measured exactly'])
if cases==54: report['negativeControls'].append('fog no-op rejected')
(output/'paint-raster-analysis.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
assert report['exact'],'EE/VU GS output differs: inspect the preserved report, do not relax the gate silently'
