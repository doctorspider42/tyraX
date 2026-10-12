"""Render captured CT32 A44 images and locate exact candidate/control differences."""
import argparse
import json
import struct
from pathlib import Path
from PIL import Image

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--stage',type=int,required=True)
a=p.parse_args();out=a.fixture/'image-diff';out.mkdir(exist_ok=True)
images=[]
for stage in range(a.stage,a.stage+3):
    raw=(a.fixture/'game/bin'/f'cell-{stage}-main.bin').read_bytes()
    w,h,psm,n=struct.unpack_from('<4I',raw)
    assert psm==0 and n==stage and len(raw)==16+w*h*4
    payload=raw[16:];images.append(payload)
    Image.frombytes('RGBA',(w,h),payload).convert('RGB').save(out/f'main-{stage}.png')
pairs=[]
for before,after in ((0,1),(0,2),(1,2)):
    changed=[i for i in range(w*h) if images[before][i*4:i*4+4]!=images[after][i*4:i*4+4]]
    box=[min(i%w for i in changed),min(i//w for i in changed),
         max(i%w for i in changed),max(i//w for i in changed)] if changed else None
    mask=bytearray(w*h*3)
    for i in changed:mask[i*3:i*3+3]=bytes((255,0,255))
    Image.frombytes('RGB',(w,h),bytes(mask)).save(out/f'diff-{before}-{after}.png')
    pairs.append(dict(pair=[before,after],pixels=len(changed),box=box))
print(json.dumps(pairs))
