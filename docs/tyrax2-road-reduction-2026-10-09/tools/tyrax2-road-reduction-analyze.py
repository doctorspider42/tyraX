"""A06 accounting, immutable draw selection and unchanged sky policy gates."""
import copy
import csv
import importlib.util
import json
import re
import statistics
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
fixture,root,logfile=map(Path,sys.argv[1:4])
spec=importlib.util.spec_from_file_location('sky',ROOT/'tyrax2-sky-bounds-analyze.py')
sky=importlib.util.module_from_spec(spec);spec.loader.exec_module(sky)
plans={p['id']:p for p in json.loads((fixture/'road-reduction-manifest.json').read_text())['plans']}
rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader((root/'drive-probe.csv').open())]
log=logfile.read_text(errors='replace')
def validate(data,text):
    sky.validate(data,text,sky_refine=False)
    builds=re.findall(r'ROADREDUCE phase=(\d+) offset=(\d+) id=(\d+) source=(\d+) reduced=(\d+) applied=(\d+) matched=(\d+)',text)
    assert builds
    seen=set()
    for build in builds:
        phase,offset,i,n,size,applied,matched=map(int,build)
        assert phase<6 and offset<1000 and i in plans and matched==1
        assert n==plans[i]['count'] and size==plans[i]['size'] and applied==int(phase in (1,4))
        seen.add((phase,i))
    assert seen=={(p,i) for p in range(6) for i in plans}
    for p in range(6):
        rs=[r for r in data if r['phase']==p]
        assert sum(r['roadCalls'] for r in rs)>0
        assert sum(r['roadCullCalls']+r['roadClipCalls'] for r in rs)>0
        for r in rs:
            assert all(r[k]>=0 and r[k]==int(r[k]) for k in ('roadCalls','roadSourceVerts','roadDrawVerts','roadReducedCalls','roadCullCalls','roadClipCalls'))
            if p in (1,4):
                assert r['roadCalls']==r['roadReducedCalls']
                if r['roadCalls']:assert 0<r['roadDrawVerts']<r['roadSourceVerts']
            else:assert r['roadReducedCalls']==0 and r['roadDrawVerts']==r['roadSourceVerts']
    return builds
builds=validate(rows,log);controls=[]
for name,key,value in [('missing consumption','roadReducedCalls',0),('lost source','roadSourceVerts',0),('sky accidentally enabled','skyApplied',1)]:
    bad=copy.deepcopy(rows);next(r for r in bad if r['phase']==1 and r['roadCalls']>0)[key]=value
    try:validate(bad,log)
    except AssertionError:controls.append(name)
    else:raise RuntimeError('Accepted '+name)
try:validate(rows,log.replace('matched=1','matched=0',1))
except AssertionError:controls.append('source mismatch')
else:raise RuntimeError('Accepted source mismatch')
result=dict(negativeControls=controls,builds=builds,phases=[],scope='Private draw-only reduction; no raster/depth equivalence claim')
for p in range(6):
    rs=[r for r in rows if r['phase']==p and r['offset']>=1320]
    work=[(r['whole']-r['pacing'])/294912 for r in rs]
    result['phases'].append(dict(phase=p,frames=len(rs),workMs=statistics.mean(work),p95Ms=sorted(work)[int(.95*(len(work)-1))],
        maxMs=max(work),loopHz=294912000/statistics.mean(r['period'] for r in rs),
        counts={k:sum(r[k] for r in rs) for k in ('roadCalls','roadSourceVerts','roadDrawVerts','roadReducedCalls','roadCullCalls','roadClipCalls','probeCapture')}))
(root/'road-reduction-analysis.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
