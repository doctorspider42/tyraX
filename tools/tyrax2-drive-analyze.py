"""Validate full-throttle physical/emulator CSVs; do not compare by frame index."""
import csv
import hashlib
import json
import math
import statistics
import sys
from pathlib import Path
freeze_trial='--freeze-capture' in sys.argv[2:]
fix_trial='--submit-fix' in sys.argv[2:]
geometry_trial='--geometry-reuse' in sys.argv[2:]
prepared_trial='--prepared-static' in sys.argv[2:]
partial_trial='--partial-baked' in sys.argv[2:] or geometry_trial
followup_trial='--clip-followup' in sys.argv[2:]
phase_count=8 if followup_trial else 18 if fix_trial else 6
prefix_flags=[a for a in sys.argv[2:] if a.startswith('--completed-prefix=')]
assert len(prefix_flags)<=1
prefix=int(prefix_flags[0].split('=',1)[1]) if prefix_flags else 0
if prefix:
    assert fix_trial and prefix in (3,6,9,12,15), 'Only complete fix-trial triplets may be salvaged'
    phase_count=prefix
def variant(phase):return [0,1,2,3,3,2,1,0][phase] if followup_trial else int(phase in (1,3,5)) if partial_trial or prepared_trial else phase//6+1 if fix_trial and phase%2 else 0
breakdown_trial='--packet-breakdown' in sys.argv[2:]
scope_trial='--packet-scopes' in sys.argv[2:] or breakdown_trial
assert not (freeze_trial and scope_trial)
assert not (fix_trial and (freeze_trial or scope_trial))
assert not (partial_trial and (fix_trial or freeze_trial or scope_trial))
assert not (followup_trial and (partial_trial or fix_trial or freeze_trial or scope_trial))
timing_fields='coreTicks coreWaitTicks partialTicks partialWaitTicks coreCalls partialCalls waitCalls scopeErrors'.split()
bucket_names='create classify fill packet submit payload'.split()
detail_fields=[n+s for s in ('Ticks','Calls') for n in bucket_names]
if breakdown_trial:timing_fields+=detail_fields

