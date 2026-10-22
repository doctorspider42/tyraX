from pathlib import Path
import json,hashlib,re
r=Path('F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-ps2-two-cars-light-buildings-80k-v2')
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
d=json.loads((r/'protocol-analysis.json').read_text(encoding='utf8'))
assert d['status']=='PASS_COMPLETED_PRIVATE_TWO_CARS_LIGHT_PINE_FIFTY_BUILDINGS_80K_PROTOCOL'
for cold in d['buildingColdWitnesses'].values():
 for row in cold.values():
  assert int(row['acceptedVertices'])==70800 and int(row['acceptedBags'])==24
  assert all(int(row['refs'+str(i)])>0 for i in range(50))
for cold in d['renderColdWitnesses'].values():
 for row in cold.values():assert int(row['acceptedVertices'])==80133 and int(row['acceptedBags'])==36
for row in d['byPhase'].values():
 assert row['renderedCompletions']==320 and row['syntheticCompletions']==0
 assert abs(row['chunkMeanEntryMs']-16.6833)<.001
s=(r/'bin/closeout-samples.txt').read_text(encoding='utf8')
long=sum(int(x)/294912>25 for x in re.findall(r'entryPeriod=(\d+)',s));assert long==0
review=dict(status='ROOT_QUALIFIED_BOUNDED_80133_VERTEX_PHYSICAL_CADENCE',protocolSha256=h(r/'protocol-analysis.json'),physicalReviewSha256=h(r/'physical-review.json'),loops=5400,byPhase=d['byPhase'],rawEntryPeriodsOver25ms=long,measuredRenderedCompletions=960,measuredSyntheticCompletions=0,allSixColdBuildingVertices=70800,allSixColdTotalVertices=80133,allSixColdBuildingCarriers=24,allSixColdTotalCarriers=36,all50OwnerMembershipsPositive=True,approximately59_94HzObservedWindows=True,userVisualReport='Wygląda dobrze',userVisualScope='Current 80k scene cars and facades, not retroactive eight-building ELF certification.',userReportedUSBRemovedBeforeStart=True,networkResetWithoutUSBFailedFreshELFStart=True,successfulColdStartAfterManualReset=True,USBFailureCauseNotEstablished=True,clientLeftServing=True,noFurtherResetPlanned=True,historicalELFDifferencesNotCausalIncrementalPricing=True,acceptedInputsNotPostclipTriangles=True,nonPacingInclusiveLoopNotPureEE=True,commonObserverFootprintUnpriced=True,allFrameScanoutNotProved=True)
review['protocolPassed']=True
(r/'root-cadence-review.json').write_bytes((json.dumps(review,indent=2,ensure_ascii=False)+'\n').encode())
print('PASS root physical review', [x['chunkMeanNonPacingMs']for x in d['byPhase'].values()])
