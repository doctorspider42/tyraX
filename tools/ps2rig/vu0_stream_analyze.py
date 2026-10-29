"""Summarize fixed-input physical SAT/CCD scheduling and packing costs."""
from pathlib import Path
import argparse,csv,hashlib,json,statistics

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv',type=Path);p.add_argument('--log',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    rows=list(csv.DictReader(a.csv.open()))
    assert len(rows)==80
    assert {(int(r['workload']),int(r['repeat']),int(r['mode']))for r in rows}=={
        (w,r,m)for w in range(2)for r in range(8)for m in range(5)}
    assert all(int(r['ticks'])>0 and int(r['jobs'])==1792 for r in rows)
    for r in rows:
        if 'confirmed_ccd' in r:
            confirmations=[int(r[k])for k in ['confirmed_ccd','confirmed_tie','confirmed_parallel']]
            assert all(n>=0 for n in confirmations) and sum(confirmations)<=int(r['jobs'])
            if int(r['mode'])in [0,3]:assert sum(confirmations)==0
    log=a.log.read_text(encoding='utf8',errors='replace')
    assert 'VUSTREAM cases=224 repeats=8 failures=0 file=1' in log
    for w in range(2):
        for repeat in range(8):
            same=[r for r in rows if int(r['workload'])==w and int(r['repeat'])==repeat and int(r['mode'])in [1,2,4]]
            assert len({r['hash']for r in same})==1
    modes=['scalar','previous-micro','chained-micro','packing-only','prepacked-chained']
    result=[]
    for workload in range(2):
        entry=dict(workload=['mixed','coincident-centres'][workload],modes=[])
        for mode,name in enumerate(modes):
            selected=[r for r in rows if int(r['workload'])==workload and int(r['mode'])==mode]
            us=[int(r['ticks'])/294.912/int(r['jobs'])for r in selected]
            entry['modes'].append(dict(mode=name,median_us_per_pair=statistics.median(us),
                min_us_per_pair=min(us),max_us_per_pair=max(us),samples_us_per_pair=us,
                mean_wait_us_per_pair=statistics.mean(int(r['wait_ticks'])/294.912/int(r['jobs'])for r in selected)))
            if 'confirmed_ccd' in selected[0]:
                entry['modes'][-1]['mean_confirmation_fraction']={k:statistics.mean(int(r[k])/int(r['jobs'])for r in selected)
                    for k in ['confirmed_ccd','confirmed_tie','confirmed_parallel']}
        result.append(entry)
    out=dict(summary=result,rows=rows,csv_sha256=hashlib.sha256(a.csv.read_bytes()).hexdigest(),
             log_sha256=hashlib.sha256(a.log.read_bytes()).hexdigest(),
             scope='Fixed-input SAT/CCD backend including contact reconstruction and integer hashing. Full packing for modes1/2; packet staging copy for mode4. No broad phase, solver, file I/O or scalar shadow in timing. Scalar geometry may differ within qualified tolerances. Not whole-scene FPS.')
    a.output.write_text(json.dumps(out,indent=2),encoding='utf8')
    for entry in result:
        print(entry['workload'],', '.join(f'{m["mode"]}={m["median_us_per_pair"]:.3f} us/pair'for m in entry['modes']))

if __name__=='__main__':main()
