"""Validate bounded census rows and correlate actual prior-job presentation tails."""
import argparse
import csv
import hashlib
import json
import statistics
from pathlib import Path

TICKS=294912
BUCKETS='game scene reflection terrainUpdate terrainBuild terrainMain terrainReflection roadsMain roadsReflection objects wheels particles coreMain coreReflection nativeFlush vifWait gsWait pending vsync flip'.split()


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def delta(a,b):return (a-b)&0xffffffff
def mean(x):return statistics.mean(x) if x else 0
def p95(x):return sorted(x)[int(.95*(len(x)-1))] if x else 0


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('fixture',type=Path);p.add_argument('--platform',choices=('emulator','ps2'),required=True)
    p.add_argument('--attempt',type=Path);a=p.parse_args();root=a.fixture
    attempt=root if a.platform=='emulator' else a.attempt
    assert attempt
    path=attempt/('world-cell-price-'+a.platform+'.json');qualified=json.loads(path.read_text())
    assert qualified['valid'] and qualified['variant']=='critical-path' and qualified['criticalPathEndpointsExact']
    log=root/'emulator/emulator.log' if a.platform=='emulator' else attempt/'console.log'
    assert sha(log)==qualified['logSha256']
    binding=json.loads((root/'native-binding.json').read_text())
    assert sha(root/'game/bin/vehicle-playground.elf')==qualified['elfSha256']==binding['elfSha256']
    for n,h in binding['files'].items():assert sha(root/n)==h,n
    bins=root/'game/bin' if a.platform=='emulator' else attempt/'bin'
    pins={str(path):sha(path),str(log):sha(log)}
    def read(name):
        f=bins/name;pins[str(f)]=sha(f)
        with f.open(newline='') as stream:return [{k:int(v) for k,v in r.items()} for r in csv.DictReader(stream)]
    with (bins/'drive-probe.csv').open(newline='') as f:drive=list(csv.DictReader(f))
    pins[str(bins/'drive-probe.csv')]=sha(bins/'drive-probe.csv')
    results=[]
    for phase in range(6):
        rows=read(f'critical-path-{phase}.csv')
        gas=[r for r in drive if r['phase']==str(phase) and r['gas']=='1']
        assert len(gas)==537 and len(rows)==(537 if phase%2 else 0)
        if rows:
            assert [r['offset'] for r in rows]==[int(r['offset']) for r in gas]
            for i,r in enumerate(rows):
                assert r['phase']==phase
                elapsed=delta(r['mark8'],r['mark0'])
                assert 0<elapsed<=int(gas[i]['whole'])
                assert all(r[b]>=0 and r[b+'Calls']>=0 for b in BUCKETS)
                assert r['game']<=elapsed and r['scene']<=r['game']
                assert r['nativeStarts']==1,'One-launch timing model requires the actual single-prefix route'
                if i:
                    assert r['previousLaunch']==rows[i-1]['mark7']
                    assert r['launchSequence']==r['presentSequence']
                marks=[r['mark'+str(k)] for k in (2,3,4,5,6)]
                assert all(delta(marks[j+1],marks[j])<=elapsed for j in range(4))
        result=dict(phase=phase,observed=bool(phase%2),rows=len(rows))
        if rows:
            result['inclusiveScopes']={b:dict(meanMs=mean([r[b]/TICKS for r in rows]),p95Ms=p95([r[b]/TICKS for r in rows]),calls=sum(r[b+'Calls'] for r in rows)) for b in BUCKETS}
            result['heavyFrames']=[dict(offset=r['offset'],workMs=(int(g['whole'])-int(g['pacing']))/TICKS,
                updateMs=int(g['update'])/TICKS,capture=int(g['probeCapture']),
                scopesMs={b:r[b]/TICKS for b in BUCKETS},pendingVifMs=delta(r['mark3'],r['mark2'])/TICKS,
                pendingGsMs=delta(r['mark4'],r['mark3'])/TICKS,
                prepareBeforePendingMs=delta(r['mark2'],r['mark0'])/TICKS)
                for r,g in zip(rows,gas) if int(g['whole'])-int(g['pacing'])>16.683*TICKS]
        if a.platform=='ps2':
            presents=read(f'triple-present-{phase}.csv')
            assert len(presents)==538 and all(r['slot']==1 and r['isr']==0 for r in presents)
            intervals=[delta(presents[i]['tick'],presents[i-1]['tick'])/TICKS for i in range(1,len(presents))]
            tails=[]
            for index,interval in enumerate(intervals,start=1):
                if interval<=25:continue
                item=dict(presentIndex=index,presentMs=interval)
                # Record i presents pending N-1 during gas loop i; record zero
                # is the first gas loop, with one final drain after the cohort.
                if index<len(gas):
                    g=gas[index];item.update(offset=int(g['offset']),previousOffset=int(gas[index-1]['offset']),
                      precedingWorkMs=(int(gas[index-1]['whole'])-int(gas[index-1]['pacing']))/TICKS,
                      currentWorkMs=(int(g['whole'])-int(g['pacing']))/TICKS,
                      previousCapture=int(gas[index-1]['probeCapture']))
                    if rows:
                        r=rows[index];prev=presents[index-1]['tick']
                        item.update(pendingEnterAfterPreviousPresentMs=delta(r['mark2'],prev)/TICKS,
                          pendingReadyAfterPreviousPresentMs=delta(r['mark4'],prev)/TICKS,
                          vifWaitMs=delta(r['mark3'],r['mark2'])/TICKS,
                          gsWaitMs=delta(r['mark4'],r['mark3'])/TICKS,
                          vsyncMs=r['vsync']/TICKS,flipMs=r['flip']/TICKS,
                          preparationBeforePendingMs=delta(r['mark2'],r['mark0'])/TICKS,
                          nativeAgeAtReadyMs=delta(r['mark4'],r['previousLaunch'])/TICKS if r['previousLaunch'] else None,
                          chcr=r['chcr'],vifStat=r['vifStat'],gifStat=r['gifStat'],csr=r['csr'],
                          currentScopesMs={b:r[b]/TICKS for b in BUCKETS},
                          previousScopesMs={b:rows[index-1][b]/TICKS for b in BUCKETS})
                tails.append(item)
            result.update(actualPresentHz=1000*len(intervals)/sum(intervals),lateIntervals=len(tails),tails=tails)
        results.append(result)
    report=dict(valid=True,platform=a.platform,elfSha256=qualified['elfSha256'],inputHashes=pins,
      readerSha256=sha(Path(__file__)),phases=results,exclusiveEeAttribution=False,vuUtilizationMeasured=False,
      limits=['Inclusive nested regions are not additive. Register snapshots are boundary states, not utilization.',
              'Native launch age includes concurrent EE preparation, not isolated GPU service.',
              'Observer Off/On comparison is a probe price, not a speedup; common layout footprint remains.',
              'Current common terrain producer in inherited fixed daytime drive/renderer harness, not full-production/night coverage.'])
    if a.platform=='ps2':
        report.update(observerForwardOverheadMs=-qualified['forwardSavedMs'],observerReverseOverheadMs=-qualified['reverseSavedMs'])
    out=attempt/('critical-path-'+a.platform+'.json');out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(valid=True,platform=a.platform,phases=[dict(phase=r['phase'],rows=r['rows'],late=r.get('lateIntervals')) for r in results])))


if __name__=='__main__':main()
