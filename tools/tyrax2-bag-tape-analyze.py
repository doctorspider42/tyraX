"""Compare A18 actual GS raw images. No timing conclusions."""
import argparse,hashlib,json,re,struct
from PIL import Image
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true')
a=p.parse_args();root=a.fixture
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
folder=root/('physical/bin' if a.physical else 'game/bin')
log=root/('physical/console.log' if a.physical else 'emulator/emulator.log')
text=log.read_text(errors='replace')
launch=json.loads((root/('physical/editor-deploy.json' if a.physical else 'emulator/launch.json')).read_text(encoding='utf-8-sig'))
assert sha(folder/'vehicle-playground.elf')==binding['elfSha256']==launch['elfSha256']
done=re.findall(r'TAPEDONE stages=(\d+) qualificationOnly=1(?: commands=(\d+))?',text)
assert len(done)==1
stage_count=int(done[0][0]);assert stage_count in (4,6)
if stage_count==6:
    assert done[0][1]=='24' and 'COMMANDCAP valid=1 flushes=1 submissions=1' in text
    assert 'COMMANDREADY' in text
stages=[tuple(map(int,x)) for x in re.findall(r'TAPESTAGE stage=(\d+) bags=(\d+) events=(\d+) bytes=(\d+) replay=(\d+)',text)]
assert len(stages)==stage_count and [s[0] for s in stages]==list(range(stage_count))
assert all(s[1:4]==stages[0][1:4] for s in stages) and stages[0][1]>0
assert [s[4] for s in stages] in ([0,0,0,0],[0,0,1,0],[0,0,1,0,0,0])
checks=[tuple(map(int,x)) for x in re.findall(r'TAPECHECK stage=(\d+) mismatches=(\d+) mask=(\d+) first=(\d+)',text)]
has_checks='TAPECHECK' in (root/'game/src/gen/game_physics.gen.cpp').read_text(encoding='utf-8-sig')
assert not has_checks or [c[0] for c in checks]==list(range(stage_count))
inputs_equal=all(c[1]==0 and c[2]==0 for c in checks) if has_checks else None
if a.physical:
    pins=json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig'))
    assert not any(Path(rel).match('tape-*.bin') for rel in pins),'Readbacks must not be launch inputs'
    for rel,digest in pins.items(): assert sha(folder/rel)==digest,rel
result={}
for view in ('main','env'):
    images=[];dims=None
    for i in range(stage_count):
        f=folder/f'tape-{i}-{view}.bin';data=f.read_bytes();w,h,psm,stage=struct.unpack('<4I',data[:16])
        assert stage==i and psm in (0,2) and w>0 and h>0
        assert len(data)==16+w*h*(2 if psm==2 else 4)
        if dims is None:dims=(w,h,psm)
        assert dims==(w,h,psm)
        pixels=data[16:];assert len(set(pixels))>8,'Blank or trivial raster'
        if psm==0:
            prefix='tape-physical' if a.physical else 'tape'
            Image.frombytes('RGBA',(w,h),pixels).convert('RGB').save(root/f'{prefix}-{i}-{view}.png')
        images.append(pixels)
    result[view]=dict(dimensions=dims,hashes=[hashlib.sha256(b).hexdigest() for b in images],
      differingBytesVsCapture=[sum(x!=y for x,y in zip(images[0],b)) for b in images],
      replayVsControlBytes=sum(x!=y for x,y in zip(images[1],images[2])))
    if dims[2]==0:
        changes=[i for i,(x,y) in enumerate(zip(images[0],images[3])) if x!=y]
        if changes:
            result[view]['returnChangedChannels']=sorted({i%4 for i in changes})
            result[view]['returnChangedBounds']=[min(i//4%dims[0] for i in changes),min(i//4//dims[0] for i in changes),
                                                 max(i//4%dims[0] for i in changes),max(i//4//dims[0] for i in changes)]
passed=all(not any(r['differingBytesVsCapture']) for r in result.values())
report=dict(rasterEqual=passed,comparedInputsEqual=inputs_equal,checks=checks,stages=stages,images=result,elfSha256=binding['elfSha256'],
            mode='replay' if stages[2][4] else 'ordinary-control',
            scope='Only frozen day Scene; no physical speed or complete command replay claim')
(root/('tape-physical.json' if a.physical else 'tape-emulator.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
raise SystemExit(0 if passed and inputs_equal is not False else 2)
