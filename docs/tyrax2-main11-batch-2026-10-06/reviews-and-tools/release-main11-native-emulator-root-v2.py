from pathlib import Path
import json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');f=lab/'night-main11-batch-physical-v1';h=lab/'night-main11-batch-root-helpers-v1'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
m=read(f/'target-source-manifest.json');assert m['frozen'] and len(m['files'])==501
for rel,d in m['files'].items():assert sha(f/rel)==d
native=lab/'night-main11-batch-native-root-review-v1/proof.json';n=read(native);assert n['status'].startswith('PASS_') and not n['blockers']
assert n['sourceManifestSha256']==sha(f/'target-source-manifest.json')
control=lab/'night-main11-batch-controls-v1/root-host-v1/host-controls-proof.json'
source=lab/'night-main11-batch-source-controls-v1/root-host-proof.json'
assert read(source)['status']=='PASS' and len(read(source)['checks'])==33 and all(c['pass'] for c in read(source)['checks'])
assert read(control)['status']=='PASS_KIND29_HOST_SYNTHETIC_AND_CLI_ONLY' and len(read(control)['checks'])==52
host=read(h/'host-authority.json');assert host['pricingSourceManifestSha256'] is None
host['pricingSourceManifestSha256']=sha(f/'target-source-manifest.json');host['sourceSchemaDraft']=False
host['rootParserControlsSha256']=sha(control);host['rootSourceControlsSha256']=sha(source)
(h/'host-authority-draft.json').write_bytes((h/'host-authority.json').read_bytes())
(h/'host-authority.json').write_bytes((json.dumps(host,indent=2)+'\n').encode())
review=h/'source-review.md'
review.write_bytes(review.read_bytes()+b'\nRoot source/native/host qualification now complete for the narrow guarded stationary trial. Root independently executes 33 source controls, 52 parser controls and 18 symbolic native closure controls. Native V55 binds501 source inputs/298 assets, actual target sampler ABI, source mirrors/dependencies/link and unchanged16 VU images. This permits emulator qualification only at this stage; physical activation/output/gain remains pending. Core acceptance is not final VU/GS output. Cold old-copy mismatches after legitimate dirty demotion can invalidate capture, so this is not general moving-scene qualification. Initial carrier preparation/common guards/class and memory cost remain unpriced against production.\n')
release=f/'root-runtime-authority.json';assert not release.exists()
r={'status':'ROOT_NATIVE_HOST_SOURCE_PRIVATE_MAIN11_EMULATOR_TRIAL','scope':'Isolated kind29 stationary fullnight sameELF candidate. Original carriers and main draw order preserved in guarded domain; fallback on changed keys. Emulator output/activation first, physical trial pending. No production promotion or fps/gain claim.',
 'review':str(review),'reviewSha256':sha(review),'host':str(h/'host-authority.json'),'hostSha256':sha(h/'host-authority.json'),
 'native':str(native),'nativeSha256':sha(native),'rootSourceFreezeSha256':sha(f/'root-source-freeze.json'),
 'hostControlSha256':sha(control),'sourceControlSha256':sha(source),'physicalPricingAccepted':False,'commonObserverCodeCostPriced':False,
 'independentReviewPins':{str(p):sha(p) for p in [lab/'night-main11-batch-independent-review-v1/report.md',lab/'night-main11-batch-independent-review-v1/parser-repair-review.md']}}
release.write_bytes((json.dumps(r,indent=2)+'\n').encode())
print('ROOT_NATIVE_HOST_SOURCE_MAIN11_EMULATOR_READY',sha(release))
