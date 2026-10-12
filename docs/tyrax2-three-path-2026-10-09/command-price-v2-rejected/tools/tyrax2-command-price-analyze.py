"""Validate A18 three-route rasters and report physical inclusive timings only."""
import argparse,hashlib,json,re,struct,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true')
a=p.parse_args();root=a.fixture
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
b=json.loads((root/'native-binding.json').read_text())
for rel,h in b['files'].items():assert sha(root/rel)==h,rel
presentation_fence='core.sync.align2D(); // Fence the hybrid presentation copy in EVERY arm.' in (root/'game/src/gen/game_physics.gen.cpp').read_text(encoding='utf-8-sig')
if a.physical:assert presentation_fence,'Physical pricing requires equal hybrid presentation completion'
folder=root/('physical/bin' if a.physical else 'game/bin')
log=root/('physical/console.log' if a.physical else 'emulator/emulator.log')
t=log.read_text(errors='replace')
launch=json.loads((root/('physical/editor-deploy.json' if a.physical else 'emulator/launch.json')).read_text(encoding='utf-8-sig'))
assert sha(folder/'vehicle-playground.elf')==b['elfSha256']==launch['elfSha256']
if a.physical:
    pins=json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig'))
    assert not any(Path(rel).match('command-*.bin') or Path(rel).match('tape-*.bin') for rel in pins)
    for rel,h in pins.items():assert sha(folder/rel)==h,rel
assert 'Assertion failed' not in t
if (folder/'log.txt').exists():assert 'Assertion failed' not in (folder/'log.txt').read_text(errors='replace')
done=re.findall(r'PRICEDONE stages=(\d+) samples=(\d+) commands=(\d+) phase=(\d+) offset=(\d+)',t)
assert len(done)==1
stages,n,commands,phase,offset=map(int,done[0]);assert (stages,n,commands)==(10,1152,448)
config=folder/'three-path.cfg'
expected=tuple(map(int,config.read_text().split())) if config.exists() else (0,1380)
assert (phase,offset)==expected
freeze=re.findall(r'PRICEFREEZE phase=(\d+) offset=(\d+) z=([^ ]+) speed=([^ ]+) yaw=([^\r\n]+)',t)
assert len(freeze)==1 and tuple(map(int,freeze[0][:2]))==(phase,offset)
frozen=t[t.index('PRICEFREEZE'):]
for name in re.findall(r'open name host:([^\s]+)',frozen):
    assert Path(name.lstrip('/')).name.startswith(('tape-','command-')),('Unexpected frozen IO',name)
cap=re.findall(r'COMMANDCAP valid=1 flushes=1 submissions=1 qwords=(\d+) bytes=(\d+) mpg=0 calls=(\d+) first=([0-9a-f]+) programEqual=1',t)
assert len(cap)==1 and 'COMMANDREADY' in t
qw,closure_bytes,vu_calls=int(cap[0][0]),int(cap[0][1]),int(cap[0][2])
assert len((folder/'command-closure.bin').read_bytes())==closure_bytes and qw*16<closure_bytes
assert sha(folder/'command-program-before.bin')==sha(folder/'command-program-after.bin')
expected_modes=[0,0,1,0,2,0,2,0,1,0]
checks=[tuple(map(int,x)) for x in re.findall(r'PRICECHECK stage=(\d+) mode=(\d+) mismatches=(\d+) mask=(\d+) bags=(\d+) events=(\d+) bytes=(\d+) lights=(\d+) target=(\d+) buffers=(\d+) frameYield=(\d+)',t)]
assert len(checks)==10 and [x[0] for x in checks]==list(range(10))
assert [x[1] for x in checks]==expected_modes
assert all(x[2:4]==(0,0) and x[4:]==checks[0][4:] for x in checks)
assert checks[0][4]>0 and checks[0][5]>=checks[0][4]
assert (checks[0][7]>0)==(phase==3),'Night light prefix disagrees with phase'
images={}
for view in ('main','env'):
    pixels=[];dimensions=None;hashes=[]
    for i in range(10):
        path=folder/f'tape-{i}-{view}.bin';raw=path.read_bytes();w,h,psm,stage=struct.unpack('<4I',raw[:16])
        assert stage==i and psm in (0,2) and w>0 and h>0
        assert len(raw)==16+w*h*(2 if psm==2 else 4) and len(set(raw[16:]))>8
        if dimensions is None:dimensions=(w,h,psm)
        assert dimensions==(w,h,psm)
        pixels.append(raw[16:]);hashes.append(sha(path))
    differences=[sum(x!=y for x,y in zip(pixels[0],image)) for image in pixels]
    assert not any(differences),(view,differences)
    images[view]=dict(dimensions=dimensions,rawSha256=hashes,differingBytes=differences)
raw=(folder/'command-times.bin').read_bytes();assert len(raw)==1152*24
rows=list(struct.iter_unpack('<6I',raw))
assert [(r[0],r[1]) for r in rows]==[(s,k) for s in range(1,10) for k in range(64,192)]
assert all(r[2]>r[3] and r[4]<=r[2] and r[5]<=r[2] for r in rows)
assert all((r[4]>0)==(expected_modes[r[0]]==2) for r in rows)
report=dict(valid=True,rasterEqual=True,presentationFence=presentation_fence,phase=phase,offset=offset,pose=dict(zip(('z','speed','yaw'),map(float,freeze[0][2:]))),
            target='physical' if a.physical else 'emulator',elfSha256=b['elfSha256'],
            logSha256=sha(log),timesSha256=sha(folder/'command-times.bin'),checks=checks,images=images,
            closure=dict(qwords=qw,bytes=closure_bytes,vuCalls=vu_calls,sha256=sha(folder/'command-closure.bin')),
            scope='Frozen Scene incl roads and live capture; pinned target/order; no update/HUD/postFX; not moving FPS or pure EE/VU/GS cost')
if a.physical:
    report['units']='ms from EE Count / 294912; pacing subtracted, startup included'
    stats=[]
    for stage in range(1,10):
        rr=[r for r in rows if r[0]==stage];work=[(r[2]-r[3])/294912 for r in rr]
        stats.append(dict(stage=stage,mode=('ordinary','list','commands')[expected_modes[stage]],
            mean=statistics.mean(work),median=statistics.median(work),p95=sorted(work)[121],maximum=max(work),
            startup=statistics.mean(r[4]/294912 for r in rr),prepareInclusive=statistics.mean(r[5]/294912 for r in rr)))
    report['stages']=stats
    report['comparisons']=[dict(stage=s,mode=stats[s-1]['mode'],
        savedVsBefore=stats[s-2]['mean']-stats[s-1]['mean'],savedVsAfter=stats[s]['mean']-stats[s-1]['mean']) for s in (2,4,6,8)]
(root/('command-price-physical.json' if a.physical else 'command-price-emulator.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('images','checks')},indent=2))
