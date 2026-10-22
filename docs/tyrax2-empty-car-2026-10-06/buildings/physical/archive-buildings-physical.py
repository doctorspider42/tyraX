from pathlib import Path
import hashlib,json,shutil,re
lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor');run=lab/'tyrax2-closeout-ps2-two-cars-light-buildings-v1';out=repo/'docs/tyrax2-empty-car-2026-10-06'
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
items=json.loads((out/'inventory.json').read_text(encoding='utf8'))
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
d=json.loads((run/'protocol-analysis.json').read_text(encoding='utf8'))
assert d['status']=='PASS_COMPLETED_PRIVATE_TWO_CARS_LIGHT_PINE_EIGHT_BUILDINGS_PROTOCOL'
for cold in d['buildingColdWitnesses'].values():
 for r in cold.values():
  assert int(r['acceptedBags'])==12 and int(r['acceptedVertices'])==11328
  assert all(int(r['refs'+str(i)])>0 for i in range(8))
sample=(run/'bin/closeout-samples.txt').read_text(encoding='utf8')
long={str(p):sum(int(x)/294912>25 for x in re.findall(r'LOG: CLOSEOUTRAW phase='+str(p)+r' .*?entryPeriod=(\d+)',sample))for p in range(3)}
means=[d['byPhase'][str(p)]['chunkMeanNonPacingMs']for p in range(3)]
for r in d['byPhase'].values():assert r['renderedCompletions']==320 and r['syntheticCompletions']==0
assert sum(long.values())==0
review=dict(status='ROOT_QUALIFIED_BOUNDED_EIGHT_BUILDING_PHYSICAL_CADENCE',loops=5400,protocolPassed=True,byPhase=d['byPhase'],rawEntryPeriodsOver25ms=long,approximately59_94HzObservedWindows=True,buildingBagsAtAllSixColdFrames=12,buildingInputVerticesAtAllSixColdFrames=11328,allEightOwnerMembershipsPositive=True,commonObserverFootprintUnpriced=True,historicalELFDifferencesNotCausalIncrementalPricing=True,userReportedUSBRemovedBeforeStart=True,USBResetCauseNotEstablished=True,visualConfirmationPending=True,everyFrameScanoutProved=False)
(run/'root-cadence-review.json').write_bytes((json.dumps(review,indent=2)+'\n').encode())
copies=[(run/name,'buildings/physical/'+name)for name in ['completed-stdout.raw','stdout.utf8.log','bin/closeout-samples.txt','launch-inputs.json','owned-launch.json','protocol-analysis.json','physical-review.json','root-cadence-review.json']]
copies +=[(lab/name,'buildings/physical/'+name)for name in ['finish-buildings-physical.py','archive-buildings-physical.py']]
for src,rel in copies: assert src.is_file() and not(out/rel).exists(),rel
for src,rel in copies:
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);items.append(dict(path=rel,sha256=h(dst)))
(out/'inventory.json').write_bytes((json.dumps(items,indent=2)+'\n').encode())
note=f'''\n### Physical eight-building result\n\nThe user physically restarted PS2 and removed USB before this run. The reviewed ELF started with fresh loadelf/Loaded evidence and completed 5400 loops. Parked / short drive / post-drive non-pacing work is {means[0]:.4f} / {means[1]:.4f} / {means[2]:.4f} ms. All measured windows remain approximately 59.94 Hz, each with 320 rendered and zero synthetic completions. None of the 384 raw entry periods exceeds 25 ms.\n\nAt all six cold observations, buildings contribute 12 accepted carriers / 11,328 input vertices with all eight owner memberships positive. Road, both cars and pine reconcile to 24 total carriers / 20,661 vertices. Vehicle light paths remain active. This scene still meets the observed 60 FPS class; greater geometry does not alone break this particular workload. Separate ELF differences do not price triangles alone, and counts are accepted input rather than postclip emitted geometry. The whole non-pacing loop includes waits. Visual confirmation is pending. USB removal is recorded, but no reset-cause conclusion follows from a successful cold start.\n'''
p=out/'README.md';p.write_bytes((p.read_text(encoding='utf8')+note).encode())
p=repo/'docs/tyrax2-closeout.md';p.write_bytes((p.read_text(encoding='utf8').rstrip()+f'\n\nThe eight-building ELF subsequently completes 5400 loops on PS2 at {means[0]:.4f}/{means[1]:.4f}/{means[2]:.4f} ms non-pacing work, retaining approximately 59.94 Hz in all observed windows and zero synthetic completions. All eight building owners are represented in 12 accepted carriers / 11,328 input vertices. Visual confirmation remains pending; USB was removed before the cold start, which does not establish the reset failure cause.\n').encode())
for e in items:assert h(out/e['path'])==e['sha256'],e['path']
print('PASS physical building archive',len(items),means)
