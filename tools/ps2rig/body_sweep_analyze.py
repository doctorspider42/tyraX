"""Independent admission/snapshot checks for the sorted-prefix body index."""
from pathlib import Path
import argparse,csv,hashlib,json,math,struct,subprocess,sys

def valid(b):return all(math.isfinite(x) for x in b) and all(b[k]<=b[k+3] for k in range(3))
def overlap(a,b):return all(a[k]<=b[k+3] and a[k+3]>=b[k] for k in range(3))
def snapshot(path):
    data=path.read_bytes();magic,version,nb,nq=struct.unpack_from('<4I',data);assert magic==0x57535954 and version==1
    offset=16;boxes=[]
    for _ in range(nb):boxes.append(struct.unpack_from('<6f',data,offset));offset+=24
    for _ in range(nq):
        area=struct.unpack_from('<6f',data,offset);offset+=24
        count,=struct.unpack_from('<I',data,offset);offset+=4
        hits=list(struct.unpack_from('<'+str(count)+'i',data,offset));offset+=4*count
        expected=[i for i,b in enumerate(boxes) if not valid(area) or not valid(b) or overlap(area,b)]
        assert hits==expected,path
    assert offset==len(data),path
    return dict(file=path.name,bodies=nb,queries=nq,sha256=hashlib.sha256(data).hexdigest())

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--log',required=True,type=Path)
    p.add_argument('--metadata',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    meta=json.loads(a.metadata.read_text(encoding='utf8'));assert meta.get('body_sweep_comparison')
    verify=bool(meta['body_sweep_verify']);assert bool(meta['shadow_enabled']) is verify
    subprocess.run([sys.executable,str(Path(__file__).with_name('body_grid_timing_analyze.py')),str(a.root),'--log',str(a.log),'--metadata',str(a.metadata),'--output',str(a.output)],check=True)
    result=json.loads(a.output.read_text(encoding='utf8'));log=a.log.read_text(encoding='utf8',errors='replace');assert 'BODYSWEEPDIFF ' not in log
    expected=[12,13] if verify else meta['expected_phases'];metrics=[];snapshots=[]
    for phase in expected:
        source=a.root/f'body-sweep-{phase}.csv';rows=list(csv.DictReader(source.open()));assert len(rows)==1
        r={k:int(v) for k,v in rows[0].items()};assert r['phase']==phase and r['errors']==0
        assert f'BODYSWEEP phase={phase} queries={r["queries"]} errors=0 snapshots={r["snapshots"]} file=1' in log
        if not verify or phase==12:assert r['queries']==r['snapshots']==0
        files=sorted(a.root.glob(f'body-sweep-{phase}-*.bin'));assert len(files)==r['snapshots']
        snapshots.extend(snapshot(f) for f in files)
        metrics.append(r);result['raw_sha256'][source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
    if verify:assert sum(r['queries'] for r in metrics)>0
    def labels(x):
        if isinstance(x,dict):return {k:labels(v) for k,v in x.items()}
        if isinstance(x,list):return [labels(v) for v in x]
        if x=='combined-world-scalar':return 'grid8m-scalar'
        if x=='world-index-scalar':return 'body-sweep-scalar'
        return x
    result['summary']=labels(result['summary']);result['body_sweep_metrics']=metrics;result['snapshots']=snapshots
    result['performance_qualified']=not verify;result['normal_clock_fps_qualified']=not verify and not meta.get('fixed_simulation_step')
    result['scope']=('Every sorted-prefix body query independently checked against full-walk admission, with exact original body-index order; snapshot replay is also independent. Oracle overhead invalidates frame performance. ' if verify else
        'Same-ELF controlled nominal-step body-index timing with strict printed workload parity; no oracle/replay/PMU work in measured frames. ')+\
        'Both arms retain cached world admission, generic point planes, canonical scalar SAT/CCD and six/three solver passes. Sorted index uses linear storage and conservative invalid-bound fallback, with no pair/contact cap. Production clock unchanged.'
    a.output.write_text(json.dumps(result,indent=2),encoding='utf8');print(f'Body sweep: {sum(r["queries"] for r in metrics)} independent queries, {len(snapshots)} snapshots, zero errors')

if __name__=='__main__':main()
