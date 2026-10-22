from pathlib import Path
import hashlib,json
P=Path(__file__).parent;L=P.parent;old=L/'wild-pool2-static-probe-design-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=P/'proof.json';assert not out.exists()
source=L/'wild-pool-table-proposal-v3/source-proof.json'
assert sha(source)=='e63230b7e051ab28cab541c003792ce783109ba9467d3221a80145845b109145'
s=json.loads(source.read_text());assert s['sourceFiles']==499
for rel,h in s['outputPins'].items():assert sha(source.parent/'source'/rel)==h
equal={}
for name in('pool2_static_probe.hpp','decode-vu1-output.py','savestate-reader-pinned.py'):
 assert sha(P/name)==sha(old/name);equal[name]=sha(P/name)
assert 'renderer3D.usePipeline(pipeline)'in (P/'pool2_static_probe.hpp').read_text()
assert 'proposal-v3/source-proof.json'in (P/'integrate-root.py').read_text()
assert len(list((P/'configs').iterdir()))==12
proof=dict(status='PREPARED_SEALED_V3_BOUND_STATIC_PROBE_WORKSHOP_SOURCE_HOST_ONLY_PENDING_ROOT_NATIVE_CAPTURE',
 sealedProposalSha256=sha(source),sealedProposalPath=str(source),sealedOutputPins=s['outputPins'],
 inheritedByteEqualityWithReviewedV1=equal,
 reusedDecoderControls=dict(path=str(old/'host/proof.json'),sha256=sha(old/'host/proof.json'),positive6=True,negative13=True,rerunHere=False),
 reusedMethod93CodecControl=dict(path=str(old/'method93-old-capture-codec-control.json'),sha256=sha(old/'method93-old-capture-codec-control.json'),historicalCaptureNotPool2=True,rerunHere=False),
 freshWorkflowHostProof=dict(path=str(P/'host/proof.json'),sha256=sha(P/'host/proof.json')),
 sourceDeltaAfterRootIntegration='499-source V3 base: 498 unchanged, changed terrain_game.cpp, added static probe header =>500',
 captureCountPlanned=12,pairCountPlanned=6,capturesExecuted=0,typedNativeCompileExecuted=False,rootIntegratorExecuted=False,
 warmPrintedSourceCounters='retained first cold values, not fresh warm witnesses',
 actualWarmArmMarkerOutputStillRequired=True,actualVUOutputGSOrRasterOrPerformanceAccepted=False,
 files={str(f):sha(f)for f in sorted(P.rglob('*'))if f.is_file()and'__pycache__'not in f.parts})
out.write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8');print(sha(out))
