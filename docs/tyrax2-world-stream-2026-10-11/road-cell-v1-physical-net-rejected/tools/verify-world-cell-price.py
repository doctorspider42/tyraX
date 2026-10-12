"""Qualify clean A44 six-trip price fixture; report timings only from real PS2."""
import argparse
import csv
import hashlib
import json
import re
import statistics
import struct
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path)
p.add_argument('--platform',choices=('emulator','ps2'),required=True)
p.add_argument('--attempt',type=Path)
a=p.parse_args();root=a.fixture.resolve();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
assert sha(root/'game/bin/vehicle-playground.elf')==binding['elfSha256']
manifest=json.loads((root/'world-cell-price-manifest.json').read_text())
assert manifest['modes']==[0,1,0,1,0,1] and manifest['bufferCount']==2
assert manifest['movingAutoOrder'] and manifest['fixedSceneStep'] and not manifest['inPriceReadbacks']
if a.platform=='emulator':
    assert not a.attempt
    logpath=root/'emulator/emulator.log';binpath=root/'game/bin'
    assert json.loads((root/'emulator/launch.json').read_text())['elfSha256']==binding['elfSha256']
else:
    assert a.attempt
    attempt=a.attempt.resolve();logpath=attempt/'console.log';binpath=attempt/'bin'
    deploy=json.loads((attempt/'editor-deploy.json').read_text())
    assert deploy['elfSha256']==binding['elfSha256'] and deploy['runner']=='src/runner.cpp::resetPs2Link/deployToPs2'
    for rel,digest in json.loads((attempt/'launch-inputs.json').read_text()).items():
        assert sha(binpath/rel)==digest,rel
log=logpath.read_text(errors='replace');assert 'DRIVEDONE valid=1' in log
assert re.findall(r'A44BUFFER phase=(\d+) requested=2 actual=(\d+)',log)==[(str(i),'2') for i in range(6)]
states=re.findall(r'NIGHTROADS phase=(\d+) hour=([\d.]+) paused=(\d+) chunks=(\d+) lights=(\d+)',log)
assert [int(r[0]) for r in states]==list(range(6))
assert all(float(r[1])==12 and int(r[2])==1 and int(r[3])>0 and int(r[4])==0 for r in states)
rows=list(csv.DictReader((binpath/'drive-probe.csv').open()))
phases=[[r for r in rows if int(r['phase'])==p] for p in range(6)]
assert len(rows)==sum(map(len,phases)) and all(len(r)>500 for r in phases)
pose=('offset','gas','x','y','z','speed','yaw','dt','damage','eyeX','eyeY','eyeZ','lookX','lookY','lookZ','driver','vehicles')
poses=[[tuple(r[k] for k in pose) for r in group] for group in phases]
poseEqual=all(group==poses[0] for group in poses)
assert all(int(r['scopeErrors'])==0 and int(r['captureFrozen'])==0 for r in rows)
assert all(sum(int(r['probeCapture']) for r in group if r['gas']=='1')>0 for group in phases),'Live reflection capture missing'
images=[]
def raster(stage,kind):
    file=binpath/f'world-price-{stage}-{kind}.bin';raw=file.read_bytes()
    assert len(raw)>=16
    w,h,psm,identity=struct.unpack_from('<4I',raw)
    assert identity==stage and (w,h)==((128,128) if kind=='env' else (448,448))
    assert psm==(2 if kind=='display' else 0)
    stride=2 if psm==2 else 4
    assert len(raw)==16+w*h*stride
    fmt='H' if stride==2 else 'I'
    assert len(set(struct.unpack('<'+fmt*(w*h),raw[16:])))>16
    return file,raw[16:]

