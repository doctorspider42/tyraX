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
log=logpath.read_text(errors='replace')
if a.platform=='emulator' and (binpath/'log.txt').exists():
    assert 'Assertion failed!' not in (binpath/'log.txt').read_text(errors='replace'), 'Quiet target assertion in host log.txt'
assert 'DRIVEDONE valid=1' in log
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
directCold=manifest.get('variant')=='direct-cold'
paintCells=manifest.get('variant')=='paint-cells'
pcRefresh=manifest.get('variant')=='pc-refresh'
roadPackages=manifest.get('variant')=='road-packages'
terrainNormals=manifest.get('variant')=='terrain-normals'
nativeSegments=manifest.get('variant')=='native-segments'
inputCompact=manifest.get('variant')=='input-compact'
audioEvent=manifest.get('variant')=='audio-event' or terrainNormals or nativeSegments or inputCompact
audioIdle=manifest.get('variant')=='audio-idle' or audioEvent
wheelVu=manifest.get('variant')=='wheels-vu' or audioIdle
commonDirect=directCold or paintCells or pcRefresh or roadPackages or wheelVu
if commonDirect:
    assert sha(root/'tyra/engine/inc/debug/world_cell_direct.hpp')==manifest['directHeaderSha256']
    assert 'WorldCellComplete::directAudit=MotionProbe::phase<6 && MotionProbe::offset==1100;' in (root/'game/src/terrain_game.cpp').read_text()
