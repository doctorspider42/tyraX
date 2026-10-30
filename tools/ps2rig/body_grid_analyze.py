"""Validate and summarize fixed-snapshot EE grid timings (not scene FPS)."""
from pathlib import Path
import argparse,csv,hashlib,json,math,statistics,struct

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path)
    p.add_argument('--log',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    log=a.log.read_text(encoding='utf8',errors='replace');summary=[];hashes={}
    for frame in [0,150,300,600]:
        name=f'body-grid-13-{frame}';raw=(a.root/(name+'.bin')).read_bytes()
        magic,version,objects,queries,phase,stored_frame=struct.unpack_from('<6I',raw)
        assert (magic,version,phase,stored_frame)==(0x47524944,1,13,frame)
        assert 0<objects<100000 and queries<100000 and len(raw)==24+(objects+queries)*24
        values=struct.unpack_from(f'<{(objects+queries)*6}f',raw,24)
        assert all(math.isfinite(v)for v in values)
        assert all(values[i+k]<=values[i+3+k]for i in range(0,len(values),6)for k in range(3))
        input_hash=2166136261
        for byte in raw[24:]:input_hash=((input_hash^byte)*16777619)&0xffffffff
        rows=list(csv.DictReader((a.root/(name+'.csv')).open()))
        assert len(rows)==32 and {(int(r['cell']),int(r['repeat']))for r in rows}=={(c,r)for c in [8,4,2,1]for r in range(8)}
        assert all(int(r['input_hash'])==input_hash and int(r['objects'])==objects and int(r['queries'])==queries and int(r['loops'])==16 and int(r['phase'])==13 and int(r['frame'])==frame for r in rows)
        assert all(int(r['build_ticks'])>0 and int(r['query_ticks'])>0 for r in rows)
        assert f'BODYGRID phase=13 frame={frame} objects={objects} queries={queries} failures=0' in log
        modes=[]
        for cell in [8,4,2,1]:
            selected=[r for r in rows if int(r['cell'])==cell]
            assert len({r['digest']for r in selected})==1
            build=[int(r['build_ticks'])/294.912 for r in selected]
            query=[int(r['query_ticks'])/294.912/16 for r in selected]
            modes.append(dict(cell=cell,median_build_us=statistics.median(build),median_query_pass_us=statistics.median(query),
                median_build_plus_query_us=statistics.median(x+y for x,y in zip(build,query)),build_samples_us=build,query_pass_samples_us=query))
        summary.append(dict(phase=13,frame=frame,objects=objects,queries=queries,modes=modes))
        for suffix in ['.bin','.csv']:
            f=a.root/(name+suffix);hashes[f.name]=hashlib.sha256(f.read_bytes()).hexdigest()
    assert 'BODYGRID ' in log and 'failures=1' not in '\n'.join(line for line in log.splitlines()if line.startswith('BODYGRID '))
    out=dict(summary=summary,raw_sha256=hashes,log_sha256=hashlib.sha256(a.log.read_bytes()).hexdigest(),
        scope='Physical EE fixed snapshots: freshly allocated grid build and16 repeated query passes, eight reversing-order samples. Float AABB eligibility and sorted order checked against independent full walk outside timing. No SAT/solver; not whole-scene FPS. Snapshot capture/probes perturb the live scene and invalidate its frame timings.')
    a.output.write_text(json.dumps(out,indent=2),encoding='utf8')
    for s in summary:print(f'frame={s["frame"]} objects={s["objects"]} queries={s["queries"]}: '+', '.join(f'{m["cell"]}m={m["median_build_plus_query_us"]:.1f}us'for m in s['modes']))
if __name__=='__main__':main()
