"""Validate P06 RAM ledgers before comparing individual road/transport windows."""
import collections
import copy
import csv
import json
import re
import statistics as st
import subprocess
import sys
from pathlib import Path

fixture,root,logpath=map(Path,sys.argv[1:4])
subprocess.run([sys.executable,str(Path(__file__).with_name('tyrax2-reflection-turn-analyze.py')),
               str(root),str(logpath),'--vu-paint','--roads-day','--road-prep-scopes','--road-prefix','--selftest'],check=True)
def read(path):
    with path.open() as f:return [{k:float(v) for k,v in row.items()} for row in csv.DictReader(f)]
main=read(root/'drive-probe.csv');frames=read(root/'road-frames.csv');bags=read(root/'road-bags.csv')
header=(fixture/'tyra/engine/inc/debug/motion_probe.hpp').read_text()
q=(fixture/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_text()
for name in ('ExpandBuilds','ExpandCachedVerts','PartialBuild','PartialReplay','PartialBytes'):
    assert q.count('MotionProbe::add(MotionProbe::'+name)==1,('missing hook',name)
names=[s.strip() for s in re.search(r'enum Counter \{(.*?)\};',header,re.S)[1].split(',')][:-1]
metrics=list(frames[0])[2:]
windows=set(1320+cycle*240+t for cycle in range(3) for start in (12,24,180) for t in range(start,start+8))

def validate(fs,bs):
    assert len(fs)==2520 and [(r['phase'],r['offset']) for r in fs]==[(r['phase'],r['offset']) for r in main]
    assert all(v>=0 and v==int(v) for row in fs+bs for v in row.values())
    assert all(r['errors']==0 for r in fs)
    assert all(r[k]==0 for r in fs if r['phase']!=1 for k in metrics)
    on=[r for r in fs if r['phase']==1]
    assert all(r['submitCalls']>0 and r['submitQwords']>0 and r['flushCalls']>0 and r['flushQwords']>0 and r['waitCalls']>0 for r in on)
    assert len(bs)>0 and len(bs)<4096 and {r['offset'] for r in bs}==windows
    assert all(r['phase']==1 and r['id']>0 and r['view'] in (0,1,2) and r['vertices']>0 and 0<=r['wait']<=r['elapsed'] for r in bs)
    assert all(r['c'+str(names.index('Bags'))]==1 for r in bs)
    total=lambda name:sum(r['c'+str(names.index(name))] for r in bs)
    assert total('ClipChunks')>0 and total('ExpandCachedVerts')+total('ExpandVerts')>0
    assert total('Replay')+total('PartialReplay')+total('PartialBuild')>0
    assert all(sum(r[k] for k in ('vifWait','gsWait','otherWait'))<=m['whole']-m['pacing'] for r,m in zip(fs,main))
    byoffset=collections.defaultdict(float)
    calls=collections.Counter(r['offset'] for r in bs)
    for r in bs:byoffset[r['offset']]+=r['elapsed']
    for r in main:
        if r['phase']==1 and r['offset'] in windows:
            assert calls[r['offset']]==r['coreCalls']
            assert byoffset[r['offset']]>0 and byoffset[r['offset']]<=r['whole']-r['pacing']

log=logpath.read_text(errors='replace')
assert re.findall(r'ROADTRACE phase=(\d+) interleave=(\d+) normalDrawing=(\d+)',log)==[('0','2','1'),('1','2','1'),('2','2','1')]
validate(frames,bags)
rejected=[]
for label,which,key,value in [('off leakage','frames','submitCalls',1),('wait accounting','frames','vifWait',1e10),
                             ('lost ID','bags','id',0),('nested waits','frames','errors',1),('wrong view','bags','view',8)]:
    fs,bs=copy.deepcopy(frames),copy.deepcopy(bags)
    (fs if which=='frames' else bs)[840 if which=='frames' and key in ('vifWait','errors') else 0][key]=value
    try:validate(fs,bs)
    except AssertionError:rejected.append(label)
    else:raise RuntimeError('Accepted '+label)
try:validate(frames,bags[:-1]+[dict(bags[-1],offset=1400)])
except AssertionError:rejected.append('unexpected window')
else:raise RuntimeError('Accepted unexpected window')

index={(r['phase'],r['offset']):r for r in main}
def frame_summary(rs):
    n=len(rs);ms=lambda k:sum(r[k] for r in rs)/n/294912
    originals=[index[r['phase'],r['offset']] for r in rs]
    work=st.mean((r['whole']-r['pacing'])/294912 for r in originals)
    return dict(rows=n,workMs=work,waitMs=sum(ms(k) for k in ('vifWait','gsWait','otherWait')),
                roadCoreMs=st.mean(r['coreTicks']/294912 for r in originals),
                captureFrames=sum(r['probeCapture'] for r in originals),
                metrics={k:ms(k) if k.endswith('Wait') else st.mean(r[k] for r in rs) for k in metrics})
result=dict(scope='original roads; explicit waits are not all EE stalls; road-triggered queue can mix owners',
            negativeControlsRejected=rejected,phases=[],windows=[])
for phase in range(3):result['phases'].append(dict(phase=phase,**frame_summary([r for r in frames if r['phase']==phase and r['offset']>=1320])))
for cycle in range(3):
    for start in (12,24,180):
        first=1320+240*cycle+start;selected=[r for r in bags if first<=r['offset']<first+8]
        grouped=collections.defaultdict(list)
        for r in selected:grouped[int(r['id']),int(r['view'])].append(r)
        roads=[]
        for (id,view),rs in grouped.items():
            roads.append(dict(id=id,view=view,calls=len(rs),elapsedMs=sum(r['elapsed'] for r in rs)/8/294912,
                vertices=sum(r['vertices'] for r in rs)/8,
                counters={name:sum(r['c'+str(i)] for r in rs)/8 for i,name in enumerate(names) if any(r['c'+str(i)] for r in rs)}))
        result['windows'].append(dict(offset=first,**frame_summary([r for r in frames if r['phase']==1 and first<=r['offset']<first+8]),
            controls=[dict(phase=p,**frame_summary([r for r in frames if r['phase']==p and first<=r['offset']<first+8])) for p in (0,2)],
            roads=sorted(roads,key=lambda r:r['elapsedMs'],reverse=True)))
(root/'road-trace-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='windows'},indent=2))
for w in result['windows']:print(w['offset'], 'work',w['workMs'],'road',w['roadCoreMs'],'wait',w['waitMs'],'capture',w['captureFrames'],'top',w['roads'][:2])
