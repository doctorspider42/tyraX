from pathlib import Path
import json,hashlib,importlib.util,re
lab=Path('F:/Projects/tyrax2-lab-20261001')
sp=importlib.util.spec_from_file_location('audit',lab/'light-pick-far-controls-v1/analyze-night.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
out=lab/'light-far-role-results-v1.json';assert not out.exists()
rows=[]
for order in (0,1):
 stem=f'night-ablation-ps2-light-far-order{order}-20261005';root=lab/(stem+'-evidence');report=root/'strict-analysis.json';r=json.loads(report.read_text(encoding='utf8'))
 assert r['status']=='PASS_STRICT_NIGHT_MASKED_LOOP_CAPTURE' and r['farPickFlags']==([0,1,0]if order==0 else[1,0,1])
 means=[r['by_phase'][str(p)]['mean_non_pacing_ms']for p in range(3)];candidate=sum(means[p]for p in range(3)if r['farPickFlags'][p])/sum(r['farPickFlags']);control=sum(means[p]for p in range(3)if not r['farPickFlags'][p])/(3-sum(r['farPickFlags']))
 rows.append(dict(order=order,flags=r['farPickFlags'],phaseMeans_ms=means,candidate_ms=candidate,control_ms=control,candidateMinusControl_ms=candidate-control,adjacentCandidateMinusControls_ms=r['middleArmMinusOwnOuterControlMean_ms'],outerArmSpread_ms=r['outer_control_spread_ms'],renderedPeriod_ms=[r['by_phase'][str(p)]['sample_stats']['renderedPeriod']['mean_ms']for p in range(3)],reportSha256=hashlib.sha256(report.read_bytes()).hexdigest()))
 # Target protocol controls, replayed against this actual completed pair.
 stdout=(root/'stdout.log').read_text(encoding='utf8') if(root/'stdout.log').exists()else(root/'normalized-stdout.log').read_text(encoding='utf8')
 artifact=(root/'night-ablation.log').read_text(encoding='utf8')
 tests={
  'phase wrong flag':re.sub(r'(NIGHTFARPICKPHASE phase=0 enabled=)[01]',lambda x:x[1]+str(1-r['farPickFlags'][0]),stdout,count=1),
  'nonzero mismatch':re.sub(r'(NIGHTFARPICKGATES[^\n]*mismatches=)0',r'\g<1>1',stdout,count=1),
  'missing far gate':re.sub(r'^.*NIGHTFARPICKGATES[^\n]*\n','',stdout,count=1,flags=re.M),
  'zero selected reject':re.sub(r'(NIGHTFARPICKGATES[^\n]*rejected=)61',r'\g<1>0',stdout,count=1),
  'wrong actual application':re.sub(r'(NIGHTFARPICKGATES[^\n]*enabled=1[^\n]*applied=)61',r'\g<1>0',stdout,count=1),
 }
 for label,bad in tests.items():
  assert bad!=stdout,label
  try:m.analyze(bad,artifact,'ps2',12,order,0,0)
  except ValueError:pass
  else:raise AssertionError('accepted '+label)
out.write_bytes((json.dumps(dict(status='PASS_ROLE_BASED_TWO_ORDER_FAR_PICK_SUMMARY',rows=rows,negativeControlsRejected=10,pureEEBill=False,commonCompiledOverheadPriced=False),indent=2)+'\n').encode())
print(out)
