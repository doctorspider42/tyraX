from pathlib import Path
import argparse,json,hashlib,shutil,importlib.util
ap=argparse.ArgumentParser();ap.add_argument('--revision',required=True);ap.add_argument('--fixture',required=True);ap.add_argument('--preparation',default='preparation.json');a=ap.parse_args();lab=Path('F:/Projects/tyrax2-lab-20261001');base=lab/'wild-pool2-ee-physical-v2';s=lab/a.revision;f=lab/a.fixture;assert s.is_dir()and not f.exists();f.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();bm=json.loads((base/'target-source-manifest.json').read_text())['files'];prep=json.loads((s/a.preparation).read_text());delta=prep.get('changedFiles',prep.get('pins'));d={x['path']:x for x in delta};assert len(d)==14
for rel,h in bm.items():
 src=(s if rel in d else base)/rel;want=d[rel]['sha256']if rel in d else h;assert sha(src)==want;dst=f/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
for rel,row in d.items():
 if rel in bm:assert row['baseSha256']==bm[rel];continue
 assert row['baseSha256']is None and sha(s/rel)==row['sha256'];dst=f/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(s/rel,dst)
for name in('res','.res-baked'):shutil.copytree(base/'game'/name,f/'game'/name)
spec=importlib.util.spec_from_file_location('q',Path('F:/Projects/tyra-editor/tools/tyrax2-quiet-fixture.py'));q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q);files=q.source_inputs(f,'vehicle-playground.tyra');assert len(files)==501
r={'frozen':True,'status':'ROOT_FROZEN_EARLY_COMPILE_ONLY_NO_RUNTIME_ACCEPTANCE','files':files,'reviewSha256':sha(s/a.preparation),'hostProofSha256':None};(f/'target-source-manifest.json').write_bytes((json.dumps(r,indent=2)+'\n').encode());p={'status':'ROOT_EARLY_COMPILE_SOURCE_FREEZE_ONLY','sourceManifestSha256':sha(f/'target-source-manifest.json'),'sourceFiles':501,'sourceDelta14':delta,'readmeSha256':sha(s/'README.md'),'hostReviewPending':True,'nativeAccepted':False,'runtimeAccepted':False};(f/'root-source-freeze.json').write_bytes((json.dumps(p,indent=2)+'\n').encode());print(p['status'],p['sourceManifestSha256'])
