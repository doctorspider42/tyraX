"""Validate A16 scene travel and target gate; price only the physical PS2 run."""
import argparse
import copy
import csv
import hashlib
import json
import math
import re
import statistics
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path)
p.add_argument('--physical',action='store_true')
p.add_argument('--output',type=Path)
a=p.parse_args();root=a.fixture.resolve()
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
for name,flag in [('hardware_trace.hpp','TYRA_HARDWARE_TRACE'),('frame_profile.hpp','TYRA_FRAME_PROFILE')]:
    assert re.search(r'^#define '+flag+r' 0$',(root/'tyra/engine/inc/debug'/name).read_text(),re.M)
manifest=json.loads((root/'target-audit-manifest.json').read_text())
assert manifest['drive'] and manifest['paint']=='EE in all drive arms'
physics=(root/'game/src/gen/game_physics.gen.cpp').read_text()
assert 'false && stapip.core.supportsPaintVu(part.envBag.get())' in physics
assert '(MotionProbe::offset / 240) % 3' not in physics and 'PaintLife::' not in physics
probe=(root/'tyra/engine/inc/debug/motion_probe.hpp').read_text()
assert 'inline bool freezeCapture() { return false; }' in probe
assert 'inline bool skipBodyEnv(){return false;}' in probe
platform='physical' if a.physical else 'emulator'
folder=root/'physical/bin' if a.physical else root/'game/bin'
logpath=root/'physical/console.log' if a.physical else root/'emulator/emulator.log'
launchpath=root/'physical/editor-deploy.json' if a.physical else root/'emulator/launch.json'
launch=json.loads(launchpath.read_text(encoding='utf-8-sig'))
assert sha(folder/'vehicle-playground.elf')==binding['elfSha256']==launch['elfSha256']
if a.physical:
    assert launch['runner']=='src/runner.cpp::resetPs2Link/deployToPs2'
    for rel,digest in json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig')).items():
        assert sha(folder/rel)==digest,rel
text=logpath.read_text(errors='replace')
rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader((folder/'drive-probe.csv').open())]

def validate(data,log):
    assert 'DRIVEDONE valid=1' in log
    assert sorted(set(r['phase'] for r in data))==list(range(6))
    counters=[tuple(map(int,r)) for r in re.findall(r'TARGETCOUNTS phase=(\d+) tested=(\d+) rejected=(\d+)',log)]
    assert len(counters)==6 and [r[0] for r in counters]==list(range(6))
    results=[]
    for phase in range(6):
        on=phase in (1,4);hour=12 if phase<3 else 0
        _,tested,rejected=counters[phase]
        assert (tested>0 and 0<rejected<=tested) if on else tested==rejected==0
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
        assert drive[-1]['z']-group[0]['z']>182 and max(r['speed'] for r in drive)>5
        assert all(b['z']>=a['z'] for a,b in zip(group,group[1:]))
        angles=[math.degrees(math.atan2(r['lookX']-r['eyeX'],r['lookZ']-r['eyeZ'])) for r in drive]
        assert min(angles)<-50 and max(angles)>50
        assert all(sum(r[k] for r in drive)>0 for k in ('envDrawn','paint','probeCapture'))
        assert re.search(rf'NIGHTROADS phase={phase} hour={hour}\.000000 paused=1 chunks=66 lights='+('0' if hour else '[1-9][0-9]*')+r'\b',log)
        assert re.search(rf'TURNPINS phase={phase} count=18\b',log)
        assert re.search(rf'DRIVEPHASE phase={phase} .*valid=1 rows={n} .*variant={6 if on else 0}\b',log)
        bins=[[r for r in drive if lo<=r['z']<lo+20] for lo in range(-60,100,20)]
        assert min(map(len,bins))>=5
        item=dict(phase=phase,on=on,hour=hour,rows=n,gasFrames=len(drive),lastZ=drive[-1]['z'],
                  cameraDegrees=[min(angles),max(angles)],tested=tested,rejected=rejected,
                  captures=sum(r['probeCapture'] for r in drive),
                  minVehicleEnvSubmissions=min(r['envDrawn'] for r in drive),
                  framesWithoutVehicleEnvSubmissions=sum(r['envDrawn']==0 for r in drive))
        if a.physical:
            work=lambda r:(r['whole']-r['pacing'])/294912
            times=list(map(work,drive))
            item.update(meanWorkMs=statistics.mean(times),p95WorkMs=sorted(times)[math.ceil(.95*len(times))-1],
                        maxWorkMs=max(times),entryHz=len(drive)*294912000/sum(r['period'] for r in drive),
                        periodsOver25ms=sum(r['period']>25*294912 for r in drive),
                        spatialMeanWorkMs=[statistics.mean(map(work,b)) for b in bins])
        results.append(item)
    return results

result=validate(rows,text)
controls=[]
for name,key,value in [('stationary','z',-74.303497),('no-gas','gas',0),('freeze','captureFrozen',1),
                       ('cars','vehicles',1),('profiler','coreTicks',1),('paint','paintVu',1),('present','flips',0)]:
    data=copy.deepcopy(rows)
    for r in data:
        if r['gas']:r[key]=value
    try:validate(data,text)
    except AssertionError:controls.append(name)
    else:raise AssertionError('Negative control escaped: '+name)
for name,data,log in [('missing-phase',rows[:-1],text),('completion',rows,text.replace('DRIVEDONE valid=1','DRIVEDONE valid=0')),
                      ('gate-witness',rows,re.sub(r'TARGETCOUNTS[^\r\n]+','',text))]:
    try:validate(data,log)
    except AssertionError:controls.append(name)
    else:raise AssertionError('Negative control escaped: '+name)
report=dict(valid=True,platform=platform,elfSha256=binding['elfSha256'],sourceFiles=len(binding['files']),
            phases=result,negativeControls=controls,csvSha256=sha(folder/'drive-probe.csv'),logSha256=sha(logpath),
            limitations=['Work is elapsed non-pacing, not pure EE arithmetic','No combined VU paint gain',
                         'Gate counts include warm-up; not counts per sampled frame','One order; whole-scene raster qualification remains'])
(a.output or root/f'target-drive-{platform}.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
