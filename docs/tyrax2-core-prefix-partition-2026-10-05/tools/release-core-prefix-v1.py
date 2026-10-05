from pathlib import Path
import json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'core-prefix-partition-physical-v1';h=b/'core-prefix-root-helpers-v1';native=b/'core-prefix-native-root-review-v1/proof.json';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();n=json.loads(native.read_text(encoding='utf8'));assert n['status'].startswith('PASS_')and not n['blockers']
host=json.loads((h/'host-authority.json').read_text(encoding='utf8'))
for p,d in host['sourcePins'].items():assert sha(Path(p))==d
q=dict(status='ROOT_RELEASE_PRIVATE_CORE_PREFIX_PARTITION',scope='Instrument existing numerical producers only; no effect removal or new fences. Native source/ABI/assets/linked-microimage qualified. Physical records determine activation, scope elapsed and net observer tax; no visual-equivalence or optimized-renderer promotion claim.',review=str(h/'source-review.md'),reviewSha256=sha(h/'source-review.md'),host=str(h/'host-authority.json'),hostSha256=sha(h/'host-authority.json'),native=str(native),nativeSha256=sha(native),rootSourceFreezeSha256=sha(f/'root-source-freeze.json'),hostControlSha256=sha(b/'core-prefix-host-controls-v1.json'),physicalPricingAccepted=False,commonObserverCodeCostPriced=False)
p=f/'root-runtime-authority.json';assert not p.exists();p.write_bytes((json.dumps(q,indent=2)+'\n').encode());print(q['status'])
