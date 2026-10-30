"""Attribute sampled current-pipeline intervals; never qualify production FPS."""
from pathlib import Path
import argparse,csv,hashlib,json,statistics
from step_pool_diff import decode,differences

COMPONENTS=['empty_scope','corners','prepare_hint','legacy_pack','shared_body_pack','pair_append','upload_start','finish_readback','consume','reconstruct','emit','solver_prepare','solver_warm','velocity_solve','normal_impulse','friction','position_solve','solver_history']
def read(path):
    with path.open() as f:return list(csv.DictReader(f))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--gate',type=Path,required=True)
    p.add_argument('--previous',type=Path,required=True,help='Frozen uninstrumented-source dense admission')
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    meta=json.loads((a.root/'metadata.json').read_text(encoding='utf8'));gate=json.loads(a.gate.read_text(encoding='utf8'))
    errors=[];drift=[];phases=[];hashes={}
    def check(value,reason):
        if not value:errors.append(reason)
    check(meta.get('pipeline_cost_profile') and meta.get('pipeline_cost_enabled'),'missing sampled profiler configuration')
    check(gate['qualified'] and gate['metadata']['elf_sha256']==meta['elf_sha256'],'wrong/failed same-ELF gate')
    check(hashlib.sha256((a.root/'vehicle-playground.elf').read_bytes()).hexdigest()==meta['elf_sha256'],'ELF hash')
    for phase in [12,13]:
        path=a.root/f'pipeline-cost-{phase}.csv';hashes[path.name]=hashlib.sha256(path.read_bytes()).hexdigest();rows=read(path)
        byframe={}
        for row in rows:
            frame=int(row['frame']);bucket=byframe.setdefault(frame,{})
            check(row['component'] not in bucket,f'duplicate component phase{phase} frame{frame}');bucket[row['component']]=row
            check(int(row['phase'])==phase and int(row['calls'])>=0 and int(row['ticks'])>=0,'invalid profiler row')
        check(sorted(byframe)==list(range(0,750,16)),f'phase{phase} incomplete sample frame set')
        for frame,buckets in byframe.items():
            check(set(buckets)==set(COMPONENTS),f'phase{phase} frame{frame} missing component')
            check(int(buckets['empty_scope']['calls'])==64,'missing timer calibration')
            check(int(buckets['normal_impulse']['calls'])==int(buckets['friction']['calls']),'changed solver iteration count')
            check(int(buckets['solver_prepare']['calls'])==int(buckets['velocity_solve']['calls'])==int(buckets['position_solve']['calls']) and int(buckets['solver_prepare']['calls']) in [0,1],'invalid coarse solver intervals')
        samples=read(a.root/f'collision-phase-{phase:02}.csv');previous=read(a.previous/f'collision-phase-{phase:02}.csv')
        fields=['contacts','generated_contacts','awake','body_candidates','body_bounds_rejects','car_z','car_speed','velocity_energy','max_depth']
        check(len(samples)==len(previous)==600,'wrong scene sample count')
        for x,y in zip(samples,previous):
            changed=[k for k in ['frame']+fields if x[k]!=y[k]]
            if changed:drift.append(dict(phase=phase,frame=int(x['frame']),fields=changed))
        if meta['shadow_enabled']:
            for frame in [0,1]:
                x=decode(a.root/f'step-pool-{phase}-{frame}.bin');y=decode(a.previous/f'step-pool-{phase}-{frame}.bin')
                if len(x['steps'])!=len(y['steps']) or len(x['contacts'])!=len(y['contacts']) or differences(x['steps'],y['steps']) or differences(x['contacts'],y['contacts']):
                    drift.append(dict(phase=phase,frame=frame,fields=['raw initialized state/contact snapshot']))
        groups={}
        frame_state={int(r['frame']):r['state'] for r in samples}
        sample_byframe={int(r['frame']):r for r in samples}
        for frame,buckets in byframe.items():
            if frame in sample_byframe:
                check(int(buckets['normal_impulse']['calls'])==6*int(sample_byframe[frame]['contacts']),f'phase{phase} frame{frame} normal solve coverage')
                if int(sample_byframe[frame]['contacts']):check(int(buckets['solver_prepare']['calls'])==1,f'phase{phase} frame{frame} missing contact preparation')
        for state in ['all750_sampled','park','drive']:
            selected=[b for f,b in byframe.items() if state=='all750_sampled' or frame_state.get(f)==state]
            check(bool(selected),f'phase{phase} no samples {state}');n=len(selected)
            groups[state]=dict(sampled_frames=n,components={k:dict(mean_ms_per_sampled_frame=sum(int(b[k]['ticks']) for b in selected)/294912/n,
                mean_calls_per_sampled_frame=sum(int(b[k]['calls']) for b in selected)/n,
                mean_ticks_per_call=sum(int(b[k]['ticks']) for b in selected)/max(1,sum(int(b[k]['calls']) for b in selected))) for k in COMPONENTS})
        phases.append(dict(phase=phase,groups=groups))
    check(not drift,'instrumented source workload differs from preceding uninstrumented-source comparison')
    result=dict(metadata=meta,qualified=not errors,attribution_qualified=not meta['shadow_enabled'] and not errors,
        performance_qualified=False,errors=sorted(set(errors)),baseline_drift_frames=len(drift),first_baseline_drift=drift[:8],phases=phases,
        raw_sha256=hashes,gate_sha256=hashlib.sha256(a.gate.read_bytes()).hexdigest(),
        scope='Every16th nominal physics frame. Means normalize only sampled frames, grouped by their actual state. Velocity includes normal/friction scopes; consume includes reconstruction and some emission; append does not include a full-packet flush. Timers perturb execution. Empty scopes expose timer overhead but are not an exact per-category correction. Never sum inclusive intervals, extrapolate to production FPS, or price a different trajectory silently.')
    a.output.write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(dict(qualified=result['qualified'],attribution_qualified=result['attribution_qualified'],errors=result['errors'],baseline_drift_frames=len(drift))))
    if errors:raise SystemExit(1)

if __name__=='__main__':main()
