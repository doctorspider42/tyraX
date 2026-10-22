"""A02b reconciliation and conservative offline classification of actual clip inputs."""
import collections
import copy
import csv
import json
import itertools
import math
import re
import statistics
import struct
import sys
from pathlib import Path

OWNERS='unknown terrain roads batches objects vehicles effects sky procedural shadows lights'.split()
PASSES='unspecified base layer ao groundShadow emission environment shadow wheel glass skyBody proxy light effect'.split()
VIEWS='main reflection secondary'.split()
WINDOWS={1:{1300,1336,1337,1576,1577},4:{1380,1381,1500,1501}}

def read(path):
    with path.open() as f:return [{k:float(v) for k,v in r.items()} for r in csv.DictReader(f)]

def f32(u):return struct.unpack('<f',struct.pack('<I',int(u)))[0]

def classify(vertices,planes):
    ds=[[sum(p[k]*v[k] for k in range(3))+p[3] for v in vertices] for p in planes]
    margins=[1e-5+1e-5*(abs(p[3])+sum(abs(p[k])*max(abs(v[k]) for v in vertices) for k in range(3))) for p in planes]
    if all(min(d)>m for d,m in zip(ds,margins)):return 'safe8'
    if any(max(d)<-m for d,m in list(zip(ds,margins))[:6]):return 'outside6'
    return 'boundaryOrUncertain'

def validate(rows,bags,clips,names,log):
    assert len(re.findall(r'LOG: DRIVEPHASE phase=\d+ .*?valid=1 ',log))==6
    assert 'DRIVEDONE valid=1' in log
    reports=re.findall(r'SCENECAPTURE phase=(\d+) bags=(\d+) clips=(\d+) errors=(\d+)',log)
    assert len(reports)==6 and all(int(r[3])==0 for r in reports)
    assert {int(r['phase']) for r in rows}==set(range(6))
    assert all(math.isfinite(v) for r in rows+bags+clips for v in r.values())
    counter=lambda r,name:r['c'+str(names.index(name))]
    for p in range(6):
        rs=[r for r in rows if r['phase']==p];bs=[r for r in bags if r['phase']==p];cs=[r for r in clips if r['phase']==p]
        assert [int(r['offset']) for r in rs]==list(range(1200,1200+len(rs)))
        assert tuple(map(int,reports[p]))==(p,len(bs),len(cs),0)
        assert len(bs)<4096 and len(cs)<1024
        assert [int(b['row']) for b in bs]==list(range(len(bs)))
        assert {int(b['offset']) for b in bs}==WINDOWS.get(p,set())
        assert all(r['scopeErrors']==0 and r['whole']==r['update']+r['render'] and
                   0<=r['pacing']<=r['whole'] and r['driver']>=0 and r['vehicles']==3 and
                   0<r['dt']<.2 and abs(r['x'])<2 and r['damage']<.001 for r in rs)
        if p<3:
            assert len(rs)==840 and all(abs(r['z']+74.3035)<.01 and abs(r['speed'])<.5 for r in rs)
        else:
            assert rs[-1]['z']>=108 and rs[-2]['z']<108
        byrow={int(b['row']):b for b in bs}
        counts=collections.Counter(int(c['row']) for c in cs)
        verts=collections.Counter()
        for c in cs:
            assert int(c['row']) in byrow and 0<c['n']<=64 and c['n']%3==0
            verts[int(c['row'])]+=int(c['n'])
            assert all(math.isfinite(f32(c['p'+str(i)])) for i in range(32))
            assert all(math.isfinite(f32(c['v'+str(i)])) for i in range(int(c['n'])*3))
        for b in bs:
            assert counter(b,'Bags')==1 and 0<=b['owner']<len(OWNERS) and 0<=b['pass']<len(PASSES) and b['view'] in (0,1,2)
            assert b['callsClip']==counter(b,'ClipChunks')==counts[int(b['row'])]
            assert counter(b,'ClipVerts')==verts[int(b['row'])]
            assert b['callsCull']<=counter(b,'CullChunks')
        for r in rs:
            selected=[b for b in bs if b['offset']==r['offset']]
            assert r['bags']==len(selected)
            for n,col in [('CullChunks','cullChunks'),('ClipChunks','clipChunks'),('CullVerts','cullVerts'),('ClipVerts','clipVerts')]:
                assert r[col]==sum(counter(b,n) for b in selected),(p,r['offset'],n)

