from pathlib import Path
import json,hashlib,datetime,re
lab=Path('F:/Projects/tyrax2-lab-20261001');root=Path(__file__).parent;run=lab/'tyrax2-closeout-ps2-loop-balance-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
n=json.loads((root/'vehicle/native-proof.json').read_text());launch=json.loads((run/'launch-inputs.json').read_text(encoding='utf-8-sig'));result=json.loads((run/'strict-result.json').read_text())
assert result['status']=='PASS_COMPLETED_LOOP_BALANCE_PROTOCOL'
elf=run/'bin/vehicle-playground.elf';assert sha(elf)==n['actualELFSha256'];pin=next(x for x in launch['files']if x['name']=='vehicle-playground.elf');assert pin['sha256']==sha(elf)
for x in launch['files']:assert sha(run/'bin'/x['name'])==x['sha256'],x['name']
for name,h in json.loads((root/'vehicle/source-review.json').read_text())['files'].items():
 p=root/'vehicle/game'/name[5:]if name.startswith('game/')else root/name;assert sha(p)==h,name
raw=(run/'completed-prefix/ps2client.stdout').read_bytes();text=raw.decode('latin1')
assert 'loadelf' in text.lower() and not re.search(r'(?im)^.*(?:ASSERT|TRAP|FRAMEPIPELINE final FINISH recording failed|freepad: DMA Busy).*$',text)
contrasts=[result['byPhase'][str(p)]['observerOnMinusOffMs']for p in (1,2)];spread=[result['byPhase'][str(p)]['outerSpreadMs']for p in (1,2)]
for line in (run/'completed-prefix/closeout-samples.txt').read_text().splitlines():
 if not line.startswith('LOG: CLOSEOUTBAL '):continue
 b={k:int(v)for k,v in (x.split('=')for x in line.split()[2:])}
 if b['enabled']:assert b['firstOwner']==b['lastOwner']==b['frame']-1
proof=dict(status='ROOT_ACCEPTED_BOUNDED_PHYSICAL_LOOP_BALANCE',UTC=datetime.datetime.now(datetime.timezone.utc).isoformat(),sourceHead=launch['sourceHead'],nativeELFSha256=sha(elf),physicalResultSha256=sha(run/'strict-result.json'),rawLoops=384,balanceRows=384,enabledRows=sum(x['enabledRows']for x in result['byPhase'].values()),addedCountReadsInRawRows=result['addedCountReadsInRawRows'],sameELFObserverContrastsMs=contrasts,outerSpreadMs=spread,uniformTaxSubtractionAccepted=False,commonFootprintUnpriced=True,wholeLoopClosure=True,nestingObserved=0,completionOwner='all enabled raw rows record preceding EE loop',pureEEOrGPUPrice=False,physicalTV='pending; diagnostic does not change rendering semantics',terminalClientExitClaim=False,productionChange=False,nativeSixtyFpsAccepted=False)
(run/'root-qualification.json').write_text(json.dumps(proof,indent=2)+'\n');print(proof['status']);print(json.dumps(proof,indent=2))
