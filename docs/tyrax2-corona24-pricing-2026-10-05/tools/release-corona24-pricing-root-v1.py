from pathlib import Path
import json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');f=lab/'corona24-pricing-physical-v1';h=lab/'corona24-pricing-root-helpers-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=lab/'corona24-pricing-native-root-review-v1/proof.json';n=json.loads(native.read_text());assert n['status'].startswith('PASS_')and n['sourceFiles']==501 and not n['blockers']
host=json.loads((h/'host-authority.json').read_text());assert len(host['sourcePins'])==6
for p,digest in host['sourcePins'].items():assert sha(Path(p))==digest
q=dict(status='ROOT_RELEASE_CORONA24_ORDINARY_SAME_ELF_COMPARISON',physicalPricingAccepted=False,actualOrdinaryAcceptedSpritesKnown=False,scope='Normal full night, unchanged game and normal timing; qualified isolated candidate image. Ordinary requests and complete rendered frames precede end-to-end console pricing, which is not VU/GS stage isolation.',review=str(h/'source-review.md'),reviewSha256=sha(h/'source-review.md'),host=str(h/'host-authority.json'),hostSha256=sha(h/'host-authority.json'),native=str(native),nativeSha256=sha(native),rootSourceFreezeSha256=sha(f/'root-source-freeze.json'),isolatedOutputProofSha256=sha(lab/'corona24-positive-pair-v1.json'),isolatedDrawRasterProofSha256=sha(lab/'corona24-draw-raster-pair-v1.json'))
(f/'root-runtime-authority.json').write_bytes((json.dumps(q,indent=2)+'\n').encode());print(q['status'])
