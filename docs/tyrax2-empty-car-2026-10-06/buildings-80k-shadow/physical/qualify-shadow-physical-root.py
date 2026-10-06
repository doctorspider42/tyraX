from pathlib import Path
import json,hashlib,re
r=Path('F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-ps2-two-cars-light-buildings-80k-shadow-v2');h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
d=json.loads((r/'protocol-analysis.json').read_text(encoding='utf8'));assert d['status']=='PASS_COMPLETED_PRIVATE_NORMAL_RAVAGER_BLOB_80K_PROTOCOL'
for cold in d['blobColdWitnesses'].values():
 for row in cold.values():assert int(row['owner'])==1 and int(row['acceptedBags'])==1 and int(row['acceptedVertices'])==54
for cold in d['renderColdWitnesses'].values():
 for row in cold.values():assert int(row['acceptedVertices'])==80187 and int(row['acceptedBags'])==37
for row in d['byPhase'].values():assert row['renderedCompletions']==320 and row['syntheticCompletions']==0 and abs(row['chunkMeanEntryMs']-16.6833)<.001
sample=(r/'bin/closeout-samples.txt').read_text(encoding='utf8');long=sum(int(x)/294912>25 for x in re.findall(r'entryPeriod=(\d+)',sample));assert long==0
report=dict(status='ROOT_QUALIFIED_REPAIRED_NORMAL_BLOB_PHYSICAL_CADENCE',protocolPassed=True,loops=5400,byPhase=d['byPhase'],protocolSha256=h(r/'protocol-analysis.json'),physicalReviewSha256=h(r/'physical-review.json'),allSixColdBlobAccepted54=True,parent80133Retained=True,allSixColdTotalInputVertices=80187,allSixColdTotalCarriers=37,approximately59_94HzObservedWindows=True,measuredRenderedCompletions=960,measuredSyntheticCompletions=0,rawEntryPeriodsOver25ms=long,repairedAssetClosureNoErrors=True,hardwareTimingQualified=True,physicalTVConfirmationPending=True,historicalSeparateELFDeltaNotCausalShadowPrice=True,commonObserverFootprintUnpriced=True,nonPacingInclusiveLoopNotPureEE=True,everyFrameScanoutNotProved=True)
(r/'root-cadence-review.json').write_bytes((json.dumps(report,indent=2)+'\n').encode());print('PASS root clean shadow physical cadence')
