from pathlib import Path
import hashlib,json,shutil,importlib.util
lab=Path('F:/Projects/tyrax2-lab-20261001');src=lab/'wild-gs-sprite-corona-prototype-v6';base=lab/'wild-pool2-ee-physical-v2';out=lab/'wild-gs-sprite-corona-physical-v2';assert not out.exists();out.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
handoff=json.loads((src/'final-handoff-proof.json').read_text());manifest=src/'target-source-manifest.json';assert sha(manifest)==handoff['sourceManifestSha256'];m=json.loads(manifest.read_text())['files'];assert len(m)==500
for rel,h in m.items():
 assert sha(src/rel)==h;dest=out/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src/rel,dest)
for name in ('res','.res-baked'):shutil.copytree(base/'game'/name,out/'game'/name)
spec=importlib.util.spec_from_file_location('q',Path('F:/Projects/tyra-editor/tools/tyrax2-quiet-fixture.py'));q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q);actual=q.source_inputs(out,'vehicle-playground.tyra');assert actual==m
record={'frozen':True,'status':'ROOT_FROZEN_CORONA_SOURCE_COMPILE_ONLY','files':actual,'handoffSha256':sha(src/'final-handoff-proof.json')};(out/'target-source-manifest.json').write_bytes((json.dumps(record,indent=2)+'\n').encode());freeze={'status':'ROOT_EARLY_CORONA_SOURCE_FREEZE','sourceManifestSha256':sha(out/'target-source-manifest.json'),'sourceFiles':500,'nativeAccepted':False,'runtimeAccepted':False};(out/'root-source-freeze.json').write_bytes((json.dumps(freeze,indent=2)+'\n').encode());print(freeze['status'],freeze['sourceManifestSha256'])
