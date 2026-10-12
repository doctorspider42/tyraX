"""Validate A12 actual road travel, moving paint and lifecycle; no timing verdict."""
import copy
import csv
import hashlib
import json
import math
import re
import subprocess
import sys
from pathlib import Path

root=Path(sys.argv[1]).resolve()
subprocess.run([sys.executable,str(Path(__file__).with_name('tyrax2-paint-lifecycle-analyze.py')),str(root)],check=True)
plan=json.loads((root/'paint-drive-manifest.json').read_text())
assert plan['hours']==[12,0,12] and plan['gasStart']==1320 and plan['endZ']==108
csvpath=root/'game/bin/drive-probe.csv'
rows=[{k:float(v) for k,v in row.items()} for row in csv.DictReader(csvpath.open())]
log=(root/'emulator/emulator.log').read_text(errors='replace')
moving=[tuple(map(int,m)) for m in re.findall(r'PAINTDRIVE phase=(\d+) visits=(\d+) vu=(\d+) ee=(\d+)',log)]
assert len(moving)==3 and [r[0] for r in moving]==[0,1,2]
for phase,visits,vu,ee in moving:
    assert visits==vu+ee and min(vu,ee)>0

def validate(data):
    assert sorted(set(r['phase'] for r in data))==[0,1,2]
    results=[]
    for phase in range(3):
        group=[r for r in data if r['phase']==phase]
        n=len(group)
        assert 120<n<=plan['maxRows']
        assert [r['offset'] for r in group]==list(range(1200,1200+n))
        assert all(math.isfinite(v) for r in group for v in r.values())
        assert all(r['vehicles']==3 and r['driver']>=0 and r['captureFrozen']==0 for r in group)
        assert all(r['gas']==(r['offset']>=1320) for r in group)
        assert all(abs(r['x'])<1 and abs(r['yaw'])<1 for r in group), 'Not the straight-road route'
        park,drive=group[:120],group[120:]
        assert all(abs(r['speed'])<.001 for r in park)
        assert abs(group[0]['z']+74.303497)<.1
        assert drive[-1]['z']>=108 and all(r['z']<108 for r in drive[:-1])
        assert drive[-1]['z']-park[0]['z']>182
        assert max(r['speed'] for r in drive)>5
        assert all(b['z']>=a['z'] for a,b in zip(group,group[1:]))
        angles=[math.degrees(math.atan2(r['lookX']-r['eyeX'],r['lookZ']-r['eyeZ'])) for r in drive]
        assert min(angles)<-50 and max(angles)>50, 'Camera sweep missing during travel'
        summary=re.search(rf'DRIVEPHASE phase={phase} .*valid=1 rows=(\d+) z=([-.\d]+) speed=([-.\d]+)',log)
        assert summary and int(summary[1])==n and float(summary[2])>=108
        results.append(dict(phase=phase,hour=plan['hours'][phase],rows=n,gasFrames=len(drive),
                            firstZ=group[0]['z'],lastZ=drive[-1]['z'],maxSpeed=max(r['speed'] for r in drive),
                            cameraDegrees=[min(angles),max(angles)],movingPaint=dict(zip(['visits','vu','ee'],moving[phase][1:]))))
    return results

results=validate(rows)
mutations={}
for name,key,value in [('stationary','z',-74.303497),('no-gas','gas',0),('missing-cars','vehicles',1),('frozen-capture','captureFrozen',1)]:
    data=copy.deepcopy(rows)
    for row in data:
        if row['offset']>=1320:row[key]=value
    mutations[name]=data
mutations['missing-row']=rows[:-1]
for name,data in mutations.items():
    try:validate(data)
    except AssertionError:pass
    else:raise AssertionError('Negative control escaped: '+name)
result=dict(scope='A12 full-throttle day/night/day correctness only; no performance or paired raster verdict',
            phases=results,negativeControls=list(mutations),csvSha256=hashlib.sha256(csvpath.read_bytes()).hexdigest(),
            lifecycle=json.loads((root/'paint-lifecycle-analysis.json').read_text()))
(root/'paint-drive-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
