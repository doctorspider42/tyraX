from pathlib import Path
import json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1';emu=lab/'tyrax2-closeout-emulator-two-cars-light-buildings-80k-shadow-v2';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
d=json.loads((emu/'protocol-analysis.json').read_text(encoding='utf8'));assert d['status']=='PASS_COMPLETED_PRIVATE_NORMAL_RAVAGER_BLOB_80K_PROTOCOL'
raw=(emu/'runtime/emulator.log').read_bytes()
assert b'CLOSEOUTDONE'in raw and not any(x in raw for x in (b'Failed to load texture:',b'Texture missing:',b'TmdlLoader: cannot read',b'freepad: DMA Busy'))
for cold in d['blobColdWitnesses'].values():
 for row in cold.values():assert int(row['owner'])==1 and int(row['acceptedBags'])==1 and int(row['acceptedVertices'])==54
n=json.loads((r/'native-proof.json').read_text(encoding='utf8'))
review=dict(status='ROOT_QUALIFIED_REPAIRED_SHADOW_NATIVE_AND_EMULATOR_ONLY',rootReviewed=True,emulatorCompleted5400=True,threeOwnedImagesReviewed=True,emulatorOwnedExitConfirmed=True,emulatorOwnedPid=7529,exitEvidence='Exact owned stop followed by root /proc/7529 absence check; Xvfb retained.',allSixColdBlobAccepted54=True,parent80133Retained=True,actualELFSha256=n['actualELFSha256'],protocolSha256=h(emu/'protocol-analysis.json'),emulatorLogSha256=h(emu/'runtime/emulator.log'),sourceReviewSha256=h(r/'source-review.json'),nativeProofSha256=h(r/'native-proof.json'),repairedRuntimeAssetsSha256=h(lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-assets-v2/root-assets-review.json'),visibleSoftShadowUnderRavagerReviewed=True,bothCarsAndLongFacadesReviewed=True,all50OwnersIndependentlyPixelProved=False,hardwareTimingQualified=False,physicalCompleted5400=False,physicalResetReadyConfirmationPending=True,firstBothShadowRunsRejectedForMissingDefaultGlow=True,originalSourceAndELFUnchangedForCorrection=True)
(r/'root-runtime-review.json').write_bytes((json.dumps(review,indent=2)+'\n').encode())
print('PASS repaired emulator; physical repetition remains pending')