if sys.argv[1]=='--selftest':
    import copy
    import re
    import subprocess
    import tempfile
    header=Path(__file__).with_name('tyrax2-drive-probe.hpp').read_text()
    fields=re.search(r'fprintf\(output,"(phase,lifetime,[^"\\]+)\\n"',header).group(1).split(',')
    reference=[]
    for phase in range(phase_count):
        for i in range(303):
            r=dict.fromkeys(fields,0)
            r.update(phase=phase,lifetime=1000 if phase in (1,4) else 250,observed=int(phase<3),frame=phase*2000+1200+i,offset=1200+i,
                whole=10000,pacing=100,period=10000,update=1000,render=9000,flips=1,gas=int(i>=120),z=-74.3035+max(0,i-119),speed=20 if i>=120 else 0,dt=.05,driver=0,vehicles=3)
            if phase<3:r.update(bags=10,outside=2,direct=5,replay=5,partial=3,cachePeakBytes=1000)
            if freeze_trial:
                r['lifetime']=250
                r['captureFrozen']=int(phase in (1,4) and i>=120)
                if i>=120 and phase<3:
                    r['captureBlocked' if phase==1 else 'probeCapture']=1
            if scope_trial:
                r['lifetime']=250
                if phase in (1,3,5):r.update(coreTicks=4000,coreWaitTicks=500,partialTicks=2000,partialWaitTicks=200,coreCalls=10,partialCalls=3,waitCalls=2)
                if breakdown_trial and phase in (1,3,5):
                    r.update({n+'Ticks':100 for n in bucket_names})
                    r.update({n+'Calls':1 for n in bucket_names})
            if fix_trial:
                r.update(lifetime=250,observed=1,bags=10,outside=2,direct=5,replay=5,partial=3,cachePeakBytes=1000,fusedSubmit=10 if variant(phase)&1 else 0)
            if partial_trial:
                r['lifetime']=250
                if phase==1:r.update(partialReplay=2,partialBytes=64)
                if geometry_trial and phase==1:r['expandCachedVerts']=3
            if prepared_trial:
                r['lifetime']=250
                if phase<3:r.update(partialReplay=2,partialBytes=64,expandCachedVerts=3)
            if followup_trial:
                r.update(lifetime=250,observed=1,bags=10,outside=2,direct=5,replay=5,partial=3,cachePeakBytes=1000)
                if i>=120:r.update(probeCapture=1,envRecorded=2,barrierOmitted=2 if variant(phase)&2 else 0)
                if variant(phase)&1:r['expandCachedVerts']=3
            reference.append(r)
    cases={'reference':reference,'missingPhase':reference[:-303]}
    for name,index,key,value in [('lane',130,'x',20),('endpoint',302,'z',107),('pedal',130,'gas',0),('driver',130,'driver',-1),('accounting',0,'whole',1),('disabled',1000,'bags',1),('reason',130,'missNew',1),('ttl',130,'lifetime',1000),('parked',0,'speed',10)]:
        rs=copy.deepcopy(reference);rs[index][key]=value;cases[name]=rs
    if freeze_trial:
        for name,index,key,value in [('freezeFlag',433,'captureFrozen',0),('captureLeak',433,'probeCapture',1),('notBlocked',433,'captureBlocked',0)]:
            rs=copy.deepcopy(reference)
            if name=='notBlocked':
                for r in rs:r['captureBlocked']=0
            else:rs[index][key]=value
            cases[name]=rs
    if scope_trial:
        for name,index,key,value in [('clockLeak',130,'coreTicks',1),('nestedWait',433,'partialWaitTicks',501),('scopeDepth',433,'scopeErrors',1),('scopeCount',433,'coreCalls',11),('scopeMissing',433,'coreTicks',0),('partialExceedsCore',433,'partialTicks',5000),('coreExceedsFrame',433,'coreTicks',11000),('captureFrozen',433,'captureFrozen',1)]:
            rs=copy.deepcopy(reference);rs[index][key]=value;cases[name]=rs
    if breakdown_trial:
        for name,index,key,value in [('bucketLeak',130,'fillTicks',1),('bucketOverflow',433,'fillTicks',3000),('bucketMissingCalls',433,'fillCalls',0),('bucketMissingTicks',433,'fillTicks',0)]:
            rs=copy.deepcopy(reference);rs[index][key]=value;cases[name]=rs
    if fix_trial:
        for name,index,key,value in [('fusedLeak',130,'fusedSubmit',1),('unexpectedClock',433,'coreTicks',1),('captureFrozen',433,'captureFrozen',1),('parkMismatch',6*303,'snapshotBytes',16)]:
            rs=copy.deepcopy(reference);rs[index][key]=value;cases[name]=rs
        rs=copy.deepcopy(reference)
        for r in rs:r['fusedSubmit']=0
        cases['fusedNeverEntered']=rs
    if partial_trial:
        for name,index,key,value in [('replayLeak',130,'partialReplay',1),('unexpectedClock',433,'coreTicks',1),('captureFrozen',433,'captureFrozen',1)]:
            rs=copy.deepcopy(reference);rs[index][key]=value;cases[name]=rs
        rs=copy.deepcopy(reference)
        for r in rs:r['partialReplay']=0
        cases['replayNeverEntered']=rs
    if geometry_trial:
        rs=copy.deepcopy(reference);rs[130]['expandCachedVerts']=3
        cases['expansionLeak']=rs
        rs=copy.deepcopy(reference)
        for r in rs:r['expandCachedVerts']=0
        cases['expansionNeverEntered']=rs
    if prepared_trial:
        for name,key in [('noPartial','partialReplay'),('noExpansion','expandCachedVerts')]:
            rs=copy.deepcopy(reference)
            for r in rs:r[key]=0
            cases[name]=rs
        for name in ('missingPreparation','controlPreparation','emptyPreparation','reorderedPreparation'):
            cases[name]=reference
    if followup_trial:
        for name,index,key,value in [('expandLeak',130,'expandCachedVerts',3),('barrierLeak',130,'barrierOmitted',1),('fallback',433,'envFallback',1),('unexpectedClock',433,'coreTicks',1),('captureFrozen',433,'captureFrozen',1)]:
            rs=copy.deepcopy(reference);rs[index][key]=value;cases[name]=rs
        for name,key in [('noExpansion','expandCachedVerts'),('noBarrier','barrierOmitted'),('noRecording','envRecorded')]:
            rs=copy.deepcopy(reference)
            for r in rs:r[key]=0
            cases[name]=rs
    result={}
    with tempfile.TemporaryDirectory() as temp:
        for name,rs in cases.items():
            with (Path(temp)/'drive-probe.csv').open('w',newline='') as f:
                w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rs)
            extra=[]
            if prepared_trial:
                activation=Path(temp)/'activation.log'
                records=[f'PREPAREDSTATIC phase={p} tried={10 if variant(p) else 0} ready={10 if variant(p) else 0} ticks=123' for p in range(6)]
                if name=='missingPreparation':records.pop()
                if name=='controlPreparation':records[0]='PREPAREDSTATIC phase=0 tried=10 ready=10 ticks=123'
                if name=='emptyPreparation':records[1]='PREPAREDSTATIC phase=1 tried=10 ready=0 ticks=123'
                if name=='reorderedPreparation':records[0],records[1]=records[1],records[0]
                activation.write_text('\n'.join(records))
                extra=['--activation-log='+str(activation)]
            run=subprocess.run([sys.executable,__file__,temp]+(['--prepared-static'] if prepared_trial else ['--geometry-reuse'] if geometry_trial else ['--clip-followup'] if followup_trial else ['--partial-baked'] if partial_trial else ['--submit-fix'] if fix_trial else ['--packet-breakdown'] if breakdown_trial else ['--freeze-capture'] if freeze_trial else ['--packet-scopes'] if scope_trial else [])+prefix_flags+extra,capture_output=True,text=True)
            result[name]=run.returncode==0
            if name=='reference' and run.returncode:print(run.stderr)
    assert result=={name:name=='reference' for name in cases},result
    Path(sys.argv[2]).write_text(json.dumps(result,indent=2)+'\n')
    print(result)
    sys.exit(0)

