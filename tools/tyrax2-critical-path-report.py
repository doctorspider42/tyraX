"""Seal completed critical-path evidence and classify repeated deadline tails."""
import argparse
import collections
import csv
import hashlib
import json
import statistics
from pathlib import Path


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def delta(a,b):return (a-b)&0xffffffff


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('out','fixture','attempt'):p.add_argument(n,type=Path)
    a=p.parse_args();r=a.fixture
    b=json.loads((r/'native-binding.json').read_text())
    for n,h in b['files'].items():assert sha(r/n)==h,n
    assert sha(r/'game/bin/vehicle-playground.elf')==b['elfSha256']
    manifest=json.loads((r/'world-cell-price-manifest.json').read_text())
    assert manifest['criticalPathObserver'] and manifest['offlineTerrainDisabled']
    record=dict(valid=True,productionPromoted=False,optimizationImplemented=False,date='2026-10-20',
      fixture=str(r),attempt=str(a.attempt),elfSha256=b['elfSha256'],nativeSourceFiles=len(b['files']),
      nativeBindingSha256=sha(r/'native-binding.json'),manifestSha256=sha(r/'world-cell-price-manifest.json'),platforms={})
    for platform,base in [('emulator',r),('ps2',a.attempt)]:
        result=base/('world-cell-price-'+platform+'.json');q=json.loads(result.read_text())
        diag=base/('critical-path-'+platform+'.json');d=json.loads(diag.read_text())
        assert q['valid'] and d['valid'] and q['elfSha256']==d['elfSha256']==b['elfSha256']
        assert q['poseEqual'] and q['deliveryValid'] and q['criticalPathCaptureWorkEqual'] and q['criticalPathEndpointsExact']
        assert all(not any(image['differences']) for image in q['images'])
        log=r/'emulator/emulator.log' if platform=='emulator' else base/'console.log'
        assert sha(log)==q['logSha256']
        for n,h in d['inputHashes'].items():assert sha(Path(n))==h,n
        record['platforms'][platform]=dict(qualification=q,diagnostics=d,qualificationSha256=sha(result),diagnosticsSha256=sha(diag))
    deploy=json.loads((a.attempt/'editor-deploy.json').read_text());assert deploy['elfSha256']==b['elfSha256']
    for n,h in json.loads((a.attempt/'launch-inputs.json').read_text()).items():assert sha(a.attempt/'bin'/n)==h,n
    residency=json.loads((r/'vu-residency.json').read_text())
    assert residency['valid'] and residency['elfSha256']==b['elfSha256']
    assert residency['nativeBindingSha256']==sha(r/'native-binding.json')
    assert residency['symbolElfSha256']==sha(r/'game/bin/vehicle-playground.elf.sym')
    record['vuResidency']=residency
    record['vuResidencySha256']=sha(r/'vu-residency.json')
    d=record['platforms']['ps2']['diagnostics'];groups=collections.defaultdict(list);classes=collections.Counter()
    observedSets=[];offSets=[]
    for phase in d['phases']:
        offsets={t['offset'] for t in phase['tails'] if 'offset' in t}
        if not phase['observed']:offSets.append(offsets);continue
        observedSets.append(offsets)
        with (a.attempt/'bin'/f"triple-present-{phase['phase']}.csv").open(newline='') as f:
            ticks=[int(row['tick']) for row in csv.DictReader(f)]
        intervals=[delta(ticks[i],ticks[i-1])/294912 for i in range(1,len(ticks))]
        deadline=statistics.median(v for v in intervals if v<25)
        assert 16<deadline<17
        for t in phase['tails']:
            assert 'offset' in t
            enter=t['pendingEnterAfterPreviousPresentMs'];ready=t['pendingReadyAfterPreviousPresentMs']
            wait=t['vifWaitMs']
            if enter>deadline and wait<.01 and t['gsWaitMs']<.01:kind='preparation'
            elif enter<deadline and wait>.25 and ready>deadline:kind='consumer-completion'
            else:kind='unresolved'
            classes[kind]+=1
            groups[t['offset']].append(dict(phase=phase['phase'],kind=kind,deadlineMs=deadline,
              enterMs=enter,readyMs=ready,vifWaitMs=wait,gsWaitMs=t['gsWaitMs'],
              overDeadlineMs=ready-deadline,vifStat=t['vifStat'],
              vew=(t['vifStat']>>2)&1,vgw=(t['vifStat']>>3)&1))
    assert observedSets and all(s==observedSets[0] for s in observedSets)
    assert all(observedSets[0]<=s for s in offSets),'Observed late sites absent from a control'
    record['repeatedTailGroups']={str(k):v for k,v in sorted(groups.items())}
    record['classificationCountsAcrossObservedTrips']=dict(classes)
    record['repeatedPreparationOffsets']=[k for k,v in sorted(groups.items()) if all(t['kind']=='preparation' for t in v)]
    record['repeatedConsumerOffsets']=[k for k,v in sorted(groups.items()) if all(t['kind']=='consumer-completion' for t in v)]
    record['extraUnobservedControlOffsets']=sorted(set.union(*offSets)-observedSets[0])
    record['limits']=['A boundary consumer wait includes VU1 execution and possible GIF/GS backpressure; no ALU-only attribution.',
      'Inclusive Core/scene regions are not additive, not exclusive EE instruction costs.',
      'Observer contrasts are about -0.006/+0.002ms, not a universal fee or an optimization.',
      'Current common terrain builder in inherited private daytime drive/renderer; no full-production/night/general ceiling claim.',
      'Vertex-once strip transformation is a source-backed VU1 candidate only; no new shader, native numerical proof or speed gain yet.',
      'VIF bit locations follow PS2SDK VIFSTAT; snapshots are not utilization or proof of the entire wait reason.']
    assert not a.out.exists(),'Do not overwrite sealed evidence'
    a.out.write_text(json.dumps(record,indent=2)+'\n');print(a.out)


if __name__=='__main__':main()
