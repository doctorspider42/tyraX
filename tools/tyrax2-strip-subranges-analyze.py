"""Strict same-ELF terrain subrange A/B/A and B/A/B comparison."""
import hashlib
import importlib.util
import json
import math
import re
import statistics as st
import sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('producer',Path(__file__).with_name('tyrax2-producer-analyze.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)

def analyze(rows,bins,log,oracle=False):
    metrics=list(rows[0])[29:];sub=[k for k in metrics if k.startswith('sub')]
    assert {r['phase'] for r in rows}==set(range(12))
    assert all(math.isfinite(v) for r in rows for v in r.values())
    assert 'PRODUCERDONE valid=1' in log
    records=re.findall(r'PRODUCERPHASE phase=(\d+) workload=(\d+) mode=(\d+) valid=(\d+) rows=(\d+)',log)
    witnesses=re.findall(r'SUBPHASE phase=(\d+) candidate=(\d+) checks=(\d+) failed=(\d+) bytes=(\d+)',log)
    assert len(records)==len(witnesses)==12
    assert not re.search(r'(?:PARTIALCHECK|EXPANDCHECK)[^\n]*failed=[1-9]',log)
    if oracle:
        assert re.search(r'PARTIALCHECK[^\n]*checked=[1-9]',log)
        assert re.search(r'EXPANDCHECK[^\n]*checked=[1-9]',log)
    result=[]
    for p in range(12):
        rs=[r for r in rows if r['phase']==p];bs=[r for r in bins if r['phase']==p]
        mode=p%6;drive=p>=6;candidate=mode in (1,3,5);observed=mode<3
        assert tuple(map(int,records[p]))==(p,int(drive),mode,1,len(rs))
        wp,wb,checks,failed,memory=map(int,witnesses[p]);assert (wp,wb,failed)==(p,int(candidate),0)
        assert memory>0 and (not oracle or not candidate or checks>int(witnesses[p-1][2]))
        warm=1200 if drive or p==0 else 360
        assert [r['offset'] for r in rs]==list(range(warm,warm+len(rs)))
        assert all(r['mode']==mode and r['workload']==drive and r['errors']==0 for r in rs)
        assert all(r['whole']==r['update']+r['render'] and 0<=r['pacing']<=r['whole'] and r['period']>0 for r in rs)
        assert all(0<r['dt']<.2 and r['vehicles']==(3 if drive else 0) for r in rs)
        assert all(r[k]==0 for r in rs for k in base.CLOCKS)
        if drive:
            assert all(r['driver']>=0 and abs(r['x'])<2 and r['damage']<.001 for r in rs)
            assert rs[-1]['z']>=108 and rs[-2]['z']<108
            assert [r['segment'] for r in rs]==[0]*120+[1]*(len(rs)-120)
            assert all(abs(r['speed'])<.5 and abs(r['z']+74.3035)<1 for r in rs[:120])
        else:
            assert len(rs)==600 and [r['angle'] for r in rs]==[0]*120+list(range(360))+[0]*120
            assert [r['segment'] for r in rs]==[0]*120+[1]*360+[2]*120
            assert all(r['driver']==-1 and abs(r['eyeZ']+74.3035)<.001 for r in rs)
        def key(r):return 0 if r['segment']==0 else 13 if r['segment']==2 else base.key_for_report(r,drive)
        assert len(bs)==36*len({key(r) for r in rs})
        assert len({(b['bin'],b['producer'],b['view']) for b in bs})==len(bs)
        for b in bs:
            assert 0<=b['producer']<9 and 0<=b['view']<4
            assert all(b[k]>=0 for k in metrics)
            assert b['rows']==sum(key(r)==b['bin'] for r in rs)
            assert b['deferredPush']==b['deferredPop']
            if b['snapshotBytes']:assert (b['producer'],b['view'])==(0,3)
            if any(b[k] for k in sub):assert b['producer']==1 and candidate and observed
        for b in {key(r) for r in rs}:
            for k in metrics:assert sum(r[k] for r in rs if key(r)==b)==sum(r[k] for r in bs if r['bin']==b),(p,b,k)
        if not observed:assert all(r[k]==0 for r in rs for k in metrics)
        else:
            assert all(r['bags']>0 for r in rs)
            if candidate:
                assert sum(r['subCalls'] for r in rs)>0
                assert sum(r['subCullTris'] for r in rs)>0 and sum(r['subOutsideTris'] for r in rs)>0
            else:assert all(r[k]==0 for r in rs for k in sub)
        segments=[]
        for seg in sorted({r['segment'] for r in rs}):
            group=[r for r in rs if r['segment']==seg];work=[(r['whole']-r['pacing'])/294912 for r in group]
            segments.append(dict(segment=seg,rows=len(group),workMs=st.mean(work),p95Ms=sorted(work)[int(.95*(len(work)-1))],loopHz=294912000/st.mean(r['period'] for r in group),
                means={k:st.mean(r[k] for r in group) for k in metrics}))
        spatial=[]
        for b in sorted({key(r) for r in rs if r['segment']==1}):
            group=[r for r in rs if key(r)==b]
            spatial.append(dict(bin=b,rows=len(group),workMs=st.mean(r['whole']-r['pacing'] for r in group)/294912))
        result.append(dict(phase=p,candidate=candidate,observed=observed,segments=segments,bins=spatial))
    # Same-pose rotation semantics and restored original-route counts.
    stable='bags outside direct replay partial packages probeCapture paint rebuild'.split()
    ref=[r for r in rows if r['phase']==0]
    for p in (1,2):
        for a,b in zip(ref,[r for r in rows if r['phase']==p]):
            assert all(a[k]==b[k] for k in stable),(p,a['angle'])
    for k in ('cullChunks','clipChunks','cullVerts','clipVerts'):
        assert [r[k] for r in rows if r['phase']==0]==[r[k] for r in rows if r['phase']==2]
    contrasts=[]
    for offset in (0,6):
        means=[result[offset+i]['segments'][1]['workMs'] for i in range(6)]
        contrasts.append(dict(workload='rotation' if offset==0 else 'drive',
            countedSavedMs=[means[0]-means[1],means[2]-means[1]],
            countersOffSavedMs=[means[4]-means[3],means[4]-means[5]]))
    return dict(phases=result,contrasts=contrasts,oracle=oracle,
        checks=int(witnesses[-1][2]),metadataBytes=int(witnesses[-1][4]))

if __name__=='__main__':
    root=Path(sys.argv[1]);log=Path(sys.argv[2]).read_text(errors='replace')
    result=analyze(base.read(root/'producer-probe.csv'),base.read(root/'producer-bins.csv'),log,'--oracle' in sys.argv[3:])
    result['sha256']={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ('producer-probe.csv','producer-bins.csv')}
    (root/'subrange-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result['contrasts'],indent=2))
