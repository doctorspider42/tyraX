"""Validate lean A13 control identity and travel; price only a bound physical run."""
import argparse
import copy
import csv
import hashlib
import json
import math
import re
import statistics
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('fixture',type=Path)
parser.add_argument('--target',choices=['emulator','physical'],default='emulator')
args=parser.parse_args();root=args.fixture.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
plan=json.loads((root/'paint-qualification-manifest.json').read_text())
assert plan['modes'] in ([0,6,0]*2,[6,0,6]*2) and plan['hours']==[12]*3+[0]*3
binding=json.loads((root/'native-binding.json').read_text())
assert plan['hardwareTrace']==plan['frameProfile']==0
for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
for name,flag in [('hardware_trace.hpp','TYRA_HARDWARE_TRACE'),('frame_profile.hpp','TYRA_FRAME_PROFILE')]:
    assert re.search(r'^#define '+flag+r' 0$',(root/'tyra/engine/inc/debug'/name).read_text(),re.M)
production=(root/'production-paint-block.txt').read_text()
assert sha(root/'production-paint-block.txt')==plan['productionPaintSha256']
physics=(root/'game/src/gen/game_physics.gen.cpp').read_text()
start=physics.index('    const int paint = ')
end=physics.index('    if(vehiclePaintFor(objectIndex)) MotionProbe::add(MotionProbe::EnvDrawn);',start)
actual=physics[start:end].replace('MotionProbe::variant()==6 && ','')
actual=actual.replace('      if(part.envTexBag->paintVu) MotionProbe::add(MotionProbe::PaintVu);\n','')
actual=actual.replace('        MotionProbe::add(MotionProbe::Paint);\n','')
assert actual==production,'Paint differs beyond declared control/counter hooks'
assert '(MotionProbe::offset / 240) % 3' not in physics
for rel in binding['files']:
    if rel.endswith(('.cpp','.hpp')):assert 'PaintLife::' not in (root/rel).read_text(errors='replace'),rel
symbols=(root/'symbols.txt').read_text()
assert 'PaintLife::' not in symbols and 'paintOverride' not in symbols
assert 'Tyra::HardwareTrace::active' not in symbols
scene=(root/'game/inc/scene_data.hpp').read_text()
assert re.search(r'CLIP_VU1S\[SCENE_COUNT\] = \{true, true, true\}',scene)
probe=(root/'tyra/engine/inc/debug/motion_probe.hpp').read_text()
assert 'inline bool freezeCapture() { return false; }' in probe
assert 'inline bool skipBodyEnv(){return false;}' in probe

if args.target=='emulator':
    logpath=root/'emulator/emulator.log';csvpath=root/'game/bin/drive-probe.csv'
    launch=json.loads((root/'emulator/launch.json').read_text())
    elf=root/'game/bin/vehicle-playground.elf'
else:
    logpath=root/'physical/console.log';csvpath=root/'physical/bin/drive-probe.csv'
    launch=json.loads((root/'physical/editor-deploy.json').read_text(encoding='utf-8-sig'))
    elf=root/'physical/bin/vehicle-playground.elf'
    assert launch['runner']=='src/runner.cpp::resetPs2Link/deployToPs2'
    pins=json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig'))
    for rel,digest in pins.items():assert sha(root/'physical/bin'/rel)==digest,rel
assert sha(elf)==binding['elfSha256']==launch['elfSha256']
log=logpath.read_text(errors='replace')
rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader(csvpath.open())]

