"""Validate private EE attribution; nested scopes are not additive components."""
from pathlib import Path
import argparse,csv,hashlib,json,math


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path)
    p.add_argument('--output',required=True,type=Path);p.add_argument('--admission',type=Path);a=p.parse_args()
    root=a.root;meta=json.loads((root/'metadata.json').read_text());assert meta.get('micro_cost_profile')
    verify=meta['shadow_enabled'];errors=[];hashes={};phases=[];paired=[]
    def check(ok,why):
        if not ok:errors.append(why)
    def rows(name):
        f=root/name;hashes[name]=hashlib.sha256(f.read_bytes()).hexdigest()
        with f.open() as stream:return list(csv.DictReader(stream))
    check(hashlib.sha256((root/'vehicle-playground.elf').read_bytes()).hexdigest()==meta['elf_sha256'],'ELF mismatch')
    log=(root/'hardware.log').read_text(errors='replace')
    for required in ['CB DONE phases=14','MICROCOST mode=profile','VUSATCHECK cases=2048 failures=0 ','VUSATPRECISION cases=50000 failures=0']:
        check(required in log,'missing '+required)
    for bad in ['CB ERROR','VUSATDIFF ','WORLDGRIDDIFF ','Assertion failed','Failed to load','EE exception']:
        check(bad not in log,'diagnostic '+bad)
    if not verify:
        check(a.admission is not None,'missing admission')
        if a.admission:
            admission=json.loads(a.admission.read_text());check(admission['qualified'] and admission['metadata']['shadow_enabled'] and admission['metadata']['elf_sha256']==meta['elf_sha256'],'admission mismatch')
    check([int(f.stem.rsplit('-',1)[1]) for f in sorted(root.glob('collision-phase-*.csv'))]==[12,13],'phase set')
    components=['corners','prepare','pack','upload_start','finish_readback','consume','reconstruct','emit']
    for phase in [12,13]:
        samples=rows(f'collision-phase-{phase}.csv');check([int(r['frame']) for r in samples]==list(range(150,750)),'frame coverage')
        for r in samples:
            check(int(r['contacts'])==int(r['generated_contacts']) and int(r['dropped_contacts'])==int(r['push_overflow'])==0,'contact accounting')
            check(r['push_candidates']==r['push_tested'],'push accounting')
            check(0<=int(r['physics_ticks'])<=int(r['work_ticks'])<=int(r['period_ticks']),'timer accounting')
            check(all(math.isfinite(float(r[k])) for k in ['car_z','car_speed','velocity_energy','max_depth']),'nonfinite state')
        world=rows(f'world-grid-{phase}.csv')[0];check(int(world['errors'])==0 and int(world['verified'])==(int(world['queries']) if verify else 0),'world oracle')
        v=rows(f'vu0-phase-{phase}.csv')[0];check(int(v['jobs'])>0 and int(v['validation_errors'])==int(v['missed_reference_hits'])==0,'micro validation')
        c=rows(f'micro-cost-{phase}.csv');check([r['component'] for r in c]==components,'cost component set')
        buckets={r['component']:r for r in c}
        for r in c:check(int(r['phase'])==phase and int(r['enabled'])==1 and int(r['calls'])>0 and int(r['ticks'])>0,'cost coverage')
        for k in ['upload_start','finish_readback','consume']:check(int(buckets[k]['calls'])==int(v['batches']),'batch count '+k)
        check(int(buckets['reconstruct']['calls'])==int(v['jobs']),'reconstruction count')
        check(int(buckets['corners']['calls'])>=int(buckets['prepare']['calls'])>=int(buckets['pack']['calls'])>=int(v['jobs']),'preparation count')
        check(int(buckets['consume']['ticks'])>=int(buckets['reconstruct']['ticks']),'nested consume scope')
        check(int(buckets['finish_readback']['ticks'])>=int(v['wait_ticks']),'nested finish scope')
        probe=rows(f'world-overlap-{phase}.csv');lookup={int(r['frame']):r for r in probe}
        if verify:check(all(int(r['packet'])==0 for r in probe),'unexpected interleaving')
        else:check(not probe,'timing world probe active')
        paired.append((samples,lookup))
        phases.append(dict(phase=phase,components={k:dict(calls=int(r['calls']),ticks=int(r['ticks']),
            ms_per_all750_frames=int(r['ticks'])/294912/750,us_per_call=int(r['ticks'])/294.912/int(r['calls'])) for k,r in buckets.items()},micro=v))
    mismatch=[]
    fields=['contacts','generated_contacts','awake','body_candidates','body_bounds_rejects','car_z','car_speed','velocity_energy','max_depth']
    for x,y in zip(paired[0][0],paired[1][0]):
        changed=[k for k in fields if x[k]!=y[k]];frame=int(x['frame'])
        if verify:
            lp,rp=paired[0][1],paired[1][1];check(frame in lp and frame in rp,'missing contact fingerprint')
            if frame in lp and frame in rp:changed += [k for k in ['contacts','hash_a','hash_b'] if lp[frame][k]!=rp[frame][k]]
        if changed:mismatch.append(dict(frame=frame,fields=changed))
    check(not mismatch,'duplicate schedule mismatch')
    result=dict(metadata=meta,qualified=not errors,attribution_qualified=not verify and not errors,
        performance_qualified=False,errors=sorted(set(errors)),phases=phases,mismatched_frames=len(mismatch),first_mismatches=mismatch[:8],raw_sha256=hashes,
        log_sha256=hashlib.sha256((root/'hardware.log').read_bytes()).hexdigest(),
        scope='All750 phase frames including150 warm frames. Consume includes reconstruct and some emit work. Finish includes wait/readback. Other costs are not included in these buckets. Never sum overlapping buckets or compare their750-frame means to600-frame scene means as fractions. Instrumentation perturbs execution; this is attribution, not production FPS. Ordered contact fingerprints are probabilistic, not a complete full-body state proof.')
    a.output.write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(dict(qualified=result['qualified'],errors=result['errors'],mismatched_frames=len(mismatch))))
    if errors:raise SystemExit(1)


if __name__=='__main__':main()