root=Path(sys.argv[1])
source=root/'drive-probe.csv'
floats='x y z speed yaw dt damage eyeX eyeY eyeZ lookX lookY lookZ'.split()
rows=[{k:float(v) if k in floats else int(v) for k,v in r.items()} for r in csv.DictReader(source.open())]
assert rows and all(math.isfinite(v) for r in rows for v in r.values())
counters=[k for k in list(rows[0])[27:] if k!='captureFrozen' and k not in timing_fields+detail_fields]
reasons='missNew missBBox missContent missPrim missStreams missProgram missDepth missSize'.split()
def summary(rs):
    work=[(r['whole']-r['pacing'])/294912 for r in rs]
    return dict(rows=len(rs),workMean=statistics.mean(work),workP95=sorted(work)[min(len(work)-1,math.ceil(.95*len(work))-1)],workMax=max(work),
        periodMean=statistics.mean(r['period'] for r in rs)/294912,longPeriods=sum(r['period']>294912*25 for r in rs),
        updateMean=statistics.mean(r['update'] for r in rs)/294912,speedMean=statistics.mean(r['speed'] for r in rs),
        counterMeans={k:statistics.mean(r[k] for r in rs) for k in counters},
        **(dict(scopeMeans={k:statistics.mean(r[k] for r in rs)/(294912 if k.endswith('Ticks') else 1) for k in timing_fields},
                 coreLessWaitMs=statistics.mean(r['coreTicks']-r['coreWaitTicks'] for r in rs)/294912,
                 partialLessWaitMs=statistics.mean(r['partialTicks']-r['partialWaitTicks'] for r in rs)/294912) if scope_trial else {}),
        **(dict(partialUnassignedMs=statistics.mean(r['partialTicks']-sum(r[n+'Ticks'] for n in bucket_names) for r in rs)/294912) if breakdown_trial else {}))
