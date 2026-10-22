"""Compare actual complete-cell and ordinary-only four-view image archives."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('candidate',type=Path);p.add_argument('ordinary',type=Path)
p.add_argument('--out',type=Path)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
bindings=[json.loads((root/'native-binding.json').read_text()) for root in (a.candidate,a.ordinary)]
manifests=[json.loads((root/'world-cell-view-manifest.json').read_text())
           for root in (a.candidate,a.ordinary)]
assert not manifests[0].get('ordinaryOnly',False) and manifests[1]['ordinaryOnly']
settling=manifests[0].get('ordinarySettlingFrames',0)
assert settling==manifests[1].get('ordinarySettlingFrames',0), 'Unmatched settling schedule'
assert bindings[0]['engineArchiveSha256']==bindings[1]['engineArchiveSha256']
assert bindings[0]['elfSha256']!=bindings[1]['elfSha256']
for root,binding in zip((a.candidate,a.ordinary),bindings):
    for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
    assert sha(root/'game/bin/vehicle-playground.elf')==binding['elfSha256']
images=0;phases=[];comparisons=[]
for phase in range(3):
    reports=[]
    for root,binding in zip((a.candidate,a.ordinary),bindings):
        report=json.loads((root/f'world-cell-view-emulator-phase{phase}.json').read_text())
        assert report['elfSha256']==binding['elfSha256']
        assert report['logSha256']==sha(root/'emulator/emulator.log')
        for row in report['images']:
            for rel,digest in zip(row['files'],row['hashes']):assert sha(root/rel)==digest
        reports.append(report)
    candidate,ordinary=reports
    assert not candidate.get('ordinaryOnly',False) and ordinary['ordinaryOnly']
    assert all(r.get('ordinarySettlingFrames',0)==settling for r in reports)
    assert candidate['activation']['submittedByView']==[3,4,13,13]
    assert ordinary['activation']['submittedByView']==[0,0,0,0]
    assert len(candidate['images'])==len(ordinary['images'])==12
    for c,o in zip(candidate['images'],ordinary['images']):
        assert (c['view'],c['kind'])==(o['view'],o['kind'])
        images+=sum(x==y for x,y in zip(c['hashes'],o['hashes']))
        for left,right in ((0,1),(0,2),(1,2)):
            deltas=[]
            for root,row in ((a.candidate,c),(a.ordinary,o)):
                before=(root/row['files'][left]).read_bytes()[16:]
                after=(root/row['files'][right]).read_bytes()[16:]
                assert len(before)==len(after)
                changes=b''.join(struct.pack('<IBB',i,x,y) for i,(x,y) in
                                 enumerate(zip(before,after)) if x!=y)
                deltas.append(hashlib.sha256(changes).hexdigest())
            comparisons.append(dict(phase=phase,view=c['view'],kind=c['kind'],
                                    arms=[left,right],
                                    exactTransitionEqual=deltas[0]==deltas[1]))
    assert not candidate['valid']
    phases.append(phase)
report={'valid':True,'identicalCapturedImages':images,'totalCapturedImages':108,
        'rasterSequenceEquivalent':images==108,
        'transitionComparisons':comparisons,'phases':phases,
        'elfSha256':[b['elfSha256'] for b in bindings],
        'ordinarySettlingFrames':settling,
        'differenceReproducedWithoutCandidate':all(
            row['exactTransitionEqual'] for row in comparisons),'promotionAllowed':False,
        'limits':['Cross-build captures need not all match; transition equality is reported separately.',
                  'Ordinary-only drift does not establish universal candidate safety or its source cause.',
                  'No physical performance or moving lifetime conclusion.']}
output=a.out or a.ordinary/'world-cell-view-control-comparison.json'
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
