from pathlib import Path
import hashlib,json
r=Path(__file__).parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
n=json.loads((r/'vehicle/native-proof.json').read_text())
h=(r/'tyra/engine/inc/debug/loop_balance.hpp').read_text()
assert 'pacingActive' in h and 'pacedCalls' in h
assert n['actualELFSha256']=='d686eda2e78d488fd3c7dada64821a6663825c2f28aa566776150de245c16b00'
assert n['linkedBuffers']['LoopBalance::samples']['bytes']==17408
assert n['linkedBuffers']['RenderAudit::samples']['bytes']==24064
proof=dict(status='SUPPLEMENTAL_V2_PACING_COVERAGE_CORRECTION',elfSha256=n['actualELFSha256'],nativeProofSha256=sha(r/'vehicle/native-proof.json'),inheritedCoverageSha256=sha(r/'vehicle/coverage-review.json'),loopBalanceHeaderSha256=sha(r/'tyra/engine/inc/debug/loop_balance.hpp'),balanceRowBytes=136,balanceSampleBufferBytes=17408,renderAuditRowBytes=188,renderAuditSampleBufferBytes=24064,correction='The inherited coverage reviewer still prints the v1 15872-byte buffer descriptor and outer-leaf phrasing. The bound v2 native proof carries the actual 17408-byte symbol. Presentation starts exclusion before flipBuffers; leaf wait calls made while pacingActive are counted separately with no new clock and no duplicated interval. Strict runtime closes both partitions and matches existing pacing. Original pinned reviewer files are retained rather than silently rewritten.',historicalLoopBalanceCaveat='The earlier 49fbb9d2 ELF timed two GIF waits inside pacing again. Its explicit wait sum and exclusive render estimate overlap/subtract that tiny interval; the v2 fixture corrects ownership. Historical evidence stays unchanged; render dominance is supported by the follow-up, not by claiming the old categories were disjoint.')
(r/'vehicle/v2-correction-review.json').write_text(json.dumps(proof,indent=2)+'\n')
