"""Strict fixed-view plan comparison; emulator timings are never PS2 prices."""
import csv
import hashlib
import json
import re
import statistics
import sys
from pathlib import Path

MODES=[0,1,0,2,0,2,1,0,1,0,2,0]
def analyze(rows,log,oracle=False):
    assert len(rows)==36*180,len(rows)
    assert all(set(r)==set(rows[0]) and all(v>=0 for v in r.values()) for r in rows)
    phases=[]
    for p in range(36):
        rs=rows[p*180:(p+1)*180];mode=MODES[p%12];observed=p%12<6
        assert [r['frame'] for r in rs]==list(range(p*540+300,p*540+480))
        assert all(r['phase']==p and r['view']==p//12 and r['variant']==mode and r['observed']==observed for r in rs)
        assert all(r['whole']==r['update']+r['render'] and r['pacing']<=r['whole'] and r['period']>0 and r['errors']==0 for r in rs)
        assert all(0<r['resident']<=1024*1024 and 0<r['slots']<=512 for r in rs)
        if observed:
            assert all(r['coreTicks']>0 and r['coreTicks']<=r['render'] and r['coreCalls']==r['bags']>0 for r in rs)
            assert all(r['misses']==0 for r in rs),'Warm plan misses'
            assert all(r['descHits']==(r['slots'] if mode else 0) for r in rs)
            assert all(r['viewHits']==(r['slots'] if mode==2 else 0) for r in rs),'Frozen-view reuse inactive'
            if oracle and mode:assert all(r['checks']==r['slots'] for r in rs)
        else:
            unobserved='coreTicks coreCalls descHits viewHits misses checks errors in out partial'.split()+list(rows[0])[23:]
            assert all(all(r[k]==0 for k in unobserved) for r in rs),'Observer leakage'
        work=[(r['whole']-r['pacing'])/294912 for r in rs]
        phases.append(dict(phase=p,view=p//12,variant=mode,observed=observed,
            workMean=statistics.mean(work),workP95=sorted(work)[170],workMax=max(work),
            loopHz=1000/(statistics.mean(r['period'] for r in rs)/294912),
            coreMs=statistics.mean(r['coreTicks'] for r in rs)/294912,
            means={k:statistics.mean(r[k] for r in rs) for k in rows[0]}))
    semantic='bags outside direct replay partialBags packages in out partial slots expandVerts copyBytes snapshotBytes partialReplay partialBytes expandCachedVerts'.split()
    for view in range(3):
        ref=phases[view*12]['means']
        for p in phases[view*12:view*12+6]:
            assert all(p['means'][k]==ref[k] for k in semantic),(p['phase'],{k:[ref[k],p['means'][k]] for k in semantic if p['means'][k]!=ref[k]})
    records=[tuple(map(int,m)) for m in re.findall(r'STATICPHASE phase=(\d+) view=(\d+) variant=(\d+) valid=(\d+) checks=(\d+) errors=(\d+) owners=(\d+) resident=(\d+)',log)]
    assert len(records)==36,len(records)
    previous=0
    for p,v,m,valid,checks,errors,owners,resident in records:
        assert p==records.index((p,v,m,valid,checks,errors,owners,resident)) and v==p//12 and m==MODES[p%12]
        assert valid==1 and errors==0 and 0<owners<=128 and 0<resident<=1024*1024
        assert checks>previous if m else (checks>=previous if oracle else checks==previous)
        previous=checks
    assert re.search(r'STATICDONE valid=1 checks=\d+ errors=0',log)
    assert not re.search(r'(?:PARTIALCHECK|EXPANDCHECK)[^\n]*failed=[1-9]',log)
    views=re.findall(r'STATICVIEW phase=(\d+) eye=([^\s]+) look=([^\s]+) hour=([^\s]+) paused=(\d+)',log)
    assert len(views)==36
    for p,row in enumerate(views):
        assert int(row[0])==p and row[4]=='1'
        assert row[1:]==views[(p//12)*12][1:],'Camera/clock drift'
    contrasts=[]
    for view in range(3):
        ps=phases[view*12:(view+1)*12]
        contrasts.append(dict(view=view,
            descriptorSavedMs=[ps[7]['workMean']-ps[i]['workMean'] for i in (6,8)],
            frozenSavedMs=[ps[i]['workMean']-ps[10]['workMean'] for i in (9,11)],
            descriptorCoreSavedMs=[ps[i]['coreMs']-ps[1]['coreMs'] for i in (0,2)],
            frozenCoreSavedMs=[ps[4]['coreMs']-ps[i]['coreMs'] for i in (3,5)]))
    return dict(phases=phases,contrasts=contrasts,checks=previous,oracle=oracle,
        units='EE Count /294912 ms; emulator is correctness only; scopes are elapsed not pure EE')

if __name__=='__main__':
    root=Path(sys.argv[1]);source=root/'static-plan.csv';log=Path(sys.argv[2]).read_text(errors='replace')
    rows=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(source.open())]
    result=analyze(rows,log,'--oracle' in sys.argv[3:])
    result['sourceSha256']=hashlib.sha256(source.read_bytes()).hexdigest()
    (root/'static-plan-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result['contrasts'],indent=2))
