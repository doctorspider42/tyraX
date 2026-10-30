"""Qualify complete same-ELF scalar8m/tuned-grid frame timing and accounting."""
from pathlib import Path
import argparse,csv,hashlib,json,math
from collision_analyze import analyze

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path)
    p.add_argument('--log',type=Path,required=True);p.add_argument('--metadata',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    meta=json.loads(a.metadata.read_text(encoding='utf8'));log=a.log.read_text(encoding='utf8',errors='replace')
    assert meta.get('body_grid_timing_cell') in [1,2,4] and meta.get('micro_dispatch') is False
    assert meta.get('body_grid_probe') is False and meta.get('shadow_enabled') is False
    assert 'BODYGRID ' not in log and 'VUSATDIFF ' not in log and 'CB ERROR' not in log
    assert 'CB DONE phases=28' in log and 'VUSAT mode=timing' in log
    assert 'VUSATCHECK cases=2048 failures=0 ' in log and 'VUSATPRECISION cases=50000 failures=0' in log
    hashes={};count=0
    files=sorted(a.root.glob('collision-phase-*.csv'))
    assert [int(f.stem.rsplit('-',1)[1])for f in files]==list(range(28))
    for f in files:
        rows=list(csv.DictReader(f.open()));phase=int(f.stem.rsplit('-',1)[1])
        assert len(rows)==600 and [int(r['frame'])for r in rows]==list(range(150,750)),f
        for r in rows:
            assert int(r['generated_contacts'])==int(r['contacts']) and int(r['push_candidates'])==int(r['push_tested']),f
            assert int(r['dropped_contacts'])==int(r['push_overflow'])==0,f
            assert all(math.isfinite(float(r[k]))for k in ['car_z','car_speed','velocity_energy','max_depth']),f
        side=a.root/f'vu0-phase-{phase:02d}.csv';v=list(csv.DictReader(side.open()));assert len(v)==1
        assert all(int(v[0][k])==0 for k in ['jobs','batches','busy_ee_query','validation_errors','missed_reference_hits']),side
        classification=a.root/f'vu0-class-{phase}.csv';c=list(csv.DictReader(classification.open()));assert len(c)==1
        assert all(int(value)==0 for key,value in c[0].items()if key!='phase'),classification
        for source in [f,side,classification]:hashes[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
        count+=len(rows)
    result=analyze(a.root,complete=True,baseline_label='grid8m-scalar',solver_comparison=True,temporal_comparison=True,temporal_arms=2)
    def labels(x):
        if isinstance(x,dict):return {k:labels(v)for k,v in x.items()}
        if isinstance(x,list):return [labels(v)for v in x]
        return f'grid{meta["body_grid_timing_cell"]}m-scalar' if x=='cache-only' else x
    out=dict(metadata=meta,measured_frames=count,summary=labels(result),raw_sha256=hashes,
        log_sha256=hashlib.sha256(a.log.read_bytes()).hexdigest(),
        scope='Complete same-ELF physical frame comparison, canonical scalar SAT/CCD and6+3 solver passes in both arms. Zero micro dispatch and no snapshot probes during measured frames. Per-arm live trajectories use the original variable timestep; geometry admission is qualified independently on fixed snapshots and the host all-pairs oracle.')
    a.output.write_text(json.dumps(out,indent=2),encoding='utf8');print(f'{count} frame rows qualified, complete accounting in both arms, no micro dispatch')
if __name__=='__main__':main()
