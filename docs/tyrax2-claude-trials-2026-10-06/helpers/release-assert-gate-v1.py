from pathlib import Path
import json,hashlib,sys
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'assert-gate-physical-v1';h=b/'assert-gate-root-helpers-v1';native=b/'assert-gate-native-root-review-v1/proof.json';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
n=json.loads(native.read_text(encoding='utf8'));assert n['status'].startswith('PASS_')and not n['blockers']
sys.path.insert(0,str(h));from native_binding import verify_native_binding
provenance=json.loads((f/'root-native-provenance.json').read_text(encoding='utf8'));game=f/'game/bin'
verify_native_binding({'nativeSha256':sha(native)},n,provenance,sha(f/'target-source-manifest.json'),sha(game/'vehicle-playground.elf'),sha(game/'vehicle-playground.elf.sym'),sha(native))
m=json.loads((f/'target-source-manifest.json').read_text());freeze=json.loads((f/'root-source-freeze.json').read_text());assert m['frozen']and freeze['frozen']and freeze['assertStatements']==12
assert freeze['sourceManifestSha256']==sha(f/'target-source-manifest.json')
host=json.loads((h/'host-authority.json').read_text(encoding='utf8'));assert host['pricingSourceManifestSha256']==sha(f/'target-source-manifest.json')
for p,d in host['sourcePins'].items():assert sha(Path(p))==d
q=dict(status='ROOT_RELEASE_PRIVATE_ASSERT_GATE',scope='Private known-good fixture only: On bypasses twelve original validation statements in hot frames. Both arms execute all original checks in sparse cold frames. Original ENV_NORMALIZED, fog, geometry and packet operations retained. No NDEBUG or public API change. Hot bypass counts are not observed; cold activation/source branch only. Common gate branches are unpriced. No production safety or optimized-renderer promotion claim.',review=str(h/'source-review.md'),reviewSha256=sha(h/'source-review.md'),host=str(h/'host-authority.json'),hostSha256=sha(h/'host-authority.json'),native=str(native),nativeSha256=sha(native),rootSourceFreezeSha256=sha(f/'root-source-freeze.json'),hostControlSha256=sha(b/'assert-gate-host-controls-v1.json'),physicalPricingAccepted=False,commonObserverCodeCostPriced=False)
binding_controls=b/'assert-gate-native-binding-host-controls-v1.json';binding=json.loads(binding_controls.read_text());assert binding['status']=='PASS_PURE_HOST_NATIVE_IDENTITY_CLOSURE_MUTATIONS'and binding['verifierSha256']==sha(h/'native_binding.py')
q.update(nativeBindingVerifierSha256=sha(h/'native_binding.py'),nativeBindingHostControlsSha256=sha(binding_controls))
p=f/'root-runtime-authority.json';assert not p.exists();p.write_bytes((json.dumps(q,indent=2)+'\n').encode());print(q['status'])
