from pathlib import Path
import json,hashlib
p=Path('F:/Projects/tyrax2-lab-20261001');f=p/'wild-gs-sprite-corona-physical-v2';out=f/'root-runtime-authority.json';assert not out.exists();sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest();r={'status':'ROOT_RELEASE_KIND9_EMULATOR_ADMISSION_OBSERVATION_ONLY','physicalPricingAccepted':False,'actualPositiveOutputAccepted':False,'scope':'Observe unchanged Hybrid-depth full-night EE admission only; diagnostic V1 proved actual24bit depth and candidate marker absence. No claim SPRITE worked or physical benefit.'}
for key,rel in {'review':'wild-gs-sprite-corona-native-prep-v1/source-review.md','host':'wild-gs-sprite-corona-root-helpers-v1/host-authority.json','native':'wild-gs-sprite-corona-native-root-review-v3/proof.json','independentNative':'wild-gs-sprite-corona-tc-native-v23-v6/proof.json','rejectedDiagnostic':'corona-probe-case1-arm1-repeat1-20261005-v1-launch/root-rejected-capture.json'}.items():
 x=p/rel;r[key]=str(x);r[key+'Sha256']=sha(x)
r['rootSourceFreezeSha256']=sha(f/'root-source-freeze.json');out.write_bytes((json.dumps(r,indent=2)+'\n').encode());print(r['status'])
