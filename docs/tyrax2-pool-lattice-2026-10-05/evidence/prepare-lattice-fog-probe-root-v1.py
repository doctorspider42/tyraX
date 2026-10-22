from pathlib import Path
import hashlib,json,shutil,importlib.util
lab=Path('F:/Projects/tyrax2-lab-20261001');base=lab/'wild-pool-lattice-physical-v7';design=lab/'wild-pool-lattice-fog-probe-design-v1';f=lab/'wild-pool-lattice-fog-probe-physical-v1';assert not f.exists();f.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((base/'target-source-manifest.json').read_text());assert len(m['files'])==501
for rel,h in m['files'].items():
 assert sha(base/rel)==h;dst=f/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/rel,dst)
delta=[]
for rel in ('game/inc/pool2_static_probe.hpp','game/src/terrain_game.cpp'):
 src=design/'source'/rel;dst=f/rel;delta.append({'path':rel,'baseSha256':m['files'].get(rel),'sha256':sha(src)});dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
for name in ('res','.res-baked'):shutil.copytree(base/'game'/name,f/'game'/name)
spec=importlib.util.spec_from_file_location('q',Path('F:/Projects/tyra-editor/tools/tyrax2-quiet-fixture.py'));q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q);files=q.source_inputs(f,'vehicle-playground.tyra');assert len(files)==502
manifest={'frozen':True,'status':'ROOT_FROZEN_FOG_DIAGNOSTIC_SOURCE_ONLY','files':files,'pricingManifestSha256':sha(base/'target-source-manifest.json'),'diagnosticDelta':delta};(f/'target-source-manifest.json').write_bytes((json.dumps(manifest,indent=2)+'\n').encode())
freeze={'status':'ROOT_EARLY_FOG_DIAGNOSTIC_SOURCE_FREEZE','sourceManifestSha256':sha(f/'target-source-manifest.json'),'sourceFiles':502,'diagnosticOnly':True,'diagnosticDelta':delta};(f/'root-source-freeze.json').write_bytes((json.dumps(freeze,indent=2)+'\n').encode())
print(freeze['status'],freeze['sourceManifestSha256'])
