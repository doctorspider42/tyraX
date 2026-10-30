"""Validate exact plane face/depth shadows and uninstrumented scene accounting."""
from pathlib import Path
import argparse,csv,hashlib,json,subprocess,sys

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--log',required=True,type=Path)
    p.add_argument('--metadata',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    meta=json.loads(a.metadata.read_text(encoding='utf8'));assert meta.get('plane_sparse_comparison')
    verify=bool(meta['plane_sparse_verify']);assert bool(meta['shadow_enabled']) is verify
    subprocess.run([sys.executable,str(Path(__file__).with_name('body_grid_timing_analyze.py')),str(a.root),'--log',str(a.log),'--metadata',str(a.metadata),'--output',str(a.output)],check=True)
    result=json.loads(a.output.read_text(encoding='utf8'));log=a.log.read_text(encoding='utf8',errors='replace')
    assert 'PLANESPARSEDIFF ' not in log
    expected=[12,13] if verify else list(range(28));metrics=[]
    for phase in expected:
        source=a.root/f'plane-sparse-{phase}.csv';rows=list(csv.DictReader(source.open()));assert len(rows)==1
        r={k:int(v) for k,v in rows[0].items()};assert r['phase']==phase and r['errors']==0 and r['checked']<=r['calls']
        assert r['eligible_shapes']<=r['shapes'] and r['guard_fallback']<=r['calls']-r['checked']
        assert f'PLANESPARSE phase={phase} calls={r["calls"]} checked={r["checked"]} errors=0 shapes={r["shapes"]} eligible_shapes={r["eligible_shapes"]} guard_fallback={r["guard_fallback"]} file=1' in log
        if not verify:assert all(v==0 for k,v in r.items() if k!='phase')
        metrics.append(r);result['raw_sha256'][source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
    if verify:assert sum(r['checked'] for r in metrics)>0 and sum(r['eligible_shapes'] for r in metrics)>0
    def labels(x):
        if isinstance(x,dict):return {k:labels(v) for k,v in x.items()}
        if isinstance(x,list):return [labels(v) for v in x]
        if x=='combined-world-scalar':return 'generic-planes-scalar'
        if x=='world-index-scalar':return 'sparse-planes-scalar'
        return x
    result['summary']=labels(result['summary']);result['plane_metrics']=metrics
    result['performance_qualified']=not verify
    result['admission_qualified']=verify or bool(meta.get('same_elf_admission_qualified'))
    result['scope']=('Exact face/depth bit shadow for every eligible point query; candidate and independent world-admission oracle overhead invalidate FPS. ' if verify else 'Complete same-ELF normal-clock frame comparison, no plane/world oracle or PMU/replay work in measured frames. ')+\
        'Both arms use selected cached world admission,8m body grid, canonical scalar SAT/CCD and6+3 solver. Exact six-plane normal classification and normal-point guard retain full generic fallback. Variable-step trajectories can differ.'
    a.output.write_text(json.dumps(result,indent=2),encoding='utf8')
    print(f'Plane sparse: {sum(r["checked"] for r in metrics)} exact shadow comparisons, zero errors; frames={result["measured_frames"]}')

if __name__=='__main__':main()
