"""Validate same-view stationary sweeps before pricing reflection interventions."""
import copy
import importlib.util
import json
import math
import re
import sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('base',Path(__file__).with_name('tyrax2-reflection-isolation-analyze.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
base.ACTIVE.append('paintBlocked')
MODES=[0,5,0] if '--scatter-copy' in sys.argv else [0,4,3,1,0]
def analyze(rows,log):
    assert 'DRIVEDONE valid=1' in log
    assert len(rows)==len(MODES)*840 and {r['phase'] for r in rows}==set(range(len(MODES)))
    assert all(math.isfinite(v) for r in rows for v in r.values())
    records=re.findall(r'DRIVEPHASE phase=(\d+) ttl=(\d+) observed=(\d+) valid=(\d+) rows=(\d+).*?variant=(\d+)',log)
    assert [tuple(map(int,r)) for r in records]==[(p,250,0,1,840,m) for p,m in enumerate(MODES)]
    assert re.findall(r'TURNPINS phase=(\d+) count=(\d+)',log)==[(str(p),'18') for p in range(len(MODES))]
    names=list(rows[0]);disabled=[k for k in names[names.index('bags'):] if k not in base.ACTIVE+['captureFrozen']]
    phases=[]
    for p,m in enumerate(MODES):
        rs=[r for r in rows if r['phase']==p];park,turn=rs[:120],rs[120:]
        assert [r['offset'] for r in rs]==list(range(1200,2040))
        assert all(r['lifetime']==250 and r['observed']==0 and r['flips']==1 for r in rs)
        assert all(r['whole']==r['update']+r['render'] and 0<=r['pacing']<=r['whole'] and r['period']>0 for r in rs)
        assert all(0<r['dt']<.2 and r['vehicles']==3 and r['driver']>=0 and abs(r['speed'])<.05 and r['damage']<.001 for r in rs)
        assert all(abs(r[k]-rows[0][k])<.01 for r in rs for k in ('x','y','z','yaw'))
        assert all(r[k]==0 for r in rs for k in disabled)
        assert all(r['gas']==0 and r['captureFrozen']==0 and r['envSkipped']==0 and r['paintBlocked']==0 for r in park)
        assert all(r['gas']==1 and r['captureFrozen']==int(m==1) for r in turn)
        assert all(r['envEligible']==r['envDrawn']+r['envSkipped'] for r in rs)
        counts=base.stats(turn)['counts']
        assert counts['envEligible']>0
        assert counts['probeCapture']==0 if m==1 else counts['probeCapture']>0
        assert counts['captureBlocked']>0 if m==1 else counts['captureBlocked']==0
        if m==3:assert counts['envDrawn']==counts['paint']==counts['paintBlocked']==0
        else:assert counts['envDrawn']>0 and counts['envSkipped']==0
        if m==4:assert counts['paint']==0 and counts['paintBlocked']>0
        elif m!=3:assert counts['paint']>0 and counts['paintBlocked']==0
        angles=[math.degrees(math.atan2(r['lookX']-r['eyeX'],r['lookZ']-r['eyeZ'])) for r in turn]
        assert max(angles)-min(angles)>100
        if p:
            assert all(abs(r[k]-q[k])<.02 for r,q in zip(rs,rows[:840]) for k in ('eyeX','eyeY','eyeZ','lookX','lookY','lookZ'))
        worst=sorted(turn,key=lambda r:r['whole']-r['pacing'],reverse=True)[:10]
        bins=[dict(tFrom=b*20,**base.stats([r for r in turn if ((r['offset']-1320)%240)//20==b])) for b in range(12)]
        phases.append(dict(phase=p,mode=m,parked=base.stats(park),turn=base.stats(turn),bins=bins,
            worst=[{k:r[k] for k in ('offset','whole','pacing','update','render','paint','probeCapture','envDrawn')} for r in worst]))
    contrasts=[]
    for p in range(1,len(MODES)-1):
        contrasts.append(dict(phase=p,mode=MODES[p],savedMs=[phases[c]['turn']['workMs']-phases[p]['turn']['workMs'] for c in (0,len(MODES)-1)]))
    return dict(phases=phases,contrasts=contrasts)

root=Path(sys.argv[1]);log=Path(sys.argv[2]).read_text(encoding='utf-8',errors='replace')
rows=base.read(root/'drive-probe.csv');result=analyze(rows,log)
if '--selftest' in sys.argv:
    rejected=[]
    cases=[('wrong path',1,'eyeX',999),('car moved',0,'speed',1),('clock leak',0,'coreTicks',1)]
    if len(MODES)==5:cases += [('frozen paint leak',1,'paint',1),('body draw leak',2,'envDrawn',1),('capture leak',3,'probeCapture',1)]
    for name,p,key,v in cases:
        bad=copy.deepcopy(rows);next(r for r in bad if r['phase']==p and r['gas'])[key]=v
        try:analyze(bad,log)
        except AssertionError:rejected.append(name)
        else:raise RuntimeError('Accepted '+name)
    result['negativeControlsRejected']=rejected
(root/'turn-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
for p in result['phases']:print(p['phase'],p['mode'],p['turn'])
print(result['contrasts'])
