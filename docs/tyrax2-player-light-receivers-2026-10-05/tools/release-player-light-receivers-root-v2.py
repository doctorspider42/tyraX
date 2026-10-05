from pathlib import Path
import json,hashlib,subprocess
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'player-light-receivers-physical-v2';h=b/'player-light-receivers-root-helpers-v2';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();native=b/'player-light-receivers-native-root-review-v2-retry1/proof.json';n=json.loads(native.read_text());assert n['status'].startswith('PASS_')and not n['blockers'];host=json.loads((h/'host-authority.json').read_text())
for p,d in host['sourcePins'].items():assert sha(Path(p))==d
q=dict(status='ROOT_RELEASE_PRIVATE_RECEIVER_COMPARISON',scope='Night fixture receiver quality tradeoff. Model body, animated pickup, driven wheels, vehicle glass, highlight deferred body. Other world passes default world. No claim of all-project identity propagation yet.',review=str(h/'source-review.md'),reviewSha256=sha(h/'source-review.md'),host=str(h/'host-authority.json'),hostSha256=sha(h/'host-authority.json'),native=str(native),nativeSha256=sha(native),rootSourceFreezeSha256=sha(f/'root-source-freeze.json'),physicalPricingAccepted=False,commonReceiverBranchCostPriced=False)
assert not(f/'root-runtime-authority.json').exists();(f/'root-runtime-authority.json').write_bytes((json.dumps(q,indent=2)+'\n').encode())
for kind in(10,11):
 p=b/f'run-player-receivers-emulator-kind{kind}-root-v2.py';assert not p.exists();p.write_bytes(f'''import subprocess
from pathlib import Path
b=Path('/mnt/f/Projects/tyrax2-lab-20261001');h=b/'player-light-receivers-root-helpers-v2';s='night-ablation-emulator-player-receivers-kind{kind}-order0-20261005'
subprocess.run(['python3',str(h/'launch-emulator.py'),'--fixture',str(b/'player-light-receivers-physical-v2'),'--stem',s,'--kind','{kind}','--order','0'],check=True)
subprocess.run(['python3',str(h/'qualify-emulator.py'),'--stem',s],check=True)
'''.encode())
print(q['status'])