clean=manifest.get('version',1)==2 and not manifest.get('clipObserver',True) and manifest.get('shakePhaseReset',False)
skipA31=manifest.get('variant')=='skip-a31'
roadCells=manifest.get('variant')=='road-cells'
allCells=skipA31 or roadCells
delivery=True if clean else None
if clean:
    configPattern=r'A44PRICECONFIG phase=(\d+) candidate=(\d+) autoOrder=1 fixedStep=1 clipObserver=0 shakeReset=1'
    if skipA31:configPattern+=' variant=skip-a31'
    if roadCells:configPattern+=' variant=road-cells'
    configs=re.findall(configPattern+r'(?=\r?\n|$)',log)
    assert configs==[(str(i),str(i%2)) for i in range(6)]
    captures=[tuple(map(int,r)) for r in re.findall(r'A44PRICEIMAGE phase=(\d+) arm=(\d+) candidate=(\d+) requested=(\d+) submitted=(\d+) fallback=(\d+) order=2 complete=1',log)]
    assert [(r[0],r[1]) for r in captures]==[(p,arm) for p in range(6) for arm in range(3)]
    for phase in range(6):
        expected=[1,1,1] if roadCells else [1,0,1] if skipA31 else [phase%2,1-phase%2,phase%2]
        for row,mode in zip(captures[phase*3:phase*3+3],expected):
            assert row[2]==mode
            delivery=delivery and (row[3]>0 and row[3]==row[4] and row[5]==0 if mode else row[3:]==(0,0,0))
            event=f'A44PRICEIMAGE phase={phase} arm={row[1]}'
            assert log.index(f'LOGWINDOWEND phase={phase}')<log.index(event)<log.index(f'DRIVEPHASE phase={phase}')
        if roadCells:
            counts=[r[3] for r in captures[phase*3:phase*3+3]]
            assert counts[0]==counts[2],'Endpoint cell delivery did not return'
            activated=counts[0]>counts[1] if phase%2 else counts[1]>counts[0]
            assert activated,'Endpoint road cells did not activate'
        for kind in ('main','env','display'):
            files,data=zip(*(raster(phase*3+arm,kind) for arm in range(3)))
            images.append(dict(phase=phase,kind=kind,files=[f.name for f in files],hashes=[sha(f) for f in files],
                differences=[sum(x!=y for x,y in zip(data[l],data[r])) for l,r in ((0,1),(1,2),(0,2))]))
    summaries=[tuple(map(int,r)) for r in re.findall(r'A44MOVING phase=(\d+) enabled=(\d+) frames=(\d+) gasFrames=(\d+) requested=(\d+) submitted=(\d+) fallback=(\d+) prepares=(\d+) prepareTicks=(\d+) dirtyFrames=(\d+) captures=(\d+)',log)]
    local=[tuple(map(int,r)) for r in re.findall(r'A44LOCALPHASE phase=(\d+) rebuilds=(\d+) ticks=(\d+) heldReaders=(\d+)',log)]
    assert [r[0] for r in summaries]==[r[0] for r in local]==list(range(6))
    for r,l in zip(summaries,local):
        assert r[1]==(1 if allCells else r[0]%2) and r[2]>=r[3]>300 and r[10]==0
        if r[1]:
            delivery=delivery and r[4]>0 and r[4]==r[5] and r[6]==0 and r[7]==1 and r[9]==0 and l[1]>=49
        else:assert r[4:9]==(0,0,0,0,0) and l[1:]==(0,0,0)
    assert not re.search(r'A44SOURCECONFIG|COMMANDCAP|COMMANDMEM|A44MOVINGFRAME',log)
    assert not re.search(r'CLIPWITNESS .*clipCalls=[1-9]',log)
    if skipA31:
        legacy=[tuple(map(int,r)) for r in re.findall(r'A44A31PHASE phase=(\d+) skip=(\d+) calls=(\d+) prepared=(\d+)',log)]
        assert [r[0] for r in legacy]==list(range(6))
        for r in legacy:
            assert r[1]==r[0]%2
            if r[1]:assert r[2:]==(0,0),'Skipped A31 work still executes'
            else:assert r[2]>=93 and r[3]>=93,'Old cell control lacks real A31 work'
    if roadCells:
        roads=[tuple(map(int,r)) for r in re.findall(r'A44ROADPHASE phase=(\d+) enabled=(\d+) compiled=(\d+) requested=(\d+) submitted=(\d+) fallback=(\d+)',log)]
        assert [r[0] for r in roads]==list(range(6))
        for r in roads:
            assert r[1]==r[0]%2
            if r[1]:assert r[2]>0 and r[3]>300 and r[3]==r[4] and r[5]==0,'Road delivery missing'
            else:assert r[2:]==(0,0,0,0),'Ordinary road role pays candidate work'
        legacy=[tuple(map(int,r)) for r in re.findall(r'A44A31PHASE phase=(\d+) skip=(\d+) calls=(\d+) prepared=(\d+)',log)]
        assert legacy==[(i,1,0,0) for i in range(6)],'Common terrain cleanup changed'
else:
    for phase in range(6):
        for kind in ('main','env','display'):
            left,data=raster(phase,kind);right,base=raster((phase//3)*3,kind)
            images.append(dict(phase=phase,kind=kind,files=[left.name,right.name],hashes=[sha(left),sha(right)],
                               differences=[sum(x!=y for x,y in zip(data,base))]))
valid=clean and poseEqual and delivery and all(not any(r['differences']) for r in images)
report=dict(valid=valid,platform=a.platform,elfSha256=binding['elfSha256'],logSha256=sha(logpath),
            clean=clean,poseEqual=poseEqual,deliveryValid=delivery,rowsByPhase=list(map(len,phases)),
            images=images,physicalPrice=False,promotionAllowed=False,
            limits=['Fixed-step controlled workload, not final ordinary variable-dt FPS qualification.',
                    'Non-pacing elapsed work is not exclusive EE time; no scene-capacity ceiling proof.'])
if skipA31:report.update(variant='skip-a31',legacyPreparation=legacy)
if roadCells:report.update(variant='road-cells',roadDelivery=roads,legacyPreparation=legacy)
if a.platform=='ps2' and valid:
    priced=[]
    for phase,group in enumerate(phases):
        gas=[r for r in group if r['gas']=='1'];assert len(gas)>300
        assert all(0<=int(r['pacing'])<=int(r['whole']) and int(r['period'])>0 for r in gas)
        factor=1000/294912000
        work=[(int(r['whole'])-int(r['pacing']))*factor for r in gas]
        period=[int(r['period'])*factor for r in gas]
        priced.append(dict(phase=phase,candidate=phase%2,rows=len(gas),nonPacingMeanMs=statistics.mean(work),
                           nonPacingP95Ms=sorted(work)[int((len(work)-1)*.95)],
                           entryHz=1000/statistics.mean(period),periodP95Ms=sorted(period)[int((len(period)-1)*.95)],
                           lateNominal60Intervals=sum(v>1000/60*1.25 for v in period)))
    forward=(priced[0]['nonPacingMeanMs']+priced[2]['nonPacingMeanMs'])/2-priced[1]['nonPacingMeanMs']
    reverse=priced[4]['nonPacingMeanMs']-(priced[3]['nonPacingMeanMs']+priced[5]['nonPacingMeanMs'])/2
    report.update(physicalPrice=True,phases=priced,forwardSavedMs=forward,reverseSavedMs=reverse,
                  controlledStable60=all(r['entryHz']>=59.5 and not r['lateNominal60Intervals'] for r in priced if r['candidate']))
out=(a.attempt if a.platform=='ps2' else root)/f'world-cell-price-{a.platform}.json'
out.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ('valid','platform','clean','poseEqual','deliveryValid','physicalPrice')}))
assert valid,'World-cell price conditioning/delivery/image gate rejected'
