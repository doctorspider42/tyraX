from pathlib import Path
import shutil,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');old=lab/'wild-pool-table-root-v3';out=lab/'wild-pool2-ee-root-v1'
assert not out.exists();out.mkdir()
for name in ('freeze-input.py','build-native.ps1','complete-native-provenance.py','compile-abi.py',
             'launch-emulator.py','qualify-emulator.py','stop-emulator.py','prepare-physical.py',
             'launch-physical-observed.py','finish-physical.ps1','finish-physical-no-reset.ps1'):
 source=old/name
 if not source.exists():source=lab/'night-ablation-root-v3'/name
 text=source.read_text(encoding='utf-8')
 if name=='prepare-physical.py':
  assert "fixture=lab/'wild-pool-table-physical-v3'" in text
  text=text.replace("fixture=lab/'wild-pool-table-physical-v3'","fixture=lab/'wild-pool2-ee-physical-v1'")
 (out/name).write_bytes(text.encode())
audit=(lab/'audit-pool2-native-v3.py').read_text()
audit=audit.replace('PASS_INDEPENDENT_POOL2_ACTUAL_NATIVE_SOURCE_DEPENDENCIES_LINKED_TC_IMAGE_ABI_ASSETS',
                    'PASS_ROOT_POOL2_EE_ACTUAL_NATIVE_SOURCE_DEPENDENCIES_LINKED_TC_IMAGE_ABI_ASSETS')
(out/'audit-native.py').write_bytes(audit.encode())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'helper-pins.json').write_bytes((json.dumps({'files':{p.name:sha(p) for p in out.iterdir() if p.is_file()},
  'scope':'Adapted private root helpers; no process/build/device started by preparation'},indent=2)+'\n').encode())
print('Private runtime helpers prepared without device mutation')
