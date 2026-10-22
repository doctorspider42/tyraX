"""Preliminary owner/residual/raster gate; never authorizes physical pricing."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

def contracts(log):
    specs={'WORLDOWNER':('sources','draws','triangles','hash'),
           'CONSUMERINPUT':('calls','verts','hash'),'HEADERORACLE':('checks',),
           'COARSESOURCE':('packages','triangles','vertices','hash'),
           'COARSEDELIVERY':('jobs','triangles','vertices','hash'),
           'COARSERESIDUAL':('triangles','vertices','hash'),'RINGCONTRACT':('calls','triangles')}
    result={}
    for name,fields in specs.items():
        pattern=r'\b'+name+r' phase=(\d+) arm=(\d+)'+''.join(' '+f+r'=([0-9a-f]+)' for f in fields)
        rows=[tuple(int(v,16 if name=='WORLDOWNER' and i==5 else 10) for i,v in enumerate(r)) for r in re.findall(pattern,log)]
        assert len(rows)==30,name
        assert [r[:2] for r in rows]==[(p,a) for p in range(6) for a in range(5)],name
        for phase in range(6):
            group=rows[phase*5:phase*5+5]
            if name=='WORLDOWNER':
                assert group[0][2]>0 and group[0][4]>0
                assert all((r[2],r[4],r[5])==(group[0][2],group[0][4],group[0][5]) for r in group)
                assert all(r[3]==(r[2] if r[1]==1 else 0) for r in group)
            else:assert all(r[2:]==group[0][2:] for r in group),(name,phase)
        result[name]=rows
    for s,d in zip(result['COARSESOURCE'],result['COARSEDELIVERY']):assert s[3:]==d[3:]
    return result

def controls():
    lines=[]
    for p in range(6):
        for a in range(5):
            prefix=f' phase={p} arm={a}'
            for name,values in [('WORLDOWNER',f'sources=7 draws={7 if a==1 else 0} triangles=1942 hash=48575764'),
                                ('CONSUMERINPUT','calls=2 verts=6 hash=12'),('HEADERORACLE','checks=1'),
                                ('COARSESOURCE','packages=1 triangles=2 vertices=6 hash=12'),
                                ('COARSEDELIVERY','jobs=1 triangles=2 vertices=6 hash=12'),
                                ('COARSERESIDUAL','triangles=0 vertices=0 hash=2166136261'),
                                ('RINGCONTRACT','calls=1 triangles=2')]:lines.append(name+prefix+' '+values)
    text='\n'.join(lines);contracts(text)
    mutations={}
    for name in ('WORLDOWNER','CONSUMERINPUT','HEADERORACLE','COARSEDELIVERY','COARSERESIDUAL','RINGCONTRACT'):
        mutations['missing-'+name]=text.replace(name+' phase=0 arm=1','MISSING'+name+' phase=0 arm=1')
        mutations['changed-'+name]=re.sub('('+name+r' phase=0 arm=1[^\n]*=)[0-9a-f]+',r'\g<1>0',text)
    mutations['inactive']=text.replace('sources=7 draws=7','sources=7 draws=0')
    mutations['control-draw']=text.replace('sources=7 draws=0','sources=7 draws=1')
    mutations['zero-source']=text.replace('triangles=1942','triangles=0')
    for name,bad in mutations.items():
        try:contracts(bad)
        except AssertionError:continue
        raise AssertionError('negative escaped: '+name)
    return list(mutations)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path,nargs='?');p.add_argument('--selftest',action='store_true');a=p.parse_args()
    negatives=controls()
    if a.selftest:print(json.dumps(dict(positive=True,negatives=negatives,performanceCandidate=False)));return
    assert a.fixture is not None;root=a.fixture
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    binding=json.loads((root/'native-binding.json').read_text());plan=json.loads((root/'clip-capacity-manifest.json').read_text())
    assert plan['experiment']=='A31-owner' and plan['worldHotCandidate'] and plan['hotBaselineCoarse']
    for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
    assert sha(root/'game/bin/vehicle-playground.elf')==binding['elfSha256']
    logpath=root/'emulator/emulator.log';log=logpath.read_text(errors='replace');assert 'DRIVEDONE valid=1' in log
    parsed=contracts(log);images=[]
    for phase in range(6):
        for view in ('main','env'):
            reference=None
            for arm in range(5):
                stage=phase*5+arm;path=root/f'game/bin/tape-{stage}-{view}.bin';raw=path.read_bytes()
                w,h,psm,actual=struct.unpack('<4I',raw[:16]);assert actual==stage and 0<w<=1024 and 0<h<=1024 and psm in (0,2)
                assert len(raw)==16+w*h*(2 if psm==2 else 4) and len(set(raw[16:]))>8
                image=(w,h,psm,raw[16:])
                if reference is None:reference=image
                assert image==reference,(phase,arm,view)
                images.append(dict(stage=stage,view=view,sha256=sha(path)))
    report=dict(preliminaryValid=True,performanceCandidate=False,scope='Owner source assertions, residual equality, exact60 rasters; full travel/lifecycle/SDK-body reader and physical pricing outstanding',elfSha256=binding['elfSha256'],logSha256=sha(logpath),contracts=parsed,images=images,negativeControls=negatives)
    (root/'world-runtime-preflight.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(preliminaryValid=True,images=len(images),negatives=len(negatives),performanceCandidate=False)))

if __name__=='__main__':main()
