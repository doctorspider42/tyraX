from pathlib import Path
import json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'light-pick-far-physical-v1';h=b/'light-pick-far-root-helpers-v1';native=b/'light-pick-far-native-root-review-v1/proof.json';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();n=json.loads(native.read_text(encoding='utf8'));assert n['status'].startswith('PASS_')and not n['blockers'];host=json.loads((h/'host-authority.json').read_text(encoding='utf8'))
for p,d in host['sourcePins'].items():assert sha(Path(p))==d
q=dict(status='ROOT_RELEASE_PRIVATE_FAR_LIGHT_COMPARISON',scope='All night effects retained; exponent broad phase with wide margin, original scoring preserved. Untimed cold actual pointer oracle; actual target activation and raster required before pricing.',review=str(h/'source-review.md'),reviewSha256=sha(h/'source-review.md'),host=str(h/'host-authority.json'),hostSha256=sha(h/'host-authority.json'),native=str(native),nativeSha256=sha(native),rootSourceFreezeSha256=sha(f/'root-source-freeze.json'),hostControlSha256=sha(b/'light-pick-far-host-v2/proof.json'),physicalPricingAccepted=False,commonCandidateCodeCostPriced=False);assert not(f/'root-runtime-authority.json').exists();(f/'root-runtime-authority.json').write_bytes((json.dumps(q,indent=2)+'\n').encode())
p=b/'run-light-far-emulator-root-v1.py';assert not p.exists();p.write_bytes(b'''import subprocess
from pathlib import Path
b=Path('/mnt/f/Projects/tyrax2-lab-20261001');h=b/'light-pick-far-root-helpers-v1';s='night-ablation-emulator-light-far-order0-20261005'
subprocess.run(['python3',str(h/'launch-emulator.py'),'--fixture',str(b/'light-pick-far-physical-v1'),'--stem',s,'--kind','12','--order','0'],check=True)
subprocess.run(['python3',str(h/'qualify-emulator.py'),'--stem',s],check=True)
''');print(q['status'])
