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
VU_PAINT='--vu-paint' in sys.argv
ROAD_REFINE='--road-refine' in sys.argv
ROAD_SINGLE='--road-single' in sys.argv
ROAD_PACK='--road-pack' in sys.argv
ROAD_NO_KICK='--road-no-kick' in sys.argv
ROAD_VU='--road-vu-bypass' in sys.argv
ROAD_PREP='--road-prep-scopes' in sys.argv
ROAD_MEMO='--road-clip-memo' in sys.argv
ROAD_GROUP='--road-groups' in sys.argv
ROAD_PAIR='--road-pair-omit' in sys.argv
assert not ROAD_PAIR or (VU_PAINT and not (ROAD_GROUP or ROAD_MEMO or ROAD_PREP or ROAD_VU or ROAD_NO_KICK or ROAD_SINGLE or ROAD_REFINE or '--road-submit-off' in sys.argv))
ROAD_PREFIX='--road-prefix' in sys.argv
assert not ROAD_PREFIX or ROAD_PREP
PREFIX_TIMES=['prefixHead','prefixBounds','prefixTexture','prefixLight','prefixObject']
if ROAD_VU: ROAD_NO_KICK=True
ROAD_SUBMIT='--road-submit-off' in sys.argv or ROAD_NO_KICK
assert not ROAD_MEMO or (VU_PAINT and not (ROAD_PREP or ROAD_SUBMIT or ROAD_REFINE or ROAD_SINGLE))
assert not ROAD_GROUP or (VU_PAINT and not (ROAD_PREP or ROAD_MEMO or ROAD_SUBMIT or ROAD_REFINE or ROAD_SINGLE))
ROAD_LABEL='ROADRASTER' if ROAD_NO_KICK else 'ROADSUBMIT'
assert not ROAD_SUBMIT or (VU_PAINT and not ROAD_REFINE and not ROAD_SINGLE)
assert not ROAD_PACK or ROAD_SINGLE
assert not (ROAD_REFINE and ROAD_SINGLE)
assert not ROAD_SINGLE or VU_PAINT
assert not ROAD_REFINE or VU_PAINT
assert not ROAD_PREP or (VU_PAINT and not (ROAD_SUBMIT or ROAD_REFINE or ROAD_SINGLE))
PREP_TIMES=['coreTicks','partialTicks','createTicks','classifyTicks','fillTicks','packetTicks','submitTicks','payloadTicks']
PREP_CALLS=['coreCalls','partialCalls','createCalls','classifyCalls','fillCalls','packetCalls','submitCalls','payloadCalls']
if VU_PAINT:base.ACTIVE.append('paintVu')
MODES=[0,6,0] if VU_PAINT else ([0,5,0] if '--scatter-copy' in sys.argv else [0,4,3,1,0])
if VU_PAINT and '--reverse' in sys.argv: MODES=[6,0,6]
CONTEXT=[f for f in ('--night-roads','--roads-day','--night-no-roads','--day-no-roads') if f in sys.argv]
assert len(CONTEXT)<=1
NIGHT=bool(set(CONTEXT)&{'--night-roads','--night-no-roads'})
ROADS=bool(set(CONTEXT)&{'--night-roads','--roads-day'})
def analyze(rows,log):
    assert 'DRIVEDONE valid=1' in log
    assert len(rows)==len(MODES)*840 and {r['phase'] for r in rows}==set(range(len(MODES)))
    assert all(math.isfinite(v) for r in rows for v in r.values())
    records=re.findall(r'DRIVEPHASE phase=(\d+) ttl=(\d+) observed=(\d+) valid=(\d+) rows=(\d+).*?variant=(\d+)',log)
    assert [tuple(map(int,r)) for r in records]==[(p,250,0,1,840,m) for p,m in enumerate(MODES)]
    assert re.findall(r'TURNPINS phase=(\d+) count=(\d+)',log)==[(str(p),'18') for p in range(len(MODES))]
    if ROAD_PREP:
        assert re.findall(r'ROADPREP phase=(\d+) clocks=(\d+) normalDrawing=(\d+)',log)==[(str(p),str(int(m==6)),'1') for p,m in enumerate(MODES)]
    if ROAD_MEMO:
        witnesses=re.findall(r'ROADMEMO phase=(\d+) calls=(\d+) hits=(\d+) checks=(\d+) interleave=(\d+)',log)
        assert len(witnesses)==len(MODES)
        assert all(int(p)==i and int(c)>0 and int(il)==2 and
                   (0<int(h)==int(v)<=int(c) if MODES[i]==6 else int(h)==int(v)==0)
                   for i,(p,c,h,v,il) in enumerate(witnesses))
    if ROAD_GROUP:
        witnesses=re.findall(r'ROADGROUP phase=(\d+) groups=(\d+) accepted=(\d+) input=(\d+) submitted=(\d+) output=(\d+) merged=(\d+) padding=(\d+) interleave=(\d+)',log)
        assert len(witnesses)==len(MODES) and len({(g,a,v) for _,g,a,v,_,_,_,_,_ in witnesses})==1
        assert all(int(p)==i and int(g)>0 and int(a)>0 and int(v)>0 and int(v)+int(pad)==int(o) and int(il)==2 and
                   (int(m)>0 and 0<int(s)<int(a) if MODES[i]==6 else int(m)==int(pad)==0 and int(s)==int(a))
                   for i,(p,g,a,v,s,o,m,pad,il) in enumerate(witnesses))
    if ROAD_PAIR:
        witnesses=re.findall(r'PAIRCLIP phase=(\d+) eligible=(\d+) skipped=(\d+) triangles=(\d+) interleave=(\d+)',log)
        assert len(witnesses)==3 and len({(a,t) for _,a,_,t,_ in witnesses})==1
        assert all(int(p)==i and int(a)>0 and int(t)>0 and int(il)==2 and int(s)==(int(a) if MODES[i]==6 else 0) for i,(p,a,s,t,il) in enumerate(witnesses))
        meta=re.findall(r'PAIRMETA phase=(\d+) id=(\d+) count=(\d+) repeated=(\d+) blend=(\d+) edge=(\d+)',log)
        assert len(meta)==6 and {(int(p),int(id)) for p,id,_,_,_,_ in meta}=={(p,id) for p in range(3) for id in (24,34)}
        assert all(int(n)==66 and 0<=int(d)<=64 for _,_,n,d,_,_ in meta)
        assert all(len({(n,d,b,e) for _,i,n,d,b,e in meta if int(i)==id})==1 for id in (24,34))
    if ROAD_SUBMIT:
        witnesses=re.findall(ROAD_LABEL+r' phase=(\d+) accepted=(\d+) vertices=(\d+) skipped=(\d+)',log)
        assert len(witnesses)==len(MODES)
        assert len({(a,v) for _,a,v,_ in witnesses})==1
        assert all(int(p)==i and int(a)>0 and int(v)>0 and
                   int(s)==(int(a) if MODES[i]==6 else 0)
                   for i,(p,a,v,s) in enumerate(witnesses))
    if ROAD_VU:
        uniforms=re.findall(r'ROADVU phase=(\d+) uniforms=(\d+) bypass=(\d+)',log)
        assert len(uniforms)==len(MODES) and len({u for _,u,_ in uniforms})==1
        assert all(int(p)==i and int(u)>0 and int(b)==(int(u) if MODES[i]==6 else 0)
                   for i,(p,u,b) in enumerate(uniforms))
    if ROAD_REFINE:
        witnesses=re.findall(r'ROADREFINE phase=(\d+) bags=(\d+) partial=(\d+) refined=(\d+)',log)
        assert len(witnesses)==len(MODES)
        assert all(int(p)==i and int(bags)==66 and int(partial)>0 and
                   (int(refined)>0 if MODES[i]==6 else int(refined)==0)
                   for i,(p,bags,partial,refined) in enumerate(witnesses))
    if ROAD_SINGLE:
        witnesses=re.findall(r'ROADCOLORS phase=(\d+) eligible=(\d+) selected=(\d+) draws=(\d+) vertices=(\d+)',log)
        assert len(witnesses)==len(MODES)
        assert len({e if ROAD_PACK else (e,v) for _,e,_,_,v in witnesses})==1
        assert all(int(p)==i and int(e)>0 and int(v)>0 and
                   (int(s)==int(e) and int(d)>0 if MODES[i]==6 else int(s)==int(d)==0)
                   for i,(p,e,s,d,v) in enumerate(witnesses))
        if ROAD_PACK:
            packs=re.findall(r'ROADPACK phase=(\d+) run=(\d+) before=(\d+) after=(\d+) verified=(\d+)',log)
            assert len(packs)==len(MODES)
            assert all(int(p)==i and int(before)>0 and
                       (int(run)>75 and int(after)<int(before) and int(checked)>0 if MODES[i]==6
                        else int(run)==75 and int(after)==int(before) and int(checked)==0)
                       for i,(p,run,before,after,checked) in enumerate(packs))
    if CONTEXT:
        context=re.findall(r'NIGHTROADS phase=(\d+) hour=([\d.]+) paused=(\d+) chunks=(\d+) lights=(\d+)',log)
        assert len(context)==len(MODES)
        assert all(int(p)==i and float(hour)==(0 if NIGHT else 12) and int(paused)==1
                   and (int(chunks)>0 if ROADS else int(chunks)==0) and (int(lights)>0 if NIGHT else int(lights)==0)
                   for i,(p,hour,paused,chunks,lights) in enumerate(context))
        assert len({(chunks,lights) for _,_,_,chunks,lights in context})==1
    names=list(rows[0]);disabled=[k for k in names[names.index('bags'):] if k not in base.ACTIVE+['captureFrozen']]
    if ROAD_PREP: disabled=[k for k in disabled if k not in PREP_TIMES+PREP_CALLS]
    if ROAD_PREFIX: disabled=[k for k in disabled if k not in PREFIX_TIMES]
    phases=[]
    for p,m in enumerate(MODES):
        rs=[r for r in rows if r['phase']==p];park,turn=rs[:120],rs[120:]
        assert [r['offset'] for r in rs]==list(range(1200,2040))
        assert all(r['lifetime']==250 and r['observed']==0 and r['flips']==1 for r in rs)
        assert all(r['whole']==r['update']+r['render'] and 0<=r['pacing']<=r['whole'] and r['period']>0 for r in rs)
        assert all(0<r['dt']<.2 and r['vehicles']==3 and r['driver']>=0 and abs(r['speed'])<.05 and r['damage']<.001 for r in rs)
        assert all(abs(r[k]-rows[0][k])<.01 for r in rs for k in ('x','y','z','yaw'))
        assert all(r[k]==0 for r in rs for k in disabled)
        if ROAD_PREP:
            assert all(r[k]>=0 and r[k]==int(r[k]) for r in rs for k in PREP_TIMES+PREP_CALLS)
            if m!=6:
                assert all(r[k]==0 for r in rs for k in PREP_TIMES+PREP_CALLS)
            else:
                assert all(r['coreTicks']>0 and r['coreCalls']>0 and r['partialTicks']>0 and
                           0<r['partialCalls']<=r['coreCalls'] and r['partialTicks']<=r['coreTicks'] and
                           sum(r[k] for k in PREP_TIMES[2:])<=r['coreTicks'] for r in rs)
                assert all(sum(r[k] for r in turn)>0 for k in PREP_CALLS)
            if ROAD_PREFIX:
                assert all(r[k]>=0 and r[k]==int(r[k]) for r in rs for k in PREFIX_TIMES)
                if m!=6: assert all(r[k]==0 for r in rs for k in PREFIX_TIMES)
                else:
                    assert all(0<sum(r[k] for k in PREFIX_TIMES)<=r['coreTicks']-r['partialTicks'] for r in rs)
                    assert all(sum(r[k] for r in turn)>0 for k in PREFIX_TIMES)
        assert all(r['gas']==0 and r['captureFrozen']==0 and r['envSkipped']==0 and r['paintBlocked']==0 for r in park)
        assert all(r['gas']==1 and r['captureFrozen']==int(m==1) for r in turn)
        assert all(r['envEligible']==r['envDrawn']+r['envSkipped'] for r in rs)
        counts=base.stats(turn)['counts']
        if VU_PAINT:
            assert all(r['paintVu']==(r['envDrawn'] if ROAD_REFINE or ROAD_SINGLE or ROAD_SUBMIT or ROAD_PREP or ROAD_MEMO or ROAD_GROUP or ROAD_PAIR or m==6 else 0) for r in rs)
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
        if ROAD_PREP:
            phases[-1]['roadPreparation']={k:sum(r[k] for r in turn)/len(turn)/(294912 if k in PREP_TIMES else 1) for k in PREP_TIMES+PREP_CALLS}
            phases[-1]['roadPreparation']['timingUnit']='ms per turn frame; elapsed, not pure EE'
            phases[-1]['roadPreparation']['partialTicksMeaning']='all selected dispatch routes'
            if ROAD_PREFIX:
                phases[-1]['roadPreparation']['prefixMs']={k:sum(r[k] for r in turn)/len(turn)/294912 for k in PREFIX_TIMES}
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
    if VU_PAINT:cases += [('missing VU paint',MODES.index(6),'paintVu',0),('wrong control paint',MODES.index(0),'paintVu',0 if ROAD_REFINE or ROAD_SINGLE or ROAD_SUBMIT or ROAD_PREP or ROAD_MEMO or ROAD_GROUP or ROAD_PAIR else 1)]
    if ROAD_PAIR:
        for name,badlog in [('missing pair witness',log.replace('PAIRCLIP','INVALID')),
                            ('inactive pair',re.sub(r'skipped=\d+','skipped=0',log)),
                            ('wrong identity',log.replace('id=24 count=66','id=24 count=65'))]:
            try:analyze(rows,badlog)
            except AssertionError:rejected.append(name)
            else:raise RuntimeError('Accepted '+name)
    if ROAD_GROUP:
        for name,badlog in [('missing group witness',log.replace('ROADGROUP','INVALID')),
                            ('inactive groups',re.sub(r'merged=\d+','merged=0',log)),
                            ('lost group inputs',re.sub(r'output=\d+','output=0',log)),
                            ('adaptive group order',re.sub(r'interleave=\d+','interleave=1',log))]:
            try:analyze(rows,badlog)
            except AssertionError:rejected.append(name)
            else:raise RuntimeError('Accepted '+name)
    if ROAD_MEMO:
        for name,badlog in [('missing memo witness',log.replace('ROADMEMO','INVALID')),
                            ('inactive memo',re.sub(r'hits=\d+','hits=0',log)),
                            ('missing memo oracle',re.sub(r'checks=\d+','checks=0',log)),
                            ('adaptive order',re.sub(r'interleave=\d+','interleave=1',log))]:
            try:analyze(rows,badlog)
            except AssertionError:rejected.append(name)
            else:raise RuntimeError('Accepted '+name)
    if ROAD_PREP:
        cases += [('inactive road scope',1,'coreCalls',0),('invalid nested time',1,'partialTicks',1e9),('bucket overflow',1,'submitTicks',1e9),('scope error',1,'scopeErrors',1)]
        try:analyze(rows,log.replace('ROADPREP','INVALID'))
        except AssertionError:rejected.append('missing road scope witness')
        else:raise RuntimeError('Accepted missing road scope witness')
    if ROAD_PREFIX:
        cases += [('prefix clock leak',0,'prefixHead',1),('prefix overflow',1,'prefixObject',1e9)]
    if ROAD_SUBMIT:
        for name,badlog in [('missing submit witness',log.replace(ROAD_LABEL,'INVALID')),
                            ('no omitted draws',re.sub(r'skipped=\d+','skipped=0',log)),
                            ('empty road inputs',re.sub(r'vertices=\d+','vertices=0',log))]:
            try:analyze(rows,badlog)
            except AssertionError:rejected.append(name)
            else:raise RuntimeError('Accepted '+name)
    if ROAD_VU:
        for name,badlog in [('missing VU witness',log.replace('ROADVU','INVALID')),
                            ('inactive bypass',re.sub(r'bypass=\d+','bypass=0',log))]:
            try:analyze(rows,badlog)
            except AssertionError:rejected.append(name)
            else:raise RuntimeError('Accepted '+name)
    if ROAD_SINGLE:
        for name,badlog in [('missing colour witness',log.replace('ROADCOLORS','INVALID')),
                            ('inactive single colour',re.sub(r'selected=\d+','selected=0',log)),
                            ('no single draws',re.sub(r'draws=\d+','draws=0',log))]:
            try:analyze(rows,badlog)
            except AssertionError:rejected.append(name)
            else:raise RuntimeError('Accepted '+name)
        if ROAD_PACK:
            for name,badlog in [('missing pack witness',log.replace('ROADPACK','INVALID')),
                                ('no triangle verification',re.sub(r'verified=\d+','verified=0',log))]:
                try:analyze(rows,badlog)
                except AssertionError:rejected.append(name)
                else:raise RuntimeError('Accepted '+name)
    if ROAD_REFINE:
        for name,badlog in [('missing road witness',log.replace('ROADREFINE','INVALID')),
                            ('inactive refinement',re.sub(r'refined=\d+','refined=0',log))]:
            try:analyze(rows,badlog)
            except AssertionError:rejected.append(name)
            else:raise RuntimeError('Accepted '+name)
    for name,p,key,v in cases:
        bad=copy.deepcopy(rows);next(r for r in bad if r['phase']==p and r['gas'])[key]=v
        try:analyze(bad,log)
        except AssertionError:rejected.append(name)
        else:raise RuntimeError('Accepted '+name)
    if CONTEXT:
        for name,badlog in ([('missing night witness',log.replace('NIGHTROADS','INVALID')),
                            ('wrong hour',re.sub(r'hour=[\d.]+','hour='+('12.000000' if NIGHT else '0.000000'),log)),
                            ('wrong roads',re.sub(r'chunks=\d+','chunks='+('0' if ROADS else '66'),log))]+
                            [('wrong lights',re.sub(r'lights=\d+','lights='+('0' if NIGHT else '8'),log))]):
            try:analyze(rows,badlog)
            except AssertionError:rejected.append(name)
            else:raise RuntimeError('Accepted '+name)
    result['negativeControlsRejected']=rejected
(root/'turn-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
for p in result['phases']:print(p['phase'],p['mode'],p['turn'])
print(result['contrasts'])
