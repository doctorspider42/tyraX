from pathlib import Path
import json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'companion-census-physical-v1';h=b/'companion-census-root-helpers-v1';native=b/'companion-census-native-root-review-v1/proof.json';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();n=json.loads(native.read_text(encoding='utf8'));assert n['status'].startswith('PASS_')and not n['blockers']
import sys
sys.path.insert(0,str(h))
from runtime_guard import verify_native_binding
manifest=json.loads((f/'target-source-manifest.json').read_text(encoding='utf8'));assert manifest['frozen']and len(manifest['files'])==501
for rel,d in manifest['files'].items():assert sha(f/rel)==d,rel
provenance=json.loads((f/'root-native-provenance.json').read_text(encoding='utf8'));game=f/'game/bin'
verify_native_binding(n,provenance,sha(f/'target-source-manifest.json'),sha(game/'vehicle-playground.elf'),sha(game/'vehicle-playground.elf.sym'),sha(native),sha(native))
host=json.loads((h/'host-authority.json').read_text(encoding='utf8'))
assert not host.get('draftUnreleased',True)and host['pricingSourceManifestSha256']==sha(f/'target-source-manifest.json')
for p,d in host['sourcePins'].items():assert sha(Path(p))==d
q=dict(status='ROOT_RELEASE_PRIVATE_COMPANION_CENSUS',scope='Sparse cold companion/rebuild activation census at750/1155; day/night phase elapsed is a workload contrast, not observer tax or optimization gain. No additional Count scopes, effect cuts or consumer fences. Retained acquisition rebuilds are attempts and may be refused. Preparation-time draft review status is superseded by root freeze/native/release. No production promotion or visual equivalence claim.',review=str(h/'source-review.md'),reviewSha256=sha(h/'source-review.md'),host=str(h/'host-authority.json'),hostSha256=sha(h/'host-authority.json'),native=str(native),nativeSha256=sha(native),rootSourceFreezeSha256=sha(f/'root-source-freeze.json'),hostControlSha256=sha(b/'companion-census-host-controls-v1.json'),physicalPricingAccepted=False,commonObserverCodeCostPriced=False)
p=f/'root-runtime-authority.json';assert not p.exists();p.write_bytes((json.dumps(q,indent=2)+'\n').encode());print(q['status'])
