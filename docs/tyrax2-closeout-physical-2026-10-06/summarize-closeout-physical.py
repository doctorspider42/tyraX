from pathlib import Path
import json,re,statistics,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');runs=[]
for run in sorted(lab.glob('tyrax2-closeout-ps2-*')):
 if not(run/'physical-review.json').is_file():continue
 review=json.loads((run/'physical-review.json').read_text());d=review['protocol']
 raw=(run/'bin/closeout-samples.txt').read_text(encoding='utf8');samples=[]
 for line in raw.splitlines():
  if line.startswith('LOG: CLOSEOUTRAW '):samples.append({k:int(v)for k,v in (part.split('=')for part in line.split()[2:])})
 phases=[]
 for p,r in d['byPhase'].items():
  values=[s['entryPeriod']/294912 for s in samples if s['phase']==int(p)]
  # 25 ms separates observed single-field cadence from double-field events;
  # the rounded16.68 threshold in the generic parser is NOT a dropped-field rate.
  phases.append(dict(phase=int(p),players=r['players'],geometryHidden=r['geometryHidden'],sampler=r['sampler'],chunkMeanNonPacingMs=r['chunkMeanNonPacingMs'],chunkMeanEntryMs=r['chunkMeanEntryMs'],maxObservedChunkMeanEntryMs=r['maxObservedChunkMeanEntryMs'],renderedCompletions=r['renderedCompletions'],syntheticCompletions=r['syntheticCompletions'],rawLoops=len(values),rawEntryPeriodsAbove25Ms=sum(v>25 for v in values),maxRawEntryPeriodMs=max(values)if values else None))
 lines=(run/'stdout.utf8.log').read_text(encoding='utf8').splitlines();envelopes=[];start=None
 unhealthy=re.compile(r'VEHCONTACT|VEH pos|FRAMEARENA frames|NATIVEVIF frames|ORDEREDFRAME frames|FRAMEPIPELINE starts|livepad\.bin|livedbg\.cmd|log\.txt')
 for i,line in enumerate(lines):
  if 'LOG: CLOSEOUTCONTEXT 'in line and 'offset=750 'in line:start=i
  if 'LOG: CLOSEOUTCONTEXT 'in line and 'offset=1155 'in line:
   assert start is not None
   bad=[l for l in lines[start+1:i]if unhealthy.search(l)];assert not bad,(run,bad)
   envelopes.append(dict(firstLine=start+1,lastLine=i+1,retainedWarnings=[l for l in lines[start+1:i]if 'WARN:'in l]));start=None
 assert len(envelopes)==3
 runs.append(dict(name=run.name,kind=d.get('kind',3),order=d.get('order',0),route=d.get('route'),phases=phases,middleMinusOuterMeanNonPacingMs=d.get('middleMinusOuterMeanNonPacingMs'),outerControlSpreadMs=d.get('outerControlSpreadMs'),portalColdCounts=d.get('portalColdCounts'),physicalReviewSha256=hashlib.sha256((run/'physical-review.json').read_bytes()).hexdigest(),quietColdEnvelopes=envelopes))
report=dict(status='COMPLETED_PHYSICAL_RUNS_SCOPE_ONLY',runs=runs,ordinarySamplerBoundaries=True,addedWaits=False,publicCommonCodeCostUnpriced=True,quietScaffoldingCostUnpriced=True,tvScanoutLatencyUnmeasured=True,universalNightSixtyFpsAccepted=False,limitations=['Only priced320-loop windows and128 raw loops per enabled phase; not exhaustive5400-loop tails.','25ms is an explicit event separator, not16.68ms rounded dropped-field threshold.','Private healthy telemetry suppression is common and unpriced; cannot assign older30fps-to-current gain to receiver policy.','Geometry removes authored content, not equivalent-output optimization.'])
(lab/'closeout-physical-summary.json').write_text(json.dumps(report,indent=2)+'\n');print('COMPLETED_RUNS',len(runs))
