"""Decode external EE PC samples, including a known-loop calibration gate."""
from pathlib import Path
import argparse,bisect,collections,csv,hashlib,json,re,struct

def decode(path):
    data=path.read_bytes();assert len(data)>=40
    h=struct.unpack_from('<10I',data);assert h[0]==0x50435331 and h[1]==1 and h[7]==16
    assert h[4]<=65536 and len(data)==40+h[4]*16 and h[5]==0
    records=list(struct.iter_unpack('<4I',data[40:]));phase=h[2] if h[2]<0x80000000 else h[2]-0x100000000
    assert all(0x100000<=r[0]<0x2000000 and (phase<0 or r[1]<750) for r in records)
    return dict(phase=phase,enabled=bool(h[3]),count=h[4],lost=h[5],period=h[6],callbacks=h[8],calibration=h[9],records=records,sha256=hashlib.sha256(data).hexdigest())

def symbols(path):
    result=[]
    for line in path.read_text(encoding='utf8').splitlines():
        m=re.match(r'^([0-9a-fA-F]+) ([0-9a-fA-F]+) [tTwW] (.+)$',line)
        if m and int(m[2],16)>0:result.append((int(m[1],16),int(m[2],16),m[3]))
    return sorted(set(result))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--symbols',type=Path,required=True)
    p.add_argument('--reference',type=Path,help='Optional frozen trajectory comparison, independent of timers')
    p.add_argument('--source-map',type=Path,help='Verified unchanged instruction spans for the candidate function')
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();table=symbols(a.symbols);starts=[r[0] for r in table]
    def name(pc):
        i=bisect.bisect_right(starts,pc)-1
        return table[i][2] if i>=0 and pc<table[i][0]+table[i][1] else '<unmapped>'
    startup=decode(a.root/'pc-sample-startup.bin');errors=[]
    def check(value,reason):
        if not value:errors.append(reason)
    check(startup['phase']==-1,'wrong calibration phase')
    if startup['enabled']:
        count=sum('PcSample::calibrate()' in name(r[0]) for r in startup['records'])
        check(startup['count']>=20 and count/startup['count']>.8,'known-loop PC calibration failed')
        check('PcSample::calibrate()' in name(startup['calibration']),'wrong calibration symbol')
    else:check(startup['count']==0,'disabled calibration sampled instructions')
    source_map=json.loads(a.source_map.read_text(encoding='utf8')) if a.source_map else None
    base=next((r[0] for r in table if r[2]=='Vehicle_playground::TerrainGame::updateObjectPhysicsCandidate()'),None)
    if source_map:
        meta=json.loads((a.root/'metadata.json').read_text(encoding='utf8'))
        check(source_map['original_object_sha256']==meta['pc_sample_object_proof']['physics_object_sha256'] and source_map['unchanged_instruction_spans_only'],'wrong source-map physics object')
        check(any(r[0]==base and r[1]==source_map['size'] and r[2]=='Vehicle_playground::TerrainGame::updateObjectPhysicsCandidate()' for r in table),'source-map function extent differs')
    phases=[]
    for phase in [12,13]:
        trace=decode(a.root/f'pc-sample-{phase}.bin');check(trace['phase']==phase and trace['enabled']==startup['enabled'],'wrong trace phase/mode')
        if trace['enabled']:check(trace['count']>1000,f'phase{phase} insufficient PC samples')
        else:check(trace['count']==0,f'phase{phase} disabled trace not empty')
        counts=collections.Counter(name(r[0]) for r in trace['records']);frames=collections.Counter(r[1] for r in trace['records'])
        loci=collections.Counter()
        if source_map and base is not None:
            for r in trace['records']:
                if not base<=r[0]<base+source_map['size']:continue
                loc=source_map['lines'].get(str(r[0]-base));loci[(loc['file'],loc['line']) if loc else ('<unmapped candidate instruction>',0)]+=1
        phases.append(dict(phase=phase,count=trace['count'],covered_frames=len(frames),sha256=trace['sha256'],functions=[dict(name=n,samples=c,fraction=c/max(1,trace['count'])) for n,c in counts.most_common(30)],
            candidate_source_locations=[dict(file=f,line=l,samples=c) for (f,l),c in loci.most_common(40)]))
    comparison=None
    if a.reference:
        def rows(path):
            with path.open() as f:return list(csv.DictReader(f))
        fields=['frame','state','contacts','generated_contacts','awake','body_candidates','body_bounds_rejects','car_z','car_speed','velocity_energy','max_depth']
        mismatches=[];compared=0;fingerprints=0;raw=0
        for phase in [12,13]:
            x=rows(a.root/f'collision-phase-{phase}.csv');y=rows(a.reference/f'collision-phase-{phase}.csv');check(len(x)==len(y)==600,'wrong trajectory frame count')
            for u,v in zip(x,y):
                changed=[k for k in fields if u[k]!=v[k]]
                if changed:mismatches.append(dict(phase=phase,frame=u['frame'],fields=changed))
                compared+=1
            x=rows(a.root/f'world-overlap-{phase}.csv');y=rows(a.reference/f'world-overlap-{phase}.csv')
            if x and y:
                check(len(x)==len(y)==750,'wrong ordered fingerprint count')
                for u,v in zip(x,y):
                    check(all(u[k]==v[k] for k in ['frame','contacts','hash_a','hash_b']),'ordered contact fingerprint differs');fingerprints+=1
            for frame in [0,1]:
                x=a.root/f'step-pool-{phase}-{frame}.bin';y=a.reference/x.name
                if x.is_file() and y.is_file():check(x.read_bytes()==y.read_bytes(),'raw initialized state/contact snapshot differs');raw+=1
        check(not mismatches,'reference trajectory differs')
        comparison=dict(measured_frame_pairs=compared,mismatched_frames=len(mismatches),ordered_fingerprint_pairs=fingerprints,raw_snapshot_pairs=raw,first_mismatches=mismatches[:4])
    result=dict(capture_qualified=not errors,performance_qualified=False,errors=errors,enabled=startup['enabled'],calibration_samples=startup['count'],
        symbols_sha256=hashlib.sha256(a.symbols.read_bytes()).hexdigest(),phases=phases,
        trajectory_comparison=comparison,
        scope='External SDK timer PC samples while the original physics wrapper is active. No function-body probes. Known-loop calibration and bounded traces are required. Instruction occupancy is not VU utilization or production FPS. Shadow runs include oracle work and cannot price the unshadowed pipeline; trajectory/object identity gates are separate.')
    a.output.write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(dict(capture_qualified=result['capture_qualified'],errors=errors,enabled=startup['enabled'])))
    if errors:raise SystemExit(1)

if __name__=='__main__':main()
