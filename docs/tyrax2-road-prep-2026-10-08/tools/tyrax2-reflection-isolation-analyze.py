"""Validate intervention activation and real-dt trajectories before pricing."""
import csv
import copy
import hashlib
import json
import math
import re
import statistics as st
import sys
from pathlib import Path
MODES=[0,1,2,3,0,3,2,1,0]
FLOATS='x y z speed yaw dt damage eyeX eyeY eyeZ lookX lookY lookZ'.split()
ACTIVE='probeCapture paint captureBlocked envEligible envDrawn envSkipped'.split()
def read(path):
    with path.open() as f:
        return [{k:float(v) if k in FLOATS else int(v) for k,v in r.items()} for r in csv.DictReader(f)]
def stats(rs):
    work=[(r['whole']-r['pacing'])/294912 for r in rs]
    periods=[r['period']/294912 for r in rs]
    return dict(rows=len(rs),workMs=st.mean(work),p50Ms=st.median(work),
        p95Ms=sorted(work)[int(.95*(len(work)-1))],maxMs=max(work),
        loopHz=294912000/st.mean(r['period'] for r in rs),
        periodP95Ms=sorted(periods)[int(.95*(len(periods)-1))],maxPeriodMs=max(periods),
        presentations=sum(r['flips'] for r in rs),
        workOverField=sum(w>1000/59.94 for w in work),
        longEntryPeriods=sum(r['period']/294912>25 for r in rs),
        counts={k:sum(r[k] for r in rs) for k in ACTIVE})
def analyze(rows,log):
    assert 'DRIVEDONE valid=1' in log
    assert {r['phase'] for r in rows}==set(range(9))
    assert all(math.isfinite(v) for r in rows for v in r.values())
    records=re.findall(r'DRIVEPHASE phase=(\d+) ttl=(\d+) observed=(\d+) valid=(\d+) rows=(\d+) z=([^ ]+) speed=([^ ]+) variant=(\d+)',log)
    assert len(records)==9
    names=list(rows[0]);disabled=names[names.index('bags'):]
    disabled=[k for k in disabled if k not in ACTIVE+['captureFrozen']]
    phases=[]
    for p,mode in enumerate(MODES):
        rs=[r for r in rows if r['phase']==p]
        assert tuple(map(int,records[p][:5]))==(p,250,0,1,len(rs))
        assert int(records[p][-1])==mode
        assert [r['offset'] for r in rs]==list(range(1200,1200+len(rs)))
        assert all(r['lifetime']==250 and r['observed']==0 for r in rs)
        assert all(r['whole']==r['update']+r['render'] and 0<=r['pacing']<=r['whole'] and r['period']>0 for r in rs)
        assert all(0<r['dt']<.2 and r['vehicles']==3 and r['driver']>=0 and abs(r['x'])<2 and r['damage']<.001 for r in rs)
        assert all(r[k]==0 for r in rs for k in disabled)
        parked=rs[:120];drive=rs[120:]
        assert drive and all(r['gas']==0 and r['captureFrozen']==0 and r['envSkipped']==0 and abs(r['speed'])<.5 for r in parked)
        assert all(abs(r['z']-rows[0]['z'])<.1 for r in parked)
        assert all(r['gas']==1 and r['captureFrozen']==int(mode in (1,2)) for r in drive)
        assert rs[-1]['z']>=108 and rs[-2]['z']<108
        assert all(r['envEligible']==r['envDrawn']+r['envSkipped'] for r in rs)
        assert sum(r['envEligible'] for r in drive)>0
        if mode in (1,2):
            assert sum(r['probeCapture'] for r in drive)==0
            assert sum(r['captureBlocked'] for r in drive)>0
        else:
            assert sum(r['probeCapture'] for r in drive)>0
            assert sum(r['captureBlocked'] for r in drive)==0
        if mode in (2,3):
            assert sum(r['envDrawn']+r['paint'] for r in drive)==0
        else:
            assert sum(r['envSkipped'] for r in drive)==0
            assert sum(r['envDrawn'] for r in drive)>0
        bins=[]
        for b in sorted({math.floor(r['z']/20) for r in drive}):
            bins.append(dict(zFrom=b*20,zTo=(b+1)*20,**stats([r for r in drive if math.floor(r['z']/20)==b])))
        worst=sorted(drive,key=lambda r:r['whole']-r['pacing'],reverse=True)[:8]
        phases.append(dict(phase=p,mode=mode,parked=stats(parked),drive=stats(drive),bins=bins,
            worst=[{k:r[k] for k in ['offset','z','whole','pacing','update','render','probeCapture','paint']} for r in worst]))
    contrasts=[]
    for a,b,c,d,e in [(0,1,2,3,4),(8,7,6,5,4)]:
        t=lambda i:phases[i]['drive']['workMs']
        contrasts.append(dict(normalPhases=[a,e],freezePhase=b,offPhase=c,bodyOffPhase=d,
            freezeSavedMs=[t(a)-t(b),t(e)-t(b)],bodyOnFrozenSavedMs=t(b)-t(c),
            bodyOnLiveSavedMs=[t(a)-t(d),t(e)-t(d)],bothOffSavedMs=[t(a)-t(c),t(e)-t(c)]))
    return dict(phases=phases,contrasts=contrasts,scope='same-bake real-dt road-free drive; elapsed minus pacing, not pure EE')
if __name__=='__main__':
    root=Path(sys.argv[1]);log=Path(sys.argv[2]).read_text(errors='replace')
    rows=read(root/'drive-probe.csv')
    result=analyze(rows,log)
    if '--selftest' in sys.argv[3:]:
        rejected=[]
        for name,phase,key,value in [('frozen capture leak',1,'probeCapture',1),
                ('body skip leak',2,'envDrawn',1),('scope clock leak',0,'coreTicks',1),
                ('invalid time balance',0,'whole',0),('wrong freeze gate',0,'captureFrozen',1),
                ('damaged trajectory',0,'damage',1)]:
            bad=copy.deepcopy(rows)
            next(r for r in bad if r['phase']==phase and r['gas']==1)[key]=value
            try: analyze(bad,log)
            except AssertionError: rejected.append(name)
            else: raise RuntimeError('Accepted corrupt evidence: '+name)
        try: analyze(rows,log.replace('DRIVEDONE valid=1','DRIVEDONE valid=0'))
        except AssertionError: rejected.append('missing completion')
        else: raise RuntimeError('Accepted missing completion')
        result['negativeControlsRejected']=rejected
    result['csvSha256']=hashlib.sha256((root/'drive-probe.csv').read_bytes()).hexdigest()
    (root/'reflection-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result['contrasts'],indent=2))
