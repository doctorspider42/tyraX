from pathlib import Path
import json,hashlib,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');base=lab/'wild-pool2-ee-physical-v2';out=lab/'wild-pool2-ee-probe-physical-v1'
assert not out.exists();out.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=json.loads((base/'target-source-manifest.json').read_text())['files']
for rel,h in files.items():
 assert sha(base/rel)==h,rel
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/rel,dst)
for folder in ('res','.res-baked'):shutil.copytree(base/'game'/folder,out/'game'/folder)
old=lab/'wild-pool2-static-probe-physical-v1/game/src/terrain_game.cpp'
shutil.copyfile(old,out/'game/src/terrain_game.cpp')
source=lab/'wild-pool2-ee-probe-design-v1/source/pool2_static_probe.hpp'
shutil.copyfile(source,out/'game/inc/pool2_static_probe.hpp')
record={'status':'ROOT_DIAGNOSTIC_SOURCE_PREPARATION_ONLY_NOT_FROZEN','basePricingManifestSha256':sha(base/'target-source-manifest.json'),
 'diagnosticCppFromPriorQualifiedLifecycle':sha(old),'diagnosticHeaderSha256':sha(source),
 'workshopProofSha256':sha(lab/'wild-pool2-ee-probe-design-v1/source-proof.json'),
 'onlySourceDifferences':['game/src/terrain_game.cpp','game/inc/pool2_static_probe.hpp'],
 'coldExpansionOracleDisabled':True,'finalColorEpochPerRepeat':True,'sourceCountExpected':500,
 'gatesPending':['independent source review','native','actual paused VU epoch/output capture']}
(out/'root-preparation.json').write_bytes((json.dumps(record,indent=2)+'\n').encode())
print('Prepared separate 500-source lazy/epoch probe; no freeze/build/device action')
