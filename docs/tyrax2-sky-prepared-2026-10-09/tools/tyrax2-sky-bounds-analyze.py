"""Strict same-ELF A03 sky-only A/B/A validation, not a raster oracle."""
import copy
import csv
import json
import math
import re
import statistics
import sys
from pathlib import Path
MODES=[0,6,0,0,6,0]
def validate(rows,log):
    assert 'DRIVEDONE valid=1' in log
    phases=re.findall(r'DRIVEPHASE phase=(\d+) ttl=(\d+) observed=(\d+) valid=(\d+) rows=(\d+).*?variant=(\d+)',log)
    assert len(phases)==6
    oracle=re.findall(r'SKYORACLE phase=(\d+) checks=(\d+) failed=(\d+) safe=(\d+) outside=(\d+)',log)
    assert len(oracle)==6
    assert all(int(p)==i and int(c)>0 and int(f)==0 and int(s)>0 and int(o)>0 for i,(p,c,f,s,o) in enumerate(oracle))
    assert re.findall(r'TURNPINS phase=(\d+) count=(\d+)',log)==[(str(i),'18') for i in range(6)]
    assert re.findall(r'NIGHTROADS phase=(\d+) hour=([\d.]+) paused=(\d+) chunks=(\d+) lights=(\d+)',log)==[(str(i),'12.000000','1','66','0') for i in range(6)]
    assert all(math.isfinite(v) for r in rows for v in r.values())
    for p,m in enumerate(MODES):
        rs=[r for r in rows if r['phase']==p]
        assert tuple(map(int,phases[p]))==(p,250,0,1,len(rs),m)
        assert [r['offset'] for r in rs]==list(range(1200,1200+len(rs)))
        assert all(r['whole']==r['update']+r['render'] and 0<=r['pacing']<=r['whole'] and r['flips']==1 and
                   0<r['dt']<.2 and r['vehicles']==3 and r['driver']>=0 and r['damage']<.001 and abs(r['x'])<2 and
                   r['scopeErrors']==0 and r['captureFrozen']==0 and r['envSkipped']==0 and r['paintBlocked']==0 and
                   r['paintVu']==r['envDrawn'] and r['envEligible']==r['envDrawn'] and r['bags']==0 for r in rs)
        assert all(r[k]>=0 and r[k]==int(r[k]) for r in rs for k in ('skyEligible','skySafe','skyOutside','skyApplied','skyClipCalls','skyCullCalls'))
        assert all(sum(r[k] for r in rs)>0 for k in ('skyEligible','skyClipCalls','skyCullCalls'))
        assert all(r['skyApplied']==r['skySafe']+r['skyOutside'] and
                   r['skyClipCalls']+r['skyApplied']==r['skyEligible'] for r in rs)
        if m==0:assert all(r['skyApplied']==0 for r in rs)
        else:assert sum(r['skySafe'] for r in rs)>0 and sum(r['skyOutside'] for r in rs)>0
        if p<3:
            assert len(rs)==840 and all(abs(r['z']+74.3035)<.01 and abs(r['speed'])<.05 for r in rs)
            if p:assert all(abs(a[k]-b[k])<.02 for a,b in zip(rs,rows[:840]) for k in ('eyeX','eyeY','eyeZ','lookX','lookY','lookZ'))
        else:assert rs[-1]['z']>=108 and rs[-2]['z']<108
    return oracle
def main():
    root,logfile=map(Path,sys.argv[1:3])
    rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader((root/'drive-probe.csv').open())]
    log=logfile.read_text(errors='replace');oracle=validate(rows,log);rejected=[]
    for label,key,value in [('unapplied candidate','skyApplied',0),('lost clip','skyClipCalls',999),('bad pose','vehicles',0)]:
        bad=copy.deepcopy(rows);next(r for r in bad if r['phase']==1)[key]=value
        try:validate(bad,log)
        except AssertionError:rejected.append(label)
        else:raise RuntimeError('Accepted '+label)
    try:validate(rows,log.replace('failed=0','failed=1',1))
    except AssertionError:rejected.append('failed oracle')
    else:raise RuntimeError('Accepted failed oracle')
    result=dict(phases=[],oracle=oracle,negativeControls=rejected,scope='private sky-only bounds; counts do not establish raster equivalence')
    for p in range(6):
        rs=[r for r in rows if r['phase']==p and r['offset']>=1320]
        work=[(r['whole']-r['pacing'])/294912 for r in rs]
        result['phases'].append(dict(phase=p,mode=MODES[p],frames=len(rs),workMs=statistics.mean(work),
            p95Ms=sorted(work)[int(.95*(len(work)-1))],maxMs=max(work),loopHz=294912000/statistics.mean(r['period'] for r in rs),
            counts={k:sum(r[k] for r in rs) for k in ('probeCapture','skyEligible','skySafe','skyOutside','skyApplied','skyCullCalls','skyClipCalls')}))
    (root/'sky-analysis.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