allCells=skipA31 or roadCells or commonDirect
delivery=True if clean else None
if clean:
    configPattern=r'A44PRICECONFIG phase=(\d+) candidate=(\d+) autoOrder=1 fixedStep=1 clipObserver=0 shakeReset=1'
    if skipA31:configPattern+=' variant=skip-a31'
    if roadCells:configPattern+=' variant=road-cells'
    if directCold:configPattern+=' variant=direct-cold'
    if paintCells:configPattern+=' variant=paint-cells'
    if pcRefresh:configPattern+=' variant=pc-refresh'
    if roadPackages:configPattern+=' variant=road-packages'
    if wheelVu:configPattern+=' variant=input-compact' if inputCompact else ' variant=native-segments' if nativeSegments else ' variant=terrain-normals' if terrainNormals else ' variant=audio-event' if audioEvent else ' variant=audio-idle' if audioIdle else ' variant=wheels-vu'
    configs=re.findall(configPattern+r'(?=\r?\n|$)',log)
    assert configs==[(str(i),str(i%2)) for i in range(6)]
    captures=[tuple(map(int,r)) for r in re.findall(r'A44PRICEIMAGE phase=(\d+) arm=(\d+) candidate=(\d+) requested=(\d+) submitted=(\d+) fallback=(\d+) order=2 complete=1',log)]
    assert [(r[0],r[1]) for r in captures]==[(p,arm) for p in range(6) for arm in range(3)]
    for phase in range(6):
        expected=[1,1,1] if roadCells or commonDirect else [1,0,1] if skipA31 else [phase%2,1-phase%2,phase%2]
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
    if commonDirect:
        cold=[tuple(map(int,r)) for r in re.findall(r'A44COLDPHASE phase=(\d+) direct=(\d+) directCalls=(\d+) materializedCalls=(\d+) audits=(\d+)',log)]
        assert [r[0] for r in cold]==list(range(6))
        for r,l in zip(cold,local):
            assert r[1]==(1 if paintCells or pcRefresh or roadPackages or wheelVu else r[0]%2) and r[4]>=20 and l[1]==93 and l[3]>0
            assert r[2]>=113 and r[3]==0 if r[1] else r[3]>=113 and r[2]==0
        coldImages=[tuple(map(int,r)) for r in re.findall(r'A44COLDIMAGE phase=(\d+) arm=(\d+) direct=(\d+) directCalls=(\d+) materializedCalls=(\d+)',log)]
        assert [(r[0],r[1]) for r in coldImages]==[(p,arm) for p in range(6) for arm in range(3)]
        for phase in range(6):
            modes=[1,1,1] if paintCells or pcRefresh or roadPackages or wheelVu else [phase%2,1-phase%2,phase%2]
            for r,mode in zip(coldImages[phase*3:phase*3+3],modes):
                assert r[2]==mode
                assert r[3]>0 and r[4]==0 if mode else r[4]>0 and r[3]==0
        legacy=[tuple(map(int,r)) for r in re.findall(r'A44A31PHASE phase=(\d+) skip=(\d+) calls=(\d+) prepared=(\d+)',log)]
        assert legacy==[(i,1,0,0) for i in range(6)]
    if paintCells:
        paint=[tuple(map(int,r)) for r in re.findall(r'A44PAINTIMAGE phase=(\d+) arm=(\d+) enabled=(\d+) heads=(\d+) vuHeads=(\d+) headWords=(\d+) paintWords=(\d+)',log)]
        assert [(r[0],r[1]) for r in paint]==[(p,arm) for p in range(6) for arm in range(3)]
        paintMoving=[]
        for phase,group in enumerate(phases):
            total=sum(int(r['paintVu']) for r in group if r['gas']=='1')
            assert total>0 if phase%2 else total==0,'Moving paint activation missing'
            paintMoving.append(total)
            triplet=paint[phase*3:phase*3+3]
            for r,mode in zip(triplet,[phase%2,1-phase%2,phase%2]):
                assert r[2]==mode and r[3]>0
                assert 0<r[4]<=r[3] and r[6]==3*r[4] if mode else r[4]==r[6]==0
            first,on,last=triplet
            assert first[3]==on[3]==last[3] and first[5]==last[5]
            assert first[5]-on[5]==3*first[4] if phase%2 else on[5]-first[5]==3*on[4],'Paint head delta changed'
    if pcRefresh or roadPackages or wheelVu:
        paint=[tuple(map(int,r)) for r in re.findall(r'A44PAINTIMAGE phase=(\d+) arm=(\d+) enabled=(\d+) heads=(\d+) vuHeads=(\d+) headWords=(\d+) paintWords=(\d+)',log)]
        assert [(r[0],r[1]) for r in paint]==[(p,arm) for p in range(6) for arm in range(3)]
        for phase,group in enumerate(phases):
            assert sum(int(r['paintVu']) for r in group if r['gas']=='1')>0
            triplet=paint[phase*3:phase*3+3]
            assert all(r[2]==1 and r[3]>=r[4]>0 and r[6]==3*r[4] for r in triplet)
            assert all(r[2:]==triplet[0][2:] for r in triplet),'Sampler changed held paint contracts'
    if wheelVu:
        wheels=[tuple(map(int,r)) for r in re.findall(r'WHEELVU phase=(\d+) arm=(-?\d+) active=(\d+) cars=(\d+) draws=(\d+) vertices=(\d+)',log)]
        wheelMoving=[r for r in wheels if r[1]==-1];wheelHeld=[r for r in wheels if r[1]!=-1]
        assert [r[0] for r in wheelMoving]==list(range(6))
        assert [(r[0],r[1]) for r in wheelHeld]==[(p,arm) for p in range(6) for arm in range(3)]
        for r in wheels:
            mode=1 if audioIdle else (r[0]%2 if r[1]==-1 or r[1]!=1 else 1-r[0]%2)
            assert r[2]==mode
            if mode:assert r[3]>0 and r[4]==4*r[3] and r[5]>r[4],'Local wheel GPU activation missing'
            else:assert r[3:]==(0,0,0),'Ordinary arm used local wheel route'
        if manifest.get('wheelMovingOnly'):
            wheelStatic=[tuple(map(int,r)) for r in re.findall(r'WHEELSTATIC phase=(\d+) arm=(-?\d+) active=(\d+) batches=(\d+)',log)]
            assert [(r[0],r[1],r[2]) for r in wheelStatic]==[(r[0],r[1],r[2]) for r in wheels]
            for r,s in zip(wheels,wheelStatic):
                if r[2] and r[1]==-1:
                    assert s[3]>300 and 300<r[3]<4498,'Moving offload or ordinary parked-wheel batches missing'
                if not r[2]:assert s[3]==0
        if audioIdle:
            assert manifest.get('audioIdleNoPriceObserver')
            assert 'AudioIdle::setObserved(!sample);' in (root/'tyra/engine/inc/debug/motion_probe.hpp').read_text()
            pattern=(r'AUDIOEVENT phase=(\d+) arm=(-?\d+) enabled=(\d+) waiting=(\d+) waiters=(\d+) idleHalf=(\d+) pcm=(\d+) starts=(\d+)'
                if audioEvent else r'AUDIOIDLE phase=(\d+) arm=(-?\d+) enabled=(\d+) idle16=(\d+) idleHalf=(\d+) pcm=(\d+) starts=(\d+)')
            audio=[tuple(map(int,r)) for r in re.findall(pattern,log)]
            assert [(r[0],r[1]) for r in audio]==[(r[0],r[1]) for r in wheels]
            for r in audio:
                mode=1 if terrainNormals or nativeSegments or inputCompact else (r[0]%2 if r[1]==-1 or r[1]!=1 else 1-r[0]%2)
                assert r[2]==mode
                if audioEvent:
                    assert r[6]==r[7]==0,'PCM invalidates empty event worker experiment'
                    if mode:assert r[3]==r[4]==1 and r[5]==0,'Actual event semaphore waiter missing'
                    else:assert r[3]==r[4]==0 and r[5]>0,'Ordinary empty polling missing'
                else:
                    assert r[5]==r[6]==0,'PCM playback invalidates empty-worker experiment'
                    if mode:assert r[3]>0 and r[4]==0,'Actual16ms empty-worker path missing'
                    else:assert r[3]==0 and r[4]>0,'Ordinary empty-worker path missing'
    if inputCompact:
        assert log.count('INPUTINIT mode=0 mask=0 row=0 common=1')==1
        for name,digest in manifest['compactHelpers'].items():
            target={'tyrax2-world-input-policy.hpp':'world_input_policy.hpp',
                    'tyrax2-world-input-compact.hpp':'world_input_compact.hpp'}[name]
            assert sha(root/'tyra/engine/inc/debug'/target)==digest
        inputFormats=[tuple(map(int,r)) for r in re.findall(r'INPUTFORMAT phase=(\d+) arm=(-?\d+) enabled=(\d+) cells=(\d+) compacted=(\d+) fallback=(\d+) originalWords=(\d+) packedWords=(\d+)',log)]
        assert [(r[0],r[1]) for r in inputFormats]==[(r[0],r[1]) for r in wheels]
        for r in inputFormats:
            mode=r[0]%2 if r[1]!=1 else 1-r[0]%2
            assert r[2]==mode and r[3]>0
            if mode:assert r[4]>0 and 0<r[7]<r[6], 'Cold compact body activation missing'
            else:assert r[4:]==(0,0,0,0), 'Ordinary role compacts geometry'
        inputCaptureFields=('probeCapture','envEligible','envDrawn','paintVu')
        inputCaptureSequences=[[tuple(int(row[field]) for field in inputCaptureFields)
                                for row in group if row['gas']=='1'] for group in phases]
        assert all(sequence==inputCaptureSequences[0] for sequence in inputCaptureSequences), 'Input format changed live capture/paint workload'
    if nativeSegments:
        captureFields=('probeCapture','envEligible','envDrawn','paintVu')
        captureSequences=[[tuple(int(row[field]) for field in captureFields)
                           for row in group if row['gas']=='1'] for group in phases]
        assert all(sequence==captureSequences[0] for sequence in captureSequences), 'Early segments changed moving reflection/paint workload'
        segments=[tuple(map(int,r)) for r in re.findall(r'NATIVESEGMENT phase=(\d+) arm=(-?\d+) enabled=(\d+) gasFrames=(\d+) gasEarly=(\d+) heldEarly=(\d+)',log)]
        assert [(r[0],r[1]) for r in segments]==[(r[0],r[1]) for r in wheels]
        for r in segments:
            mode=r[0]%2 if r[1]!=1 else 1-r[0]%2
            assert r[2]==mode
            if r[1]==-1:
                gas=sum(row['gas']=='1' for row in phases[r[0]])
                assert r[3]==gas and (0<r[4]<=gas if mode else r[4]==0),'Actual priced early native publication missing'
                if mode:
                    assert r[4]==sum(int(row['probeCapture']) for row in phases[r[0]] if row['gas']=='1'), 'Capture-to-publication coverage incomplete'
            else:
                assert r[5]>0 if mode else r[5]==0,'Held early publication missing'
        segmentAudits=[tuple(map(int,r)) for r in re.findall(r'NATIVESEGMENTAUDIT phase=(\d+) arm=(\d+) enabled=(\d+) windows=(\d+) protected=(\d+) equal=1',log)]
        assert [(r[0],r[1]) for r in segmentAudits]==[(p,arm) for p in range(6) for arm in range(3)]
        for r in segmentAudits:
            mode=r[0]%2 if r[1]!=1 else 1-r[0]%2
            assert r[2]==mode
            assert r[3]>=2 and r[4]>0 if mode else r[3]==1 and r[4]==0,'Actual old native bytes not protected'
    if terrainNormals:
        normalMovingImages=[]
        for kind in ('main','env','display'):
            files,data=zip(*(raster(phase*3,kind) for phase in range(6)))
            assert all(pixels==data[0] for pixels in data[1:]),'Terrain normal moving reconstruction changes pixels'
            normalMovingImages.append(dict(kind=kind,files=[f.name for f in files],
                hashes=[sha(f) for f in files],crossPhasePixelsExact=True))
        sources=[tuple(map(int,r)) for r in re.findall(r'NORMALSOURCE generation=(\d+) samples=(\d+) bytes=(\d+) exact=1',log)]
        assert sources and all(n>0 and size==n*12 for _,n,size in sources),'Native original lattice oracle missing'
        normals=[tuple(map(int,r)) for r in re.findall(r'NORMALWITNESS phase=(\d+) arm=(-?\d+) enabled=(\d+) builds=(\d+) uses=(\d+) audits=(\d+) bytes=(\d+)',log)]
        assert [(r[0],r[1]) for r in normals]==[(r[0],r[1]) for r in wheels]
        for r in normals:
            mode=r[0]%2 if r[1]==-1 or r[1]!=1 else 1-r[0]%2
            assert r[2]==mode and r[6]>0
            if r[1]==-1:
                assert r[3]>0
                if mode:assert r[4]>0 and r[4]<=r[3],'Moving prepared normal producer inactive'
                else:assert r[4]==0,'Control built terrain with prepared normals'
    if roadPackages:
        wires=re.findall(r'ROADWIRE count=(\d+) sourceWriter=1 transport=1 outsidePrice=1',log)
        assert len(wires)==1 and int(wires[0])>=3,'Actual emitted transport/ordinary-writer oracle missing'
        assert log.index('LOGWINDOWEND phase=0')<log.index('ROADWIRE')<log.index('LOGWINDOWBEGIN phase=1')
        roads=[tuple(map(int,r)) for r in re.findall(r'ROADPREP phase=(\d+) arm=(-?\d+) active=(\d+) compiled=(\d+) failed=(\d+) inside=(\d+) partial=(\d+) outside=(\d+) emitted=(\d+) checks=(\d+) unready=(\d+)',log)]
        moving=[r for r in roads if r[1]==-1]
        held=[r for r in roads if r[1]!=-1]
        assert [r[0] for r in moving]==list(range(6))
        assert [(r[0],r[1]) for r in held]==[(p,arm) for p in range(6) for arm in range(3)]
        for r in moving:
            assert r[2]==r[0]%2 and r[4]==r[10]==0,'Road producer preparation/admission failed'
            assert r[9]>0,'Live original package audit missing'
            if r[2]:assert r[6]>300 and r[8]>r[6],'Partial prepared GPU route missing'
            else:assert r[5:9]==(0,0,0,0),'Ordinary arm emitted prepared road work'
        for r in held:
            assert r[2]==(1-r[0]%2 if r[1]==1 else r[0]%2)
            assert r[4]==r[10]==0
            if r[2]:assert r[6]>0 and r[8]>0,'Held partial prepared activation missing'
            else:assert r[5:9]==(0,0,0,0)
        if manifest.get('roadWholeReplay'):
            whole=[tuple(map(int,r)) for r in re.findall(r'ROADWHOLE phase=(\d+) arm=(-?\d+) active=(\d+) replayed=(\d+) fallback=(\d+)',log)]
            assert [(r[0],r[1]) for r in whole]==[(r[0],r[1]) for r in roads],'Whole replay witnesses missing/reordered'
            for r,w in zip(roads,whole):
                assert w[2]==r[2] and w[3]+w[4]==r[5],'Whole replay admission accounting differs'
                if r[2] and r[1]==-1:assert w[3]>300,'Moving whole route still returns to generic frontend'
                if not r[2]:assert w[3:]==(0,0),'Ordinary arm used prepared whole route'
        if manifest.get('roadLifetime'):
            life=re.findall(r'ROADLIFE phase=(\d+) heldReader=1 replaced=1 oldBytes=1 newBytes=1 staleRejected=1 outsidePrice=1',log)
            assert list(map(int,life))==[1,3,5],'Positive road-specific old-bank replacement missing'
            for phase in (1,3,5):
                assert log.index('LOGWINDOWEND phase='+str(phase-1))<log.index('ROADLIFE phase='+str(phase))<log.index('LOGWINDOWBEGIN phase='+str(phase)),'Road pressure ran inside priced rows'
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
if directCold:report.update(variant='direct-cold',coldCompilation=cold,coldEndpointCompilation=coldImages,legacyPreparation=legacy)
if paintCells:report.update(variant='paint-cells',paintHeads=paint,movingPaint=paintMoving,coldCompilation=cold,coldEndpointCompilation=coldImages,legacyPreparation=legacy)
if pcRefresh:report.update(variant='pc-refresh',paintHeads=paint,coldCompilation=cold,coldEndpointCompilation=coldImages,legacyPreparation=legacy)
if wheelVu:report.update(variant=manifest['variant'],wheelMoving=wheelMoving,wheelHeld=wheelHeld,
    paintHeads=paint,coldCompilation=cold,coldEndpointCompilation=coldImages,legacyPreparation=legacy)
