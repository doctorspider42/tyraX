from pathlib import Path
import json,hashlib,datetime,re
root=Path(__file__).parent
run=root.parent/'tyrax2-closeout-ps2-render-audit-v1'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
n=json.loads((root/'vehicle/native-proof.json').read_text())
launch=json.loads((run/'launch-inputs.json').read_text(encoding='utf-8-sig'))
result=json.loads((run/'strict-result.json').read_text())
assert result['status']=='PASS_COMPLETED_TWO_AXIS_RENDER_AUDIT'
b=result['balance'];assert b['status']=='PASS_COMPLETED_LOOP_BALANCE_PROTOCOL'
elf=run/'bin/vehicle-playground.elf';assert sha(elf)==n['actualELFSha256']
for x in launch['files']:assert sha(run/'bin'/x['name'])==x['sha256'],x['name']
for name,h in json.loads((root/'vehicle/source-review.json').read_text())['files'].items():
 p=root/'vehicle/game'/name[5:]if name.startswith('game/')else root/name
 assert sha(p)==h,name
text=(run/'completed-prefix/ps2client.stdout').read_bytes().decode('latin1')
assert 'loadelf' in text.lower()
assert not re.search(r'(?im)^.*(?:ASSERT|TRAP|FRAMEPIPELINE final FINISH recording failed|freepad: DMA Busy).*$',text)
paced={};rows=0
for line in (run/'completed-prefix/closeout-samples.txt').read_text().splitlines():
 if not line.startswith('LOG: CLOSEOUTBAL '):continue
 r={k:int(v)for k,v in (x.split('=')for x in line.split()[2:])}
 if r['enabled']:
  assert r['firstOwner']==r['lastOwner']==r['frame']-1
  key=','.join(str(r['pacedCalls'+str(k)])for k in range(3));paced[key]=paced.get(key,0)+1;rows+=1
assert rows==256
proof=dict(status='ROOT_ACCEPTED_BOUNDED_PHYSICAL_TWO_AXIS_RENDER_AUDIT',UTC=datetime.datetime.now(datetime.timezone.utc).isoformat(),sourceHead=launch['sourceHead'],nativeELFSha256=sha(elf),physicalResultSha256=sha(run/'strict-result.json'),rawLoops=384,enabledRows=rows,renderCountReads=result['additionalRenderCountReads'],balanceCountReads=b['addedCountReadsInRawRows'],pacedWaitCallHistogram=paced,sameELFObserverContrastsMs=[b['byPhase'][str(p)]['observerOnMinusOffMs']for p in (1,2)],outerSpreadMs=[b['byPhase'][str(p)]['outerSpreadMs']for p in (1,2)],uniformTaxSubtractionAccepted=False,commonFootprintUnpriced=True,twoAlternativeExclusivePartitions=True,wholeLoopClosure=True,completionOwner='preceding EE loop',pureEEOrGPUPrice=False,physicalTV='not independently inspected; emulator raster checked',terminalClientExitClaim=False,productionChange=False,nativeSixtyFpsAccepted=False)
(run/'root-qualification.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
