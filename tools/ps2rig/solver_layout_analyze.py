"""Reject mismatched solver-layout screens while retaining fixed-input costs."""
from pathlib import Path
import argparse,csv,hashlib,json,math,re,statistics,struct

def analyze(root,log,meta):
    text=log.read_text(encoding='utf8',errors='replace')
    assert meta.get('solver_layout') and meta.get('scene_timing_qualified') is False
    assert 'CB DONE phases=14' in text and 'CB ERROR' not in text
    assert 'VUSATCHECK cases=2048 failures=0 ' in text and 'VUSATPRECISION cases=50000 failures=0' in text
    assert re.search(r'PHYSICSPMUCAL .*passed=1',text)
    cases=re.findall(r'SOLVERLAYOUTCASE phase=(\d+) frame=(\d+) bodies=(\d+) contacts=(\d+) full_stride=(\d+) compact_stride=(\d+) contact_stride=(\d+)',text)
    costs=re.findall(r'SOLVERLAYOUT phase=(\d+) frame=(\d+) repeat=(\d+) mode=(\d+) ticks=(\d+) im=(\d+) dm=(\d+)',text)
    checks=re.findall(r'SOLVERLAYOUTCHECK phase=(\d+) frame=(\d+) reference_diff=(\d+) compact_diff=(\d+) errors=(\d+) cases=(\d+)',text)
    deltas=re.findall(r'SOLVERLAYOUTDELTA phase=(\d+) frame=(\d+) maximum=(\S+) zero_signs=(\d+) layout_diff=(\d+) layout_maximum=(\S+)',text)
    scratch=bool(meta.get('solver_scratchpad'));modes_count=3 if scratch else 2
    spr=re.findall(r'SOLVERSPR phase=(\d+) frame=(\d+) repeat=(\d+) used=(\d+) restore_errors=(\d+)',text)
    spr_checks=re.findall(r'SOLVERSPRCHECK phase=(\d+) frame=(\d+) heap_scratch_diff=(\d+) restore_errors=(\d+)',text)
    expected=[(12,300),(12,600),(13,300),(13,600)]
    assert [(int(r[0]),int(r[1])) for r in cases]==expected
    assert [(int(r[0]),int(r[1])) for r in checks]==expected and len(costs)==16*modes_count
    assert not deltas or [(int(r[0]),int(r[1])) for r in deltas]==expected
    if scratch:
        assert len(spr)==16 and [(int(r[0]),int(r[1])) for r in spr_checks]==expected
        assert all(int(r[3]) in [0,1] and int(r[4])==0 for r in spr)
    hashes={};report=[]
    for phase in [12,13]:
        source=root/f'collision-phase-{phase:02}.csv';rows=list(csv.DictReader(source.open()))
        assert [int(r['frame']) for r in rows]==list(range(150,750))
        assert all(r['generated_contacts']==r['contacts'] and r['push_candidates']==r['push_tested'] and
            int(r['dropped_contacts'])==int(r['push_overflow'])==0 and
            all(math.isfinite(float(r[k])) for k in ['car_z','car_speed','velocity_energy','max_depth']) for r in rows)
        hashes[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
        source=root/f'vu0-phase-{phase:02}.csv';micro=list(csv.DictReader(source.open()));assert len(micro)==1
        assert all(int(micro[0][k])==0 for k in ['jobs','batches','validation_errors','missed_reference_hits'])
        hashes[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
    for raw in cases:
        phase,frame,nb,nc,full,compact,contact=map(int,raw)
        source=root/f'solver-layout-{phase}-{frame}.bin';data=source.read_bytes()
        assert struct.unpack_from('<6I',data)==(0x534c5954,1,nb,nc,full,contact)
        assert full==880 and compact==96 and contact==100 and len(data)==24+nb*compact+nc*contact
        for i in range(nc):
            a,b=struct.unpack_from('<hh',data,24+nb*compact+i*contact+8)
            assert 0<=a<nb and -1<=b<nb and a!=b
        hashes[source.name]=hashlib.sha256(data).hexdigest()
        selected=[tuple(map(int,r)) for r in costs if (int(r[0]),int(r[1]))==(phase,frame)]
        assert sorted((r[2],r[3]) for r in selected)==[(repeat,mode) for repeat in range(4) for mode in range(modes_count)]
        modes=[]
        for mode in range(modes_count):
            rows=[r for r in selected if r[3]==mode];assert all(r[4]>0 and 0<=r[5]<0x80000000 and 0<=r[6]<0x80000000 for r in rows)
            modes.append(dict(mode=mode,mean_ticks=statistics.mean(r[4] for r in rows),mean_im=statistics.mean(r[5] for r in rows),mean_dm=statistics.mean(r[6] for r in rows)))
        report.append(dict(phase=phase,frame=frame,bodies=nb,contacts=nc,modes=modes,compact_time_ratio=modes[1]['mean_ticks']/modes[0]['mean_ticks']))
    layout_parity=bool(deltas) and all(int(r[4])==0 and float(r[5])==0 for r in deltas)
    scratch_parity=bool(spr_checks) and all(int(r[2])==int(r[3])==0 for r in spr_checks) and all(int(r[3])==1 for r in spr)
    qualified=all(int(r[2])==int(r[3])==int(r[4])==0 for r in checks) and layout_parity and (not scratch or scratch_parity)
    return dict(metadata=meta,qualified=qualified,performance_qualified=False,cases=report,
        integration_checks=[dict(zip(['phase','frame','reference_diff','compact_diff','errors','cases'],map(int,r))) for r in checks],
        deltas=[dict(phase=int(p),frame=int(f),maximum=float(m),zero_signs=int(z),layout_diff=int(d),layout_maximum=float(l)) for p,f,m,z,d,l in deltas],
        sampled_layout_parity=layout_parity,sampled_scratchpad_parity=scratch_parity if scratch else None,
        scratchpad_checks=[dict(zip(['phase','frame','heap_scratch_diff','restore_errors'],map(int,r))) for r in spr_checks],
        raw_sha256=hashes,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),
        scope='Four prepared physical snapshots, same extracted scalar6+3 solver source with full880-byte or compact96-byte bodies. Compact cost includes gather/scatter; caller-inline reference is checked separately. Replay/setup disturbs live trajectories: no whole-frame FPS qualification. Nonzero integration differences reject promotion even if the two layout instantiations agree.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--log',required=True,type=Path)
    p.add_argument('--metadata',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    out=analyze(a.root,a.log,json.loads(a.metadata.read_text(encoding='utf8')));a.output.write_text(json.dumps(out,indent=2),encoding='utf8')
    print('4 solver snapshots retained; integration qualified='+str(out['qualified']))
    if not out['qualified']:raise SystemExit(1)
