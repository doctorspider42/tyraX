from pathlib import Path
import json
lab=Path('F:/Projects/tyrax2-lab-20261001');src=lab/'release-main11-native-emulator-root-v1.py';dst=lab/'release-main11-native-emulator-root-v2.py';assert not dst.exists()
s=src.read_text().replace("read(source)['checks']==33","len(read(source)['checks'])==33 and all(c['pass'] for c in read(source)['checks'])")
s=s.replace("host=read(h/'host-authority.json')","assert read(control)['status']=='PASS_KIND29_HOST_SYNTHETIC_AND_CLI_ONLY' and len(read(control)['checks'])==52\nhost=read(h/'host-authority.json')")
dst.write_bytes(s.encode())
(lab/'main11-root-release-preflight-rejected-v1.json').write_text(json.dumps({'status':'REJECTED_ROOT_PREFLIGHT_NO_EMULATOR_START','reason':'Root treated source proof checks list as integer33; check rejected before authority creation. Subsequent dependent calls failed closed before process/config/archive creation. Corrected rootv2 tests length and each PASS. Native/source bytes unchanged.','rejectedStem':'night-ablation-emulator-main11-batch-order0-20261006','runtimeStarted':False},indent=2)+'\n')
