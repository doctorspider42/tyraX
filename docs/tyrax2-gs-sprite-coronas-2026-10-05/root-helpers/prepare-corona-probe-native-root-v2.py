from pathlib import Path
import hashlib,json,shutil,importlib.util
lab=Path('F:/Projects/tyrax2-lab-20261001');base=lab/'wild-gs-sprite-corona-physical-v2';design=lab/'wild-gs-sprite-corona-probe-design-v2/source';out=lab/'wild-gs-sprite-corona-probe-physical-v2';assert not out.exists();out.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((base/'target-source-manifest.json').read_text())['files'];assert len(m)==500
for rel,h in m.items():
 assert sha(base/rel)==h;d=out/rel;d.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/rel,d)
for rel in ('game/inc/pool2_static_probe.hpp','game/src/terrain_game.cpp','game/src/main.cpp'):
 d=out/rel;d.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(design/rel,d)
for name in ('res','.res-baked'):shutil.copytree(base/'game'/name,out/'game'/name)
spec=importlib.util.spec_from_file_location('q',Path('F:/Projects/tyra-editor/tools/tyrax2-quiet-fixture.py'));q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q);actual=q.source_inputs(out,'vehicle-playground.tyra');assert len(actual)==501
changes={r:h for r,h in actual.items() if m.get(r)!=h};assert set(changes)=={'game/inc/pool2_static_probe.hpp','game/src/terrain_game.cpp','game/src/main.cpp'}
record={'frozen':True,'status':'ROOT_FROZEN_CORONA_PROBE_SOURCE_COMPILE_ONLY','files':actual,'pricingManifestSha256':sha(base/'target-source-manifest.json'),'diagnosticDeltas':changes};(out/'target-source-manifest.json').write_bytes((json.dumps(record,indent=2)+'\n').encode());freeze={'status':'ROOT_EARLY_CORONA_PROBE_SOURCE_FREEZE','sourceManifestSha256':sha(out/'target-source-manifest.json'),'sourceFiles':501,'nativeAccepted':False,'runtimeAccepted':False};(out/'root-source-freeze.json').write_bytes((json.dumps(freeze,indent=2)+'\n').encode());print(freeze)
