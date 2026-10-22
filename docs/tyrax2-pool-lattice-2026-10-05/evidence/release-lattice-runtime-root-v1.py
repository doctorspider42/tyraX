from pathlib import Path
import json,hashlib,py_compile
lab=Path('F:/Projects/tyrax2-lab-20261001');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
fixture=lab/'wild-pool-lattice-physical-v5'
closure=lab/'wild-pool-lattice-static-matrix-root-closure-v1/proof.json'
assert json.loads(closure.read_text())['status']=='ROOT_CLOSED_TWELVE_LATTICE_EPOCH_CAPTURES_SIX_PACKED_OUTPUT_PAIRS'
paths={
 'review':lab/'wild-pool-lattice-independent-review-v1/v10-supplement.json',
 'host':lab/'wild-pool-lattice-root-v1/host-authority.json',
 'native':lab/'wild-pool-lattice-native-root-review-v1/proof.json',
 'independentNative':lab/'wild-pool-lattice-native-independent-confirmation-v1/proof.json',
 'actualOutput':closure,
 'rootSourceFreeze':fixture/'root-source-freeze.json',
 'sourceManifest':fixture/'target-source-manifest.json',
 'selectedElf':fixture/'game/bin/vehicle-playground.elf',
}
out=fixture/'root-runtime-authority.json';assert not out.exists()
record={'status':'ROOT_RELEASE_KIND8_AFTER_SOURCE_HOST_NATIVE_AND_ACTUAL_OUTPUT'}
for k,p in paths.items():record[k]=str(p);record[k+'Sha256']=sha(p)
out.write_bytes((json.dumps(record,indent=2)+'\n').encode())
root=lab/'wild-pool-lattice-root-v1'
for name in ('launch-emulator.py','prepare-physical.py','qualify-emulator.py','freeze-completed.py'):py_compile.compile(str(root/name),doraise=True)
pins=json.loads((root/'helper-pins.json').read_text())
for name in pins:pins[name]=sha(root/name)
(root/'helper-pins.json').write_bytes((json.dumps(pins,indent=2)+'\n').encode())
print(record['status'],sha(out))
