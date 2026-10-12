"""Strict actual-packet / target-raster qualification, without FPS claims."""
import argparse
import csv
import hashlib
import json
import struct
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path)
p.add_argument('--target',action='store_true')
p.add_argument('--bin',type=Path)
p.add_argument('--log',type=Path)
p.add_argument('--output',type=Path)
a=p.parse_args();root=a.fixture;folder=a.bin or root/'game/bin'
kind='target' if a.target else 'packet'
rows=list(csv.DictReader((folder/f'{kind}-audit.csv').open()))
rows=[{k:int(v) for k,v in row.items()} for row in rows]
assert len(rows)==18 and [r['step'] for r in rows]==list(range(18))
logpath=a.log or root/'emulator/emulator.log'
log=logpath.read_text(errors='replace')
assert f'{kind.upper()}AUDITDONE cases=18 valid=1' in log
binding=json.loads((root/'native-binding.json').read_text())
for path,digest in binding['files'].items():
    assert hashlib.sha256((root/path).read_bytes()).hexdigest()==digest,path
assert hashlib.sha256((folder/'vehicle-playground.elf').read_bytes()).hexdigest()==binding['elfSha256']
images={}
for row in rows:
    step=row['step']
    if a.target:
        assert row['scene']==step//3 and row['enabled']==int(step%3==1)
        assert (row['tested']>0 and 0<row['rejected']<=row['tested']) if row['enabled'] else row['tested']==row['rejected']==0
        names=[f'target-{view}-{step:02d}.bin' for view in ('env','main')]
    else:
        assert row['legacy']==int(step//6==1) and row['shape']==step%3 and row['batch']==(step//3)%2
        assert row['checks']==24
        assert row['errors']>0 if row['legacy'] else row['errors']==0
        if row['shape']==1: assert row['clips']>0
        if row['shape']==0: assert row['replays']>0
        names=[f'packet-raster-{step:02d}.bin']
    for name in names:
        data=(folder/name).read_bytes();magic,number,w,h=struct.unpack('<4I',data[:16])
        assert number==step and magic==(0x31544754 if a.target else 0x31544150)
        assert 0<w<=1024 and 0<h<=1024 and len(data)==16+w*h*4
        colors=len(set(struct.iter_unpack('<I',data[16:])))
        assert colors>1,(name,'Blank raster')
        images[name]=(data[16:],w,h)
pairs=[]
for view in (('env','main') if a.target else ('raster',)):
    for group in range(6):
        indices=[group*3,group*3+1,group*3+2] if a.target else [group,group+12]
        names=[f'{kind}-{view}-{i:02d}.bin' for i in indices]
        assert all(images[n]==images[names[0]] for n in names),names
        pairs.append(names)
result=dict(valid=True,kind=kind,cases=len(rows),rows=rows,exactRasterGroups=pairs,
            elfSha256=binding['elfSha256'],sourceFiles=len(binding['files']),
            csvSha256=hashlib.sha256((folder/f'{kind}-audit.csv').read_bytes()).hexdigest(),
            logSha256=hashlib.sha256(logpath.read_bytes()).hexdigest(),
            limitations=['Not a scene performance result','Qualification applies to exercised raster cases'])
platform='physical' if a.bin else 'emulator'
(a.output or root/f'{kind}-audit-{platform}-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
from PIL import Image
name=next(iter(images));data,w,h=images[name]
if not a.output:
    Image.frombytes('RGBA',(w,h),data).convert('RGB').save(root/f'{kind}-sample.png')
print(json.dumps({k:v for k,v in result.items() if k not in ('rows','exactRasterGroups')},indent=2))