if wheelVu and manifest.get('wheelMovingOnly'):report['wheelStatic']=wheelStatic
if audioIdle:report['audioIdle']=audio
if terrainNormals:report.update(normalSources=sources,normalWitnesses=normals,normalMovingImages=normalMovingImages)
if nativeSegments:report.update(nativeSegmentWitnesses=segments,nativeSegmentAudits=segmentAudits,
    nativeSegmentCaptureWorkEqual=True,nativeSegmentCaptureFields=captureFields)
if inputCompact:report.update(inputFormatWitnesses=inputFormats,inputCaptureWorkEqual=True,
                              inputCaptureFields=inputCaptureFields)
if roadPackages:report.update(variant='road-packages',roadPrepared=moving,roadPreparedImages=held,
    paintHeads=paint,coldCompilation=cold,coldEndpointCompilation=coldImages,legacyPreparation=legacy)
if roadPackages and manifest.get('roadWholeReplay'):report['roadWholeReplay']=whole
if roadPackages and manifest.get('roadLifetime'):report['roadLifetime']=list(map(int,life))
if a.platform=='ps2' and valid:
    if nativeSegments or inputCompact:
        actualPresents=[]
        for phase,group in enumerate(phases):
            gas=[r for r in group if r['gas']=='1']
            file=binpath/f'triple-present-{phase}.csv'
            records=list(csv.DictReader(file.open()))
            assert len(records)==len(gas)+1 and all(r['slot']=='1' and r['isr']=='0' for r in records), 'Actual presentation capture mismatch'
            intervals=[((int(records[i]['tick'])-int(records[i-1]['tick'])) & 0xffffffff)/294912 for i in range(1,len(records))]
            assert all(10<value<100 for value in intervals), 'Invalid actual presentation interval'
            actualPresents.append(dict(phase=phase,records=len(records),sha256=sha(file),
                presentHz=1000*len(intervals)/sum(intervals),latePresentIntervals=sum(value>25 for value in intervals)))
        stable=all(r['latePresentIntervals']==0 for r in actualPresents if r['phase']%2)
        if nativeSegments:report.update(nativeSegmentActualPresents=actualPresents,actualPresentStable60=stable)
        else:report.update(inputFormatActualPresents=actualPresents,actualPresentStable60=stable)
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
    if commonDirect:
        report['localCompilerBoundary']=[dict(phase=r[0],replacements=r[1],elapsedTicks=r[2],
            elapsedTotalMs=r[2]*factor,elapsedMeanPerReplacementMs=r[2]*factor/r[1]) for r in local]
        report['limits'].append('Local compiler boundary includes pre-price replacements and preemption/publication; not exclusive EE cycles or all terrain build work.')
    if pcRefresh:
        report['limits'].append('Sampler observer comparison: savedMs is not an optimization gain; PC qualification/symbolization are separate.')
out=(a.attempt if a.platform=='ps2' else root)/f'world-cell-price-{a.platform}.json'
out.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ('valid','platform','clean','poseEqual','deliveryValid','physicalPrice')}))
assert valid,'World-cell price conditioning/delivery/image gate rejected'
