"""Reject incomplete, numerically divergent or non-overlapping schedule screens."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import math
from collision_analyze import analyze


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--admission',type=Path,help='Required for timing with world probes disabled')
    a=p.parse_args();root=a.root
    meta=json.loads((root/'metadata.json').read_text(encoding='utf8'))
    layout=bool(meta.get('step_pool_comparison'))
    assert (meta.get('world_overlap_comparison') or layout) and meta['micro_dispatch']
    verify=meta['shadow_enabled'];expected=[12,13] if verify else meta['expected_phases']
    world_probe=verify or meta.get('world_overlap_timing_probe',True)
    log=(root/'hardware.log').read_text(encoding='utf8',errors='replace')
    errors=[];hashes={};pairs={};metrics=[]
    def check(value,reason):
        if not value:errors.append(reason)
    check(hashlib.sha256((root/'vehicle-playground.elf').read_bytes()).hexdigest()==meta['elf_sha256'],'ELF hash mismatch')
    if not verify and not world_probe:
        check(a.admission is not None,'missing admission report')
        if a.admission:
            admission=json.loads(a.admission.read_text(encoding='utf8'))
            check(admission['qualified'] and admission['metadata']['shadow_enabled'] and admission['metadata']['elf_sha256']==meta['elf_sha256'],'timing/admission ELF mismatch')
    check(f'CB DONE phases={14 if verify else 28}' in log,'incomplete hardware log')
    for bad in ['VUSATDIFF ','WORLDGRIDDIFF ','CB ERROR','Assertion failed','Failed to load','EE exception']:
        check(bad not in log,'log diagnostic: '+bad)
    check('VUSATCHECK cases=2048 failures=0 ' in log,'missing SAT startup gate')
    check('VUSATPRECISION cases=50000 failures=0' in log,'missing precision startup gate')
    files=sorted(root.glob('collision-phase-*.csv'))
    check([int(f.stem.rsplit('-',1)[1]) for f in files]==expected,'wrong phase set')
    for phase in expected:
        def rows(name):
            path=root/name;hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest()
            with path.open() as f:return list(csv.DictReader(f))
        samples=rows(f'collision-phase-{phase:02}.csv')
        check([int(x['frame']) for x in samples]==list(range(150,750)),f'phase {phase}: frames')
        h=samples[0];key=tuple(int(h[k]) for k in ['repeat','added','layout']);arm=int(h['baseline'])
        pair=pairs.setdefault(key,{})
        check(arm not in pair,f'phase {phase}: duplicate arm')
        probes=rows(f'world-overlap-{phase}.csv');byframe={int(r['frame']):r for r in probes}
        pair[arm]=(samples,byframe)
        for x in samples:
            check(int(x['generated_contacts'])==int(x['contacts']),f'phase {phase}: contacts')
            check(int(x['push_candidates'])==int(x['push_tested']),f'phase {phase}: pushes')
            check(int(x['dropped_contacts'])==int(x['push_overflow'])==0,f'phase {phase}: dropped')
            check(0<=int(x['physics_ticks'])<=int(x['work_ticks'])<=int(x['period_ticks']),f'phase {phase}: timer')
            check(sum(int(x[k]) for k in ['predict_ticks','world_ticks','body_ticks','solver_ticks','writeback_ticks'])<=int(x['physics_ticks']),f'phase {phase}: stage accounting')
            check(all(math.isfinite(float(x[k])) for k in ['car_z','car_speed','velocity_energy','max_depth']),f'phase {phase}: nonfinite')
            if world_probe:check(int(x['frame']) in byframe,f'phase {phase}: missing world probe')
        v=rows(f'vu0-phase-{phase:02}.csv');check(len(v)==1,f'phase {phase}: micro sidecar')
        v=v[0]
        check(int(v['jobs'])>0 and int(v['batches'])>0,f'phase {phase}: no micro work')
        check(int(v['validation_errors'])==int(v['missed_reference_hits'])==0,f'phase {phase}: micro errors')
        classification=rows(f'vu0-class-{phase}.csv')
        check(len(classification)==1 and int(classification[0]['jobs'])==int(v['jobs']),f'phase {phase}: classification jobs')
        if 'confirmed_positive' in classification[0]:
            check(sum(int(classification[0][k]) for k in ['proved_negative','confirmed_negative','confirmed_ccd','confirmed_tie','confirmed_parallel','confirmed_positive'])==int(v['jobs']),f'phase {phase}: incomplete result classes')
        world=rows(f'world-grid-{phase}.csv')
        check(len(world)==1,f'phase {phase}: world sidecar')
        world=world[0]
        check(int(world['errors'])==0 and int(world['verified'])==(int(world['queries']) if verify else 0),f'phase {phase}: world oracle')
        check(int(world['queries'])==0 if arm else int(world['queries'])>0,f'phase {phase}: world admission')
        totals={k:sum(int(r[k]) for r in probes) for k in ['world','packet','busy_before','busy_after','packet_world_ticks']}
        if world_probe:
            check(totals['world']>0,f'phase {phase}: no world work')
            if arm or layout:check(totals['packet']==totals['busy_before']==totals['busy_after']==0,f'phase {phase}: control overlap')
            else:check(totals['packet']>0 and totals['busy_before']>0,f'phase {phase}: no witnessed overlap')
        else:check(not probes and not any(totals.values()),f'phase {phase}: timing world probe active')
        wait_ms=int(v['wait_ticks'])/294912.0
        drive_frames=sum(x['state']=='drive' for x in samples)
        metrics.append(dict(phase=phase,world=totals,micro=v,world_admission=world,classification=classification[0],
            finish_wait_total_ms=wait_ms,finish_wait_mean_all750_ms=wait_ms/750,
            finish_wait_drive_mean_upper_ms=wait_ms/drive_frames,
            wait_scope='Profiled finish waitIdle interval including status/timer overhead, all750 phase frames. Drive upper bound assigns all phase wait to drive frames. Excludes pack/upload/start/readback and EE confirmations.'))
    differences=[]
    fields=['contacts','generated_contacts','awake','body_candidates','body_bounds_rejects','car_z','car_speed','velocity_energy','max_depth']
    for key,pair in pairs.items():
        check(set(pair)=={0,1},f'{key}: incomplete pair')
        if set(pair)!={0,1}:continue
        left,lp=pair[0];right,rp=pair[1]
        for x,y in zip(left,right):
            changed=[k for k in fields if x[k]!=y[k]]
            frame=int(x['frame'])
            if verify and frame in lp and frame in rp:
                changed += [k for k in ['contacts','hash_a','hash_b'] if lp[frame][k]!=rp[frame][k]]
            if changed:differences.append(dict(workload=key,frame=frame,fields=changed))
    check(not differences,'schedule workload/contact fingerprint mismatch')
    summary=analyze(root,solver_comparison=True,temporal_comparison=True,temporal_arms=2,
                    baseline_label='wide-step-micro' if layout else 'world-before-micro')
    for group in summary['groups']+summary.get('repeats',[]):
        if group['arm']=='cache-only':group['arm']='pooled-step-micro' if layout else 'world-interleaved-micro'
    result=dict(metadata=meta,qualified=not errors,performance_qualified=not verify and not world_probe and not errors,
        world_timing_probe_enabled=not verify and world_probe,
        normal_clock_fps_qualified=False,errors=sorted(set(errors)),metrics=metrics,
        mismatched_frames=len(differences),first_mismatches=differences[:12],summary=summary,
        raw_sha256=hashes,log_sha256=hashlib.sha256((root/'hardware.log').read_bytes()).hexdigest(),
        scope='Private same-ELF nominal-step schedule screen. Verification includes ordered contact fields in two 32-bit rolling fingerprints, not an exhaustive saved full-body bitwise state proof. Busy samples witness kernel activity at world-chunk boundaries, not utilization. Canonical micro validation remains enabled in the admission run; its timings are invalid.')
    if layout:result['scope']='Private same-ELF nominal-step wide/pooled pose layout comparison with the same world-first chained micro schedule. All original vertices and scalar arithmetic retained. Admission checks ordered contact fingerprints and canonical micro results; it is not a complete saved full-body state proof. Oracle timings are invalid; timing requires the same-ELF admission report and no world-window probes.'
    a.output.write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(dict(qualified=result['qualified'],errors=result['errors'],mismatched_frames=len(differences))))
    if errors:raise SystemExit(1)


if __name__=='__main__':main()