result=dict(units='milliseconds; physical EE Count 294912000 ticks/sec (emulator not hardware cost)',phases=[])
result['experiment']='prepared-static' if prepared_trial else 'geometry-reuse' if geometry_trial else 'clip-followup' if followup_trial else 'partial-baked' if partial_trial else 'submit-fix' if fix_trial else 'packet-breakdown' if breakdown_trial else 'packet-scopes' if scope_trial else 'capture-freeze' if freeze_trial else 'cache-lifetime'
sample_start=rows[0]['offset']
assert sample_start in (180,1200)
result['sampleStart']=sample_start
result['warmupExceedsLongLifetime']=sample_start>1000
assert set(r['phase'] for r in rows)==set(range(phase_count))
for phase in range(phase_count):
    rs=[r for r in rows if r['phase']==phase]
    assert 120<len(rs)<=1200
    assert [r['offset'] for r in rs]==list(range(sample_start,sample_start+len(rs)))
    assert all(b['frame']==a['frame']+1 for a,b in zip(rs,rs[1:]))
    assert all(r['lifetime']==(1000 if not (freeze_trial or scope_trial or fix_trial or partial_trial or followup_trial or prepared_trial) and phase in (1,4) else 250) and r['observed']==int(fix_trial or followup_trial or phase<3) for r in rs)
    assert all(r['gas']==int(r['offset']>=sample_start+120) and r['driver']==0 and r['vehicles']==3 for r in rs)
    assert all(r['whole']==r['update']+r['render'] and 0<=r['pacing']<=r['whole'] for r in rs)
    assert all(sum(r[k] for k in reasons)==r['bakeMiss'] for r in rs)
    assert all(r['replay']<=r['direct'] and r['outside']+r['direct']+r['partial']<=r['bags'] for r in rs)
    if phase>=3 and not (fix_trial or followup_trial): assert all(r[k]==0 for r in rs for k in counters)
    else: assert all(r['bags']>0 and r['cachePeakBytes']<=4194304 for r in rs)
    parked=[r for r in rs if not r['gas']];drive=[r for r in rs if r['gas']]
    if prepared_trial:
        assert all(r[k]==0 for r in rs for k in timing_fields+detail_fields+['captureFrozen','captureBlocked','fusedSubmit','barrierOmitted'])
        if phase<3:
            assert sum(r['partialReplay'] for r in drive)>0
            assert sum(r['expandCachedVerts'] for r in drive)>0
    if followup_trial:
        assert all(r[k]==0 for r in rs for k in timing_fields+detail_fields+['captureFrozen','captureBlocked','fusedSubmit','partialReplay','envFallback'])
        assert sum(r['envRecorded'] for r in drive)>0 and sum(r['probeCapture'] for r in drive)>0
        assert all(r['envRecorded']==2*r['probeCapture'] for r in rs)
        if variant(phase)&1:assert sum(r['expandCachedVerts'] for r in drive)>0
        else:assert all(r['expandCachedVerts']==0 and r['expandBuilds']==0 for r in rs)
        if variant(phase)&2:assert all(r['barrierOmitted']==r['envRecorded'] for r in rs)
        else:assert all(r['barrierOmitted']==0 for r in rs)
    if partial_trial:
        assert all(r[k]==0 for r in rs for k in timing_fields+detail_fields)
        assert all(r['captureFrozen']==0 and r['captureBlocked']==0 and r['fusedSubmit']==0 for r in rs)
        if phase==1:assert sum(r['partialReplay'] for r in drive)>0 and sum(r['partialBytes'] for r in drive)>0
        else:assert all(r['partialReplay']==0 and r['partialBytes']==0 and r['partialBuild']==0 for r in rs)
    if geometry_trial:
        if phase==1:assert sum(r['expandCachedVerts'] for r in drive)>0
        else:assert all(r['expandCachedVerts']==0 and r['expandBuilds']==0 for r in rs)
        assert all(r['barrierOmitted']==0 and r['envFallback']==0 for r in rs)
    if fix_trial:
        assert all(r[k]==0 for r in rs for k in timing_fields+detail_fields)
        assert all(r['captureFrozen']==0 and r['captureBlocked']==0 for r in rs)
        if variant(phase)&1:assert sum(r['fusedSubmit'] for r in drive)>0
        else:assert all(r['fusedSubmit']==0 for r in rs)
    if scope_trial:
        assert all(r['captureFrozen']==0 and r['captureBlocked']==0 for r in rs)
        if phase in (1,3,5):
            assert all(0<=r['partialWaitTicks']<=r['coreWaitTicks']<=r['coreTicks']<=r['whole'] for r in rs)
            assert all(r['partialWaitTicks']<=r['partialTicks']<=r['coreTicks'] and r['scopeErrors']==0 and r['coreCalls']>0 for r in rs)
            assert all(r['coreTicks']>0 and r['partialCalls']>0 and r['partialTicks']>0 for r in rs)
            if phase==1:assert all(r['coreCalls']==r['bags'] and r['partialCalls']==r['partial'] for r in rs)
            if breakdown_trial:
                assert all(sum(r[n+'Ticks'] for n in bucket_names)<=r['partialTicks'] for r in rs)
                assert all((r[n+'Ticks']>0)==(r[n+'Calls']>0) for r in rs for n in bucket_names)
        else:assert all(r[k]==0 for r in rs for k in timing_fields)
    if freeze_trial:
        assert all(r['captureFrozen']==int(phase in (1,4) and r['gas']) for r in rs)
        if phase==1:
            assert all(r['probeCapture']==0 for r in drive) and sum(r['captureBlocked'] for r in drive)>0
        if phase in (0,2):
            assert sum(r['probeCapture'] for r in drive)>0 and all(r['captureBlocked']==0 for r in rs)
    assert len(parked)==120 and all(abs(r['speed'])<.5 and abs(r['z']+74.3035)<1 for r in parked)
    assert drive[-1]['z']>=108 and max(r['speed'] for r in drive)>10
    assert all(abs(r['x'])<6.5 and abs(r['yaw'])<10 and 0<r['dt']<.2 for r in rs)
    assert all(b['z']>=a['z']-.01 for a,b in zip(drive,drive[1:]))
    bins=[]
    for start in range(-70,91,20):
        segment=[r for r in drive if start<=r['z']<start+20]
        assert segment,(phase,start)
        bins.append(dict(zStart=start,zEnd=start+20,**summary(segment)))
    result['phases'].append(dict(phase=phase,variant=variant(phase),lifetime=rs[0]['lifetime'],observed=fix_trial or followup_trial or phase<3,parked=summary(parked),drive=summary(drive),
        finalPose={k:drive[-1][k] for k in floats},duration=sum(r['dt'] for r in drive),distance=drive[-1]['z']-parked[-1]['z'],bins=bins))
    if phase<3:
        result['phases'][-1]['captureGroups']={}
        for active in (False,True):
            group=[r for r in drive if bool(r['probeCapture'])==active]
            if group:result['phases'][-1]['captureGroups'][str(active)]=summary(group)