def validate(data,text):
    assert 'DRIVEDONE valid=1' in text,'Incomplete run'
    assert sorted(set(r['phase'] for r in data))==list(range(6))
    result=[]
    for phase in range(6):
        group=[r for r in data if r['phase']==phase];n=len(group)
        assert 120<n<=1200
        assert [r['offset'] for r in group]==list(range(1200,1200+n))
        assert all(math.isfinite(v) and (v>=0 or k in ('x','y','z','yaw','eyeX','eyeY','eyeZ','lookX','lookY','lookZ')) for r in group for k,v in r.items())
        assert all(r['vehicles']==3 and r['driver']>=0 and r['observed']==0 and r['lifetime']==250 for r in group)
        for r in group:
            assert r['gas']==(r['offset']>=1320)
            assert r['whole']==r['update']+r['render'] and r['pacing']<=r['whole'] and r['period']>0
            assert all(r[k]==0 for k in ('captureFrozen','captureBlocked','envSkipped','paintBlocked','coreTicks','partialTicks','scopeErrors'))
            assert r['paintVu']==(r['envDrawn'] if plan['modes'][phase]==6 else 0)
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
        expected_light='0' if phase<3 else '[1-9][0-9]*'
        assert re.search(rf'NIGHTROADS phase={phase} hour={plan["hours"][phase]}\.000000 paused=1 chunks=66 lights={expected_light}\b',text)
        assert re.search(rf'TURNPINS phase={phase} count=18\b',text)
        assert re.search(rf'DRIVEPHASE phase={phase} .*valid=1 rows={n} .*variant={plan["modes"][phase]}\b',text)
        bins=[sum(a<=r['z']<a+20 for r in drive) for a in range(-60,100,20)]
        assert min(bins)>=5,'Insufficient common-distance samples'
        result.append(dict(phase=phase,hour=plan['hours'][phase],mode=plan['modes'][phase],rows=n,
                           gasFrames=len(drive),firstZ=group[0]['z'],lastZ=drive[-1]['z'],
                           cameraDegrees=[min(angles),max(angles)],distanceBinCounts=bins,
                           paintUpdates=sum(r['paint'] for r in drive),envDraws=sum(r['envDrawn'] for r in drive),
                           vuDraws=sum(r['paintVu'] for r in drive),captures=sum(r['probeCapture'] for r in drive)))
    return result

result=validate(rows,log)
controls={}
for name,key,value in [('stationary','z',-74.303497),('no-gas','gas',0),('capture-frozen','captureFrozen',1),('missing-cars','vehicles',1),('scoped-profiler','coreTicks',1)]:
    data=copy.deepcopy(rows)
    for row in data:
        if row['offset']>=1320:row[key]=value
    controls[name]=(data,log)
data=copy.deepcopy(rows)
for r in data:r['paintVu']=0 if plan['modes'][int(r['phase'])]==6 else r['envDrawn']
controls['wrong-paint-path']=(data,log)
controls['missing-phase']=(rows[:-1],log)
controls['missing-completion']=(rows,log.replace('DRIVEDONE valid=1','DRIVEDONE valid=0'))
controls['wrong-mood']=(rows,log.replace('hour=0.000000','hour=12.000000',1))
for name,(data,text) in controls.items():
    try:validate(data,text)
    except AssertionError:pass
    else:raise AssertionError('Negative control escaped: '+name)
report=dict(target=args.target,scope='A13 lean control/workload qualification; NOT proof of sustained physical 60 Hz',
            elfSha256=binding['elfSha256'],sourceFiles=len(binding['files']),phases=result,
            negativeControls=list(controls),csvSha256=sha(csvpath),logSha256=sha(logpath),
            limitations=plan['limitations'])
if args.target=='physical':
    ticks=294912000.0
    for item in result:
        drive=[r for r in rows if r['phase']==item['phase'] and r['gas']]
        work=[(r['whole']-r['pacing'])*1000/ticks for r in drive]
        periods=[r['period']*1000/ticks for r in drive]
        item['physical']=dict(meanWorkMs=statistics.mean(work),p95WorkMs=sorted(work)[math.ceil(.95*len(work))-1],
                              maxWorkMs=max(work),entryHz=len(drive)*1000/sum(periods),maxPeriodMs=max(periods),
                              periodsOver25ms=sum(t>25 for t in periods),flips=sum(r['flips'] for r in drive),
                              spatialMeanWorkMs=[statistics.mean((r['whole']-r['pacing'])*1000/ticks for r in drive if a<=r['z']<a+20) for a in range(-60,100,20)])
(root/('paint-qualification-'+args.target+'.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
