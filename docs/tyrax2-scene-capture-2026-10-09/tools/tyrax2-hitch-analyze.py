"""Validate local hitch scopes and summarize same-route controls."""
import csv
import json
import re
import statistics
import sys
from pathlib import Path

folder, log = map(Path, sys.argv[1:3])
endpoint=108 if '--full' in sys.argv else -40
rows = list(csv.DictReader((folder/'drive-probe.csv').open()))
events = list(csv.DictReader((folder/'hitch-events.csv').open()))
text = log.read_text(encoding='utf-8',errors='replace')
assert 'DRIVEDONE valid=1' in text
phases = re.findall(r'DRIVEPHASE phase=(\d+).*?valid=(\d+) rows=(\d+).*?variant=(\d+)', text)
assert len(phases)==3 and [int(p[0]) for p in phases]==[0,1,2]
assert all(p[1]=='1' and p[3]=='0' for p in phases)
assert len(rows)==sum(int(p[2]) for p in phases)
if 'HITCHPRELOAD' in text:
    assert text.count('HITCHPRELOAD valid=1 offset=1100 phase=1')==1
if 'GROUNDPINS' in text:
    pins=re.findall(r'GROUNDPINS phase=(\d+) count=(\d+) valid=(\d+)',text[:text.index('DRIVEDONE')])
    assert len(pins)==3 and [int(p[0]) for p in pins]==[0,1,2]
    assert all(p[2]=='1' for p in pins)
    assert [int(p[1]) for p in pins]==([18,18,18] if endpoint==108 else [0,18,0])
assert re.search(r'HITCH events='+str(len(events))+r' dropped=0 ',text)
assert events and len(events)<=8192
result={'phases':[], 'inclusiveScopes':True}
lookup={}
for phase in range(3):
    cohort=[r for r in rows if int(r['phase'])==phase]
    assert len(cohort)==int(phases[phase][2])
    assert [int(r['offset']) for r in cohort]==list(range(1200,1200+len(cohort)))
    assert float(cohort[-1]['z'])>=endpoint and all(float(r['z'])<endpoint for r in cohort[:-1])
    for r in cohort:
        assert int(r['whole'])==int(r['update'])+int(r['render'])
        assert int(r['whole'])>=int(r['pacing']) and int(r['flips'])==1
        assert int(r['observed'])==0 and int(r['driver'])>=0 and int(r['vehicles'])==3
        assert abs(float(r['x']))<2 and float(r['damage'])<0.001
        assert int(r['captureFrozen'])==0 and int(r['envSkipped'])==0
        lookup[phase,int(r['offset'])]=r
    moving=[r for r in cohort if int(r['gas'])]
    worst=max(moving,key=lambda r:int(r['whole'])-int(r['pacing']))
    target=min(moving,key=lambda r:abs(float(r['z'])+53.593))
    work=sorted((int(r['whole'])-int(r['pacing']))/294912 for r in moving)
    result['phases'].append(dict(phase=phase,rows=len(cohort),worst=worst,target=target,
        movingRows=len(moving),loopHz=294912000/statistics.mean(int(r['period']) for r in moving),
        workMeanMs=statistics.mean(work),workP95Ms=work[int((len(work)-1)*0.95)],
        targetWorkMs=(int(target['whole'])-int(target['pacing']))/294912,
        workMaxMs=(int(worst['whole'])-int(worst['pacing']))/294912,
        longPeriods=sum(int(r['period'])>25*294912 for r in moving)))
for e in events:
    assert int(e['phase'])==1 and -58<=float(e['z'])<=-49
    r=lookup[1,int(e['offset'])]
    assert abs(float(e['z'])-float(r['z']))<0.001
    assert int(e['begin'])+int(e['duration'])<=int(r['whole'])
worst=result['phases'][1]['worst']
target=min((r for r in rows if int(r['phase'])==1),key=lambda r:abs(float(r['z'])+53.593))
result['targetFrame']=target
selected=[dict(e,elapsedMs=int(e['duration'])/294912) for e in events if e['offset']==target['offset']]
assert selected
result['worstScopes']=sorted(selected,key=lambda e:-e['elapsedMs'])
(folder/'hitch-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
for p in result['phases']:print('phase',p['phase'],'work max',p['workMaxMs'],'z',p['worst']['z'])
for e in result['worstScopes'][:20]:print(e['label'],e['object'],round(e['elapsedMs'],4))
if '--selftest' in sys.argv:
    import subprocess
    import tempfile
    import shutil
    cases={}
    with tempfile.TemporaryDirectory() as tmp:
        dest=Path(tmp)
        for case in ('reference','incomplete','overflow','negativeScope','wrongPhase','missingRow'):
            shutil.copy2(folder/'drive-probe.csv',dest/'drive-probe.csv')
            shutil.copy2(folder/'hitch-events.csv',dest/'hitch-events.csv')
            altered=text
            if case=='incomplete':altered=altered.replace('DRIVEDONE valid=1','DRIVEDONE valid=0')
            if case=='overflow':altered=altered.replace('dropped=0','dropped=1')
            if case in ('negativeScope','wrongPhase'):
                data=[dict(e) for e in events]
                data[0]['begin']='4294967295' if case=='negativeScope' else data[0]['begin']
                if case=='wrongPhase':data[0]['phase']='0'
                with (dest/'hitch-events.csv').open('w',newline='') as f:
                    writer=csv.DictWriter(f,fieldnames=list(events[0]));writer.writeheader();writer.writerows(data)
            if case=='missingRow':
                with (dest/'drive-probe.csv').open('w',newline='') as f:
                    writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows[:-1])
            (dest/'runtime.log').write_text(altered,encoding='utf-8')
            run=subprocess.run([sys.executable,__file__,str(dest),str(dest/'runtime.log')]+
                               (['--full'] if endpoint==108 else []),capture_output=True)
            cases[case]=run.returncode==0
    assert cases=={k:k=='reference' for k in cases},cases
    (folder/'hitch-parser-controls.json').write_text(json.dumps(cases,indent=2)+'\n')
