"""Validate A23 moving workload, cold oracles and rasters; price PS2 only."""
import argparse
import copy
import csv
import hashlib
import json
import math
import re
import statistics
import struct
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true')
p.add_argument('--archive',action='store_true');p.add_argument('--output',type=Path)
a=p.parse_args();root=a.fixture
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
if a.archive:
    assert a.output and not a.output.resolve().is_relative_to(root.resolve())
    for rel,h in json.loads((root/'archive-sha256.json').read_text()).items():assert sha(root/rel)==h,rel
binding=json.loads((root/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert sha(root/rel)==h,rel
plan=json.loads((root/'list-join-drive-manifest.json').read_text())
assert plan['modes']==[0,6,0,6,0,6]*2 and plan['hours']==[12]*6+[0]*6
for name,flag in [('hardware_trace.hpp','TYRA_HARDWARE_TRACE'),('frame_profile.hpp','TYRA_FRAME_PROFILE')]:
    assert re.search(r'^#define '+flag+r' 0$',(root/'tyra/engine/inc/debug'/name).read_text(),re.M)
physics=(root/'game/src/gen/game_physics.gen.cpp').read_text()
assert 'false && stapip.core.supportsPaintVu(part.envBag.get())' in physics
assert 'if (FastJoin::checkpoint) sharedEnvBasisValid=false;' in physics
probe=(root/'tyra/engine/inc/debug/motion_probe.hpp').read_text()
assert 'inline constexpr U phaseCount=12;' in probe
assert 'constexpr U modes[]={0,6,0,6,0,6,0,6,0,6,0,6};' in probe
assert 'inline bool freezeCapture() { return false; }' in probe and 'inline bool skipBodyEnv(){return false;}' in probe
assert 'offset>=gasStart && offset%128==64' in probe
qbuffer=(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp').read_text()
assert qbuffer.count('if (FastJoin::apply(*this, pkgs,')==2
platform='physical' if a.physical else 'emulator'
folder=root/('physical/bin' if a.physical else 'game/bin')
logpath=root/('physical/console.log' if a.physical else 'emulator/emulator.log')
launch=json.loads((root/('physical/editor-deploy.json' if a.physical else 'emulator/launch.json')).read_text(encoding='utf-8-sig'))
assert sha(folder/'vehicle-playground.elf')==binding['elfSha256']==launch['elfSha256']
if a.physical:
    assert launch['runner']=='src/runner.cpp::resetPs2Link/deployToPs2'
    omitted=json.loads((root/'physical-bin-hashes.json').read_text()) if a.archive else {}
    for rel,h in json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig')).items():
        assert (sha(folder/rel) if (folder/rel).exists() else omitted.get(rel))==h,rel
text=logpath.read_text(errors='replace')
rows=[{k:float(v) for k,v in row.items()} for row in csv.DictReader((folder/'drive-probe.csv').open())]

def validate(data,log):
    assert 'DRIVEDONE valid=1' in log
    assert not any(s in log for s in ('Assertion failed','std::bad_alloc','terminate called','DMA Busy'))
    assert sorted(set(r['phase'] for r in data))==list(range(12))
    witnesses=[tuple(map(int,r)) for r in re.findall(r'JOINWITNESS phase=(\d+) frames=(\d+) calls=(\d+) accepted=(\d+) bytes=(\d+)',log)]
    checkpoints=re.findall(r'JOINCHECKPOINT phase=(\d+) target=(\d+) z=([^ ]+) speed=([^ ]+) arms=3',log)
    assert len(witnesses)==len(checkpoints)==12
    assert [r[0] for r in witnesses]==[int(r[0]) for r in checkpoints]==list(range(12))
    result=[]
    for phase in range(12):
        on=plan['modes'][phase]==6;hour=plan['hours'][phase]
        group=[r for r in data if r['phase']==phase];n=len(group)
        assert 120<n<=1200 and [r['offset'] for r in group]==list(range(1200,1200+n))
        for r in group:
            assert all(math.isfinite(v) and (v>=0 or k in ('x','y','z','yaw','eyeX','eyeY','eyeZ','lookX','lookY','lookZ')) for k,v in r.items())
            assert r['vehicles']==3 and r['driver']>=0 and r['observed']==0 and r['lifetime']==250
            assert r['gas']==(r['offset']>=1320) and r['flips']==1
            assert r['whole']==r['update']+r['render'] and r['pacing']<=r['whole'] and r['period']>0
            assert all(r[k]==0 for k in ('captureFrozen','captureBlocked','envSkipped','paintBlocked','paintVu','coreTicks','partialTicks','scopeErrors'))
            assert abs(r['x'])<1 and abs(r['yaw'])<1
        park,drive=group[:120],group[120:]
        assert all(abs(r['speed'])<.001 for r in park)
        assert abs(group[0]['z']+74.303497)<.1
        assert drive[-1]['z']>=108 and all(r['z']<108 for r in drive[:-1])
        assert all(b['z']>=r['z'] for r,b in zip(group,group[1:]))
        assert drive[-1]['z']-group[0]['z']>182 and max(r['speed'] for r in drive)>5
        angles=[math.degrees(math.atan2(r['lookX']-r['eyeX'],r['lookZ']-r['eyeZ'])) for r in drive]
        assert min(angles)<-50 and max(angles)>50
        assert all(sum(r[k] for r in drive)>0 for k in ('envDrawn','paint','probeCapture'))
        assert re.search(rf'NIGHTROADS phase={phase} hour={hour}\.000000 paused=1 chunks=66 lights='+('0' if hour else '[1-9][0-9]*')+r'\b',log)
        assert re.search(rf'TURNPINS phase={phase} count=18\b',log)
        assert re.search(rf'DRIVEPHASE phase={phase} .*valid=1 rows={n} .*variant={6 if on else 0}\b',log)
        _,frames,calls,accepted,byte_count=witnesses[phase]
        expected=sum(r['offset']%128==64 for r in drive) if on else 0
        assert frames==expected
        assert (0<accepted<=calls and byte_count>0) if on else calls==accepted==byte_count==0
        assert float(checkpoints[phase][2])==drive[-1]['z'] and float(checkpoints[phase][3])==drive[-1]['speed']
        priced=[r for r in drive if not on or r['offset']%128 not in (64,65)]
        bins=[[r for r in priced if lo<=r['z']<lo+20] for lo in range(-60,100,20)]
        assert min(map(len,bins))>=5
        item=dict(phase=phase,on=on,hour=hour,rows=n,gasFrames=len(drive),pricedFrames=len(priced),lastZ=drive[-1]['z'],
                  cameraDegrees=[min(angles),max(angles)],oracleFrames=frames,oracleCalls=calls,joined=accepted,bytes=byte_count,
                  captures=sum(r['probeCapture'] for r in drive),environmentDraws=sum(r['envDrawn'] for r in drive))
        if a.physical:
            work=lambda r:(r['whole']-r['pacing'])/294912
            times=list(map(work,priced))
            item.update(meanWorkMs=statistics.mean(times),p95WorkMs=sorted(times)[math.ceil(.95*len(times))-1],
                        maxWorkMs=max(times),entryHz=len(priced)*294912000/sum(r['period'] for r in priced),
                        spatialMeanWorkMs=[statistics.mean(map(work,b)) for b in bins])
        result.append(item)
    return result

phases=validate(rows,text)
negatives=[]
for name,key,value in [('stationary','z',-74.303497),('no-gas','gas',0),('freeze','captureFrozen',1),
                       ('cars','vehicles',1),('profiler','coreTicks',1),('paint','paintVu',1),('present','flips',0)]:
    data=copy.deepcopy(rows)
    for row in data:
        if row['gas']:row[key]=value
    try:validate(data,text)
    except AssertionError:negatives.append(name)
    else:raise AssertionError('Negative escaped: '+name)
for name,data,log in [('missing-phase',rows[:-1],text),('completion',rows,text.replace('DRIVEDONE valid=1','DRIVEDONE valid=0')),
                      ('witness',rows,re.sub(r'JOINWITNESS[^\r\n]+','',text)),('checkpoint',rows,text.replace('JOINCHECKPOINT phase=4','LOSTCHECKPOINT phase=4'))]:
    try:validate(data,log)
    except AssertionError:negatives.append(name)
    else:raise AssertionError('Negative escaped: '+name)
images=[]
for phase in range(12):
    for view in ('main','env'):
        pixels=[];dimensions=None
        for arm in range(3):
            stage=phase*3+arm;path=folder/f'tape-{stage}-{view}.bin';raw=path.read_bytes()
            w,h,psm,actual=struct.unpack('<4I',raw[:16])
            assert actual==stage and psm in (0,2) and w>0 and h>0
            assert len(raw)==16+w*h*(2 if psm==2 else 4) and len(set(raw[16:]))>8
            if dimensions is None:dimensions=(w,h,psm)
            assert dimensions==(w,h,psm)
            pixels.append(raw[16:])
        differences=[sum(a!=b for a,b in zip(pixels[0],image)) for image in pixels]
        assert not any(differences),(phase,view,differences)
        images.append(dict(phase=phase,view=view,dimensions=dimensions,differingBytes=differences))
report=dict(valid=True,rasterEqual=True,platform=platform,elfSha256=binding['elfSha256'],
            sourceFiles=len(binding['files']),phases=phases,images=images,negativeControls=negatives,
            logSha256=sha(logpath),csvSha256=sha(folder/'drive-probe.csv'),
            scope='Moving ordinary producers, roads/live captures, real dt; work includes waits/preemption, not pure EE')
if a.physical:
    contrasts=[]
    for base in (0,3,6,9):
        group=phases[base:base+3];on=[r for r in group if r['on']];off=[r for r in group if not r['on']]
        avg=lambda rr,key:statistics.mean(r[key] for r in rr)
        spatial=lambda rr:statistics.mean(statistics.mean(r['spatialMeanWorkMs']) for r in rr)
        contrasts.append(dict(firstPhase=base,hour=group[0]['hour'],order='on/off/on' if group[0]['on'] else 'off/on/off',
                              savedMeanMs=avg(off,'meanWorkMs')-avg(on,'meanWorkMs'),
                              savedSpatialMs=spatial(off)-spatial(on),
                              candidateP95Ms=avg(on,'p95WorkMs'),maxControlP95Ms=max(r['p95WorkMs'] for r in off)))
    report['contrasts']=contrasts
    report['promotionGate']=all(c['savedMeanMs']>0 and c['savedSpatialMs']>0 and c['candidateP95Ms']<=c['maxControlP95Ms']+.15 for c in contrasts)
output=a.output or root/f'list-join-drive-{platform}.json'
output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='images'},indent=2))
