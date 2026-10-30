"""Qualify complete same-ELF scalar8m/tuned-grid frame timing and accounting."""
from pathlib import Path
import argparse,csv,hashlib,json,math
from collision_analyze import analyze

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path)
    p.add_argument('--log',type=Path,required=True);p.add_argument('--metadata',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    meta=json.loads(a.metadata.read_text(encoding='utf8'));log=a.log.read_text(encoding='utf8',errors='replace')
    world=bool(meta.get('world_grid_comparison'));verify=world and bool(meta.get('world_grid_verify'))
    assert meta.get('body_grid_timing_cell') in ([1,2,4,8]if world else [1,2,4]) and meta.get('micro_dispatch') is False
    assert meta.get('body_grid_probe') is False and meta.get('shadow_enabled') is verify
    assert 'BODYGRID ' not in log and 'VUSATDIFF ' not in log and 'CB ERROR' not in log
    assert f'CB DONE phases={14 if verify else 28}' in log and f'VUSAT mode={"verify" if verify else "timing"}' in log
    assert 'VUSATCHECK cases=2048 failures=0 ' in log and 'VUSATPRECISION cases=50000 failures=0' in log
    if meta.get('fixed_simulation_step'):assert 'FIXEDSTEP benchmark_nominal_display_step hz=' in log
    hashes={};count=0;world_metrics=[];paired={};parity=None
    files=sorted(a.root.glob('collision-phase-*.csv'))
    expected=[12,13]if verify else meta.get('expected_phases',list(range(28)))
    assert [int(f.stem.rsplit('-',1)[1])for f in files]==expected
    for f in files:
        rows=list(csv.DictReader(f.open()));phase=int(f.stem.rsplit('-',1)[1])
        assert len(rows)==600 and [int(r['frame'])for r in rows]==list(range(150,750)),f
        key=tuple(int(rows[0][k])for k in ['repeat','added','layout'])
        pair=paired.setdefault(key,{});arm=int(rows[0]['baseline']);assert arm not in pair;pair[arm]=rows
        for r in rows:
            assert int(r['generated_contacts'])==int(r['contacts']) and int(r['push_candidates'])==int(r['push_tested']),f
            assert int(r['dropped_contacts'])==int(r['push_overflow'])==0,f
            assert all(math.isfinite(float(r[k]))for k in ['car_z','car_speed','velocity_energy','max_depth']),f
        side=a.root/f'vu0-phase-{phase:02d}.csv';v=list(csv.DictReader(side.open()));assert len(v)==1
        assert all(int(v[0][k])==0 for k in ['jobs','batches','busy_ee_query','validation_errors','missed_reference_hits']),side
        classification=a.root/f'vu0-class-{phase}.csv';c=list(csv.DictReader(classification.open()));assert len(c)==1
        assert all(int(value)==0 for key,value in c[0].items()if key!='phase'),classification
        for source in [f,side,classification]:hashes[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
        if world:
            assert 'WORLDGRIDDIFF ' not in log
            source=a.root/f'world-grid-{phase}.csv';items=list(csv.DictReader(source.open()));assert len(items)==1
            w={k:int(value)for k,value in items[0].items()};assert w['phase']==phase and w['errors']==0
            assert w['verified']==(w['queries']if verify else 0)
            assert f'WORLDGRID phase={phase} queries={w["queries"]} verified={w["verified"]} errors=0' in log
            if meta.get('world_index_min_awake'):
                assert 'builds' in w and w['builds']>=0
                assert f'errors=0 builds={w["builds"]}' in log
            if 'builds' in w:assert w['builds']>=0
            if int(rows[0]['baseline']):
                assert w['queries']==0
                # Plane arithmetic controls share the selected cached-world
                # backend; its baseline legitimately builds the same index.
                if not meta.get('plane_sparse_comparison'):assert w.get('builds',0)==0
            world_metrics.append(w);hashes[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
        count+=len(rows)
    if world:assert sum(w['queries']for w in world_metrics)>0
    if meta.get('fixed_simulation_step'):
        fields=['contacts','generated_contacts','awake','body_candidates','body_bounds_rejects','car_z','car_speed','velocity_energy','max_depth']
        differences=[]
        for key,pair in paired.items():
            assert set(pair)=={0,1},key
            for left,right in zip(pair[0],pair[1]):
                changed=[k for k in fields if left[k]!=right[k]]
                if changed:differences.append(dict(workload=key,frame=int(left['frame']),fields=changed))
        parity=dict(pairs=len(paired),frames_per_arm=sum(len(v[0])for v in paired.values()),fields=fields,
            passed=not differences,mismatched_frames=len(differences),first_mismatches=differences[:8],
            resolution='Exact integer counters and equality at printed floating-point resolution; not a bitwise full-body state proof.')
    result=analyze(a.root,complete=not verify and expected==list(range(28)),baseline_label='combined-world-scalar'if world else 'grid8m-scalar',solver_comparison=True,temporal_comparison=True,temporal_arms=2)
    def labels(x):
        if isinstance(x,dict):return {k:labels(v)for k,v in x.items()}
        if isinstance(x,list):return [labels(v)for v in x]
        return ('world-index-scalar'if world else f'grid{meta["body_grid_timing_cell"]}m-scalar') if x=='cache-only' else x
    out=dict(metadata=meta,measured_frames=count,summary=labels(result),raw_sha256=hashes,world_metrics=world_metrics,logged_workload_parity=parity,
        qualified=parity is None or parity['passed'],
        log_sha256=hashlib.sha256(a.log.read_bytes()).hexdigest(),
        scope=('World-only admission verification, independent full walk on every world query; shadow overhead invalidates frame timings. 'if verify else 'Complete same-ELF physical frame comparison. ')+
            'Canonical scalar SAT/CCD and6+3 solver passes in both arms. Zero micro dispatch and no snapshot probes during measured frames. '+
            ('Private benchmark pins simulation to nominal display steps in both arms; wall-time throughput is measured, production clock unchanged. 'if meta.get('fixed_simulation_step')else 'Per-arm live trajectories use the original variable timestep. ')+
            'Geometry admission is qualified independently on fixed snapshots and the host all-pairs oracle.')
    a.output.write_text(json.dumps(out,indent=2),encoding='utf8')
    assert out['qualified'],'Controlled workload diverged; saved report is explicitly unqualified'
    print(f'{count} frame rows qualified, complete accounting in both arms, no micro dispatch')
if __name__=='__main__':main()
