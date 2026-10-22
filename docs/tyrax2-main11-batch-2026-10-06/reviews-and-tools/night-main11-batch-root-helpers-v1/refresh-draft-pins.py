"""Draft host/helper pin refresh only; never creates root runtime authority."""
from pathlib import Path
import hashlib,json
h=Path(__file__).parent;b=h.parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=h/'host-authority.json';d=json.loads(p.read_text(encoding='utf8'));assert d['draftUnreleased']and d['pricingSourceManifestSha256']is None
d['sourcePins']={k:sha(Path(k))for k in d['sourcePins']};d['draftProducerManifestSha256']=sha(b/'night-main11-batch-physical-v1/draft-source-manifest.json');p.write_text(json.dumps(d,indent=2)+'\n',encoding='utf8')
(h/'source-review.md').write_text((h/'README.md').read_text(encoding='utf8'),encoding='utf8')
(h/'preparation.json').write_text(json.dumps(dict(status='DRAFT_KIND29_UNRELEASED',concreteProducerSchemaImplemented=True,sourceSchemaIndependentReviewPending=True,nativeAuthorityAvailable=False,physicalRuntimeAccepted=False,hostControls=52,nativeClosureSymbolicControls=18,sourceCounterOracleIsNotExecutionProof=True),indent=2)+'\n',encoding='utf8')
(h/'helper-pins.json').write_text(json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n',encoding='utf8')
print('DRAFT_PINS_REFRESHED_NO_RELEASE',sha(h/'host-authority.json'))