def main():
    fixture,root,logfile=map(Path,sys.argv[1:4])
    header=(fixture/'tyra/engine/inc/debug/motion_probe.hpp').read_text()
    names=[s.strip() for s in re.search(r'enum Counter \{(.*?)\};',header,re.S)[1].split(',')][:-1]
    rows,bags,clips=[read(root/n) for n in ('drive-probe.csv','scene-bags.csv','scene-clips.csv')]
    log=logfile.read_text(errors='replace');validate(rows,bags,clips,names,log)
    rejected=[]
    for name,key,value in [('lost owner','owner',99),('bad view','view',4),('lost clip call','callsClip',99999)]:
        bad=copy.deepcopy(bags);bad[0][key]=value
        try:validate(rows,bad,clips,names,log)
        except AssertionError:rejected.append(name)
        else:raise RuntimeError('Accepted '+name)
    try:validate(rows,bags,clips[:-1],names,log)
    except AssertionError:rejected.append('missing geometry')
    else:raise RuntimeError('Accepted missing geometry')
    # Deterministic independent controls of the offline classifier.
    cube=[(1,0,0,1),(-1,0,0,1),(0,1,0,1),(0,-1,0,1),(0,0,1,1),(0,0,-1,1)]
    cube+=cube[:2]
    assert classify([(0,0,0),(.5,0,0),(0,.5,0)],cube)=='safe8'
    assert classify([(2,0,0),(3,0,0),(2,.5,0)],cube)=='outside6'
    assert classify([(0,0,0),(2,0,0),(0,.5,0)],cube)=='boundaryOrUncertain'
    counter=lambda b,n:b['c'+str(names.index(n))]
    classifications=collections.defaultdict(collections.Counter)
    for c in clips:
        ps=[[f32(c['p'+str(i*4+j)]) for j in range(4)] for i in range(8)]
        vs=[[f32(c['v'+str(i*3+j)]) for j in range(3)] for i in range(int(c['n']))]
        tally=classifications[int(c['phase']),int(c['row'])]
        chunk=[]
        for i in range(0,len(vs),3):
            tri=vs[i:i+3];kind=classify(tri,ps);tally[kind]+=1;chunk.append(kind)
            a=[tri[1][j]-tri[0][j] for j in range(3)];b=[tri[2][j]-tri[0][j] for j in range(3)]
            if all(a[(j+1)%3]*b[(j+2)%3]-a[(j+2)%3]*b[(j+1)%3]==0 for j in range(3)):tally['degenerate']+=1
        if all(k=='safe8' for k in chunk):tally['wholeChunkSafe8']+=1
        if all(k=='outside6' for k in chunk):tally['wholeChunkOutside6']+=1
        if classify(vs,ps)=='outside6':tally['wholeChunkCommonOutside6']+=1
        bounds=[(min(v[i] for v in vs),max(v[i] for v in vs)) for i in range(3)]
        boxKind=classify(list(itertools.product(*bounds)),ps)
        if boxKind=='safe8':tally['tightBoxSafe8']+=1
        if boxKind=='outside6':tally['tightBoxOutside6']+=1
    result={'scope':'nine StaPip frames; no DynPip; triangle boundary is not raster visibility',
            'negativeControlsRejected':rejected,'phases':[],'frames':[]}
    for p in range(6):
        moving=[r for r in rows if r['phase']==p and r['offset']>=1320]
        work=[(r['whole']-r['pacing'])/294912 for r in moving]
        result['phases'].append(dict(phase=p,frames=len(moving),captures=sum(r['probeCapture'] for r in moving),workMs=statistics.mean(work),
            p95Ms=sorted(work)[int(.95*(len(work)-1))],loopHz=294912000/statistics.mean(r['period'] for r in moving)))
    for p,offsets in WINDOWS.items():
        for offset in sorted(offsets):
            r=next(r for r in rows if r['phase']==p and r['offset']==offset)
            bs=[b for b in bags if b['phase']==p and b['offset']==offset]
            groups=collections.defaultdict(list)
            for b in bs:groups[int(b['owner']),int(b['view']),int(b['pass'])].append(b)
            summaries=[]
            for (owner,view,ps),group in sorted(groups.items()):
                triangles=collections.Counter()
                for b in group:triangles.update(classifications[p,int(b['row'])])
                summaries.append(dict(owner=OWNERS[owner],view=VIEWS[view],passName=PASSES[ps],bags=len(group),
                    sourceVertices=sum(b['sourceCount'] for b in group),
                    actualCull=sum(b['callsCull'] for b in group),actualClip=sum(b['callsClip'] for b in group),
                    counters={n:sum(counter(b,n) for b in group) for n in names if any(counter(b,n) for b in group)},
                    triangles=dict(triangles)))
            result['frames'].append(dict(phase=p,offset=offset,z=r['z'],speed=r['speed'],capture=r['probeCapture'],
                newBodies=r['missNew'],workMs=(r['whole']-r['pacing'])/294912,groups=summaries))
    (root/'scene-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(phases=result['phases'],frames=len(result['frames']),bags=len(bags),clips=len(clips),negativeControls=rejected),indent=2))

if __name__=='__main__':main()