park0=result['phases'][0]['parked']['counterMeans']
park2=result['phases'][2]['parked']['counterMeans']
stable_keys='bags outside direct replay partial packages expandVerts copyBytes snapshotBytes bakeMiss churn probeCapture paint rebuild evicted reserveFailed'.split()
result['parkedControlMismatches']={k:[park0[k],park2[k]] for k in stable_keys if park0[k]!=park2[k]}
result['startingStateQualified']=result['warmupExceedsLongLifetime'] and not result['parkedControlMismatches']
if fix_trial:
    result['allParkedMismatches']={str(p['phase']):{k:[park0[k],p['parked']['counterMeans'][k]] for k in stable_keys if park0[k]!=p['parked']['counterMeans'][k]} for p in result['phases']}
    assert not any(result['allParkedMismatches'].values()),result['allParkedMismatches']
if not (freeze_trial or scope_trial or fix_trial or partial_trial or followup_trial or prepared_trial):result['ttlContrastStartingStateQualified']=result['startingStateQualified']
if prepared_trial:
    assert result['startingStateQualified'],result['parkedControlMismatches']
    import re
    flags=[a.split('=',1)[1] for a in sys.argv[2:] if a.startswith('--activation-log=')]
    assert len(flags)==1,'Prepared trial requires the matching runtime log'
    activation=Path(flags[0]).read_text(errors='replace')
    records=[tuple(map(int,m)) for m in re.findall(r'PREPAREDSTATIC phase=(\d+) tried=(\d+) ready=(\d+) ticks=(\d+)',activation)]
    assert [r[0] for r in records]==list(range(6)),records
    for p,tried,ready,ticks in records:
        assert (0<ready<=tried) if variant(p) else (ready==tried==0)
    result['preparation']=[dict(phase=p,tried=t,ready=r,elapsedMs=c/294912) for p,t,r,c in records]
if followup_trial:
    assert result['startingStateQualified'],result['parkedControlMismatches']
    semantic='bags outside direct replay partial packages paint rebuild'.split()
    for p in result['phases']:
        assert all(park0[k]==p['parked']['counterMeans'][k] for k in semantic),(p['phase'],p['parked']['counterMeans'])
    assert all(park0[k]==result['phases'][7]['parked']['counterMeans'][k] for k in stable_keys)
if partial_trial or prepared_trial:
    assert result['startingStateQualified'],result['parkedControlMismatches']
    semantic=('bags outside direct replay partial packages paint rebuild' if geometry_trial or prepared_trial else 'bags outside direct replay partial packages expandVerts copyBytes paint rebuild').split()
    park1=result['phases'][1]['parked']['counterMeans']
    assert all(park0[k]==park1[k] for k in semantic),{k:[park0[k],park1[k]] for k in semantic}
result['sourceSha256']=hashlib.sha256(source.read_bytes()).hexdigest()
if fix_trial:
    result['fullProtocolCompleted']=not bool(prefix)
    result['requestedPhases']=18
    result['completedPhases']=phase_count
    result['qualification']='Completed prefix only; missing arms unqualified' if prefix else 'All 18 phases completed'
(root/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
for p in result['phases']:
    print(p['phase'],'ttl',p['lifetime'],'park',round(p['parked']['workMean'],3),'drive',round(p['drive']['workMean'],3),'update',round(p['drive']['updateMean'],3),'period',round(p['drive']['periodMean'],3),'long',p['drive']['longPeriods'],'rows',p['drive']['rows'],'duration',round(p['duration'],2))
