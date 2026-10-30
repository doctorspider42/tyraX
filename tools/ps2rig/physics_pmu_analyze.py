"""Validate independently calibrated EE PMU profiles; do not qualify FPS."""
from pathlib import Path
import argparse,csv,hashlib,json,math,re,statistics

def analyze(root,log,meta):
    text=log.read_text(encoding='utf8',errors='replace')
    assert meta.get('physics_pmu') and meta.get('expected_phases')==[12,13]
    assert 'CB DONE phases=14' in text and 'CB ERROR' not in text
    assert 'PHYSICSPMU calibration rejected' not in text
    assert all(marker not in text for marker in ['VUSATDIFF ','WORLDGRIDDIFF '])
    assert 'VUSATCHECK cases=2048 failures=0 ' in text and 'VUSATPRECISION cases=50000 failures=0' in text
    cal=re.search(r'PHYSICSPMUCAL ticks=(\d+) cycle0=(\d+) cycle1=(\d+) fit4k=(\d+) scan128k=(\d+) sink=\d+ passed=1',text)
    assert cal,'Missing or failed hardware counter calibration'
    dt,a,b,small,big=map(int,cal.groups())
    assert dt>0 and .9<a/dt<1.1 and .9<b/dt<1.1 and small<128 and 4096<big<16384
    result=[];hashes={}
    for phase in [12,13]:
        assert f'PHYSICSPMU phase={phase} frames=600 errors=0 file=1' in text
        source=root/f'collision-phase-{phase:02}.csv';frames=list(csv.DictReader(source.open()))
        assert [int(r['frame']) for r in frames]==list(range(150,750))
        assert all(r['generated_contacts']==r['contacts'] and r['push_candidates']==r['push_tested'] and
            int(r['dropped_contacts'])==int(r['push_overflow'])==0 and
            all(math.isfinite(float(r[k])) for k in ['car_z','car_speed','velocity_energy','max_depth']) for r in frames)
        hashes[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
        source=root/f'vu0-phase-{phase:02}.csv';micro=list(csv.DictReader(source.open()));assert len(micro)==1
        assert all(int(micro[0][k])==0 for k in ['jobs','batches','busy_ee_query','validation_errors','missed_reference_hits'])
        hashes[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
        source=root/f'physics-pmu-{phase:02}.csv';rows=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(source.open())]
        hashes[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
        assert all(r['phase']==phase and r['mode']==r['frame']%3 and r['calls']>0 for r in rows)
        keys=[(r['frame'],r['stage']) for r in rows];assert len(keys)==len(set(keys))
        whole=[r for r in rows if r['stage']==0]
        assert [r['frame'] for r in whole]==list(range(150,750))
        assert all(r['calls']==1 and r['ticks']>0 and r['pcr0']>0 and r['pcr1']>0 for r in whole)
        assert all(r['pcr0']<0x80000000 and r['pcr1']<0x80000000 for r in rows)
        for mode in range(3):
            assert sum(r['mode']==mode for r in whole)==200
            for stage in sorted(set(r['stage'] for r in rows)):
                selected=[r for r in rows if r['mode']==mode and r['stage']==stage and 270<=r['frame']<630]
                assert len(selected)==120,(phase,mode,stage,len(selected))
                mean={k:statistics.mean(r[k] for r in selected) for k in ['ticks','pcr0','pcr1','contacts','awake','calls']}
                if mode==1:assert mean['pcr0']+mean['pcr1']<=mean['ticks']*1.05
                if mode==2:assert mean['pcr0']<=mean['ticks']*2.05 and mean['pcr1']<=mean['pcr0']
                result.append(dict(phase=phase,baseline=phase==12,mode=mode,stage=stage,frames=len(selected),mean=mean))
    return dict(metadata=meta,performance_qualified=False,calibration=dict(ticks=dt,cycle0=a,cycle1=b,fit4k=small,scan128k=big),
        measured_frames=1200,drive_samples=result,raw_sha256=hashes,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),
        scope='Instrumented outer-stage diagnostic counts on physical EE, with three event pairs rotating every frame. Inclusive stage scopes overlap. Different event modes sample different variable-step frames; counts do not prove hardware-unit utilization or qualify frame FPS.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--log',required=True,type=Path)
    p.add_argument('--metadata',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    out=analyze(a.root,a.log,json.loads(a.metadata.read_text(encoding='utf8')))
    a.output.write_text(json.dumps(out,indent=2),encoding='utf8');print('1200 physical PMU frames validated; diagnostic only')
