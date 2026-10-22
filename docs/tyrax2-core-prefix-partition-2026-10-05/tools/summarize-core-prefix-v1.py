from pathlib import Path
import json,hashlib,importlib.util,re
b=Path('F:/Projects/tyrax2-lab-20261001');out=b/'core-prefix-results-v1.json';assert not out.exists();rows=[]
sp=importlib.util.spec_from_file_location('a',b/'core-prefix-controls-v1/analyze-night.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
for order in(0,1):
 root=b/('night-ablation-ps2-core-prefix-order0-20261005-retry1-evidence' if order==0 else 'night-ablation-ps2-core-prefix-order1-20261005-evidence');p=root/'strict-analysis.json';r=json.loads(p.read_text(encoding='utf8'));assert r['status']=='PASS_STRICT_NIGHT_MASKED_LOOP_CAPTURE';flags=r['producerObserverFlags'];assert flags==([0,1,0]if order==0 else[1,0,1]);means=[r['by_phase'][str(p)]['mean_non_pacing_ms']for p in range(3)];on=[p for p in range(3)if flags[p]];off=[p for p in range(3)if not flags[p]]
 measured={}
 for st in range(5):
  ts=[r['producerColdWindows'][f'{p}:1155:{st}']for p in on];measured[str(st)]=dict(perLoopElapsed_ms=sum(int(t['ticks'])for t in ts)/len(ts)/320/294912,perLoopCalls=sum(int(t['timedCalls'])for t in ts)/len(ts)/320,phaseElapsed_ms=[int(t['ticks'])/320/294912 for t in ts],phaseCalls=[int(t['timedCalls'])for t in ts],phaseReads=[int(t['reads'])for t in ts])
 rows.append(dict(order=order,flags=flags,phaseMeans_ms=means,observerOnMinusOff_ms=sum(means[p]for p in on)/len(on)-sum(means[p]for p in off)/len(off),adjacentOnMinusOff_ms=r['middleArmMinusOwnOuterControlMean_ms'],outerSpread_ms=r['outer_control_spread_ms'],scopes=measured,sumScopedElapsed_ms=sum(x['perLoopElapsed_ms']for x in measured.values()),coldWindows=r['producerColdWindows'],renderedPeriods_ms=[r['by_phase'][str(p)]['sample_stats']['renderedPeriod']['mean_ms']for p in range(3)],strictReportSha256=hashlib.sha256(p.read_bytes()).hexdigest()))
 text=(root/'stdout.log').read_text(encoding='utf8');artifact=(root/'night-ablation.log').read_text(encoding='utf8')
 bads=[text.replace('stage=0','stage=7',1),re.sub(r'(reads=)([1-9][0-9]*)',lambda q:q[1]+str(int(q[2])+1),text,count=1),re.sub(r'(NIGHTPROD[^\n]*enabled=0[^\n]*timedCalls=)0',r'\g<1>1',text,count=1),text.replace('offset=750 stage=0','offset=751 stage=0',1),text.replace('NIGHTPROD phase=0 offset=750 stage=0','REMOVED phase=0 offset=750 stage=0',1)]
 for bad in bads:
  assert bad!=text
  try:m.analyze(bad,artifact,'ps2',14,order,0,0)
  except ValueError:pass
  else:raise AssertionError('accepted corrupted physical producer record')
out.write_bytes((json.dumps(dict(status='PASS_COMPLETED_TWO_ORDER_CORE_PREFIX_PARTITION',rows=rows,physicalNegativeControlsRejected=10,scopeNames=['headBoundsPackager','textureProgramLightFacts','objectDataRoute','reserved3','reserved4'],scopeTimesIncludeMeasurementAndPreemption=True,commonObserverCodeFootprintPriced=False,uniformTaxSubtractionAccepted=False,optimizedRendererPromoted=False),indent=2)+'\n').encode());print(out)
