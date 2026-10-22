"""Root-only new INPUT integration; default verifies without writes."""
from pathlib import Path
import argparse,json,hashlib,shutil

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8'))
root=Path(__file__).resolve().parent;lab=root.parent;base=lab/'night-ablation-physical-v3'
p=argparse.ArgumentParser();p.add_argument('--target',type=Path,required=True);p.add_argument('--execute',action='store_true');a=p.parse_args();target=a.target.resolve()
assert target.parent==lab.resolve() and not target.exists(),'Require NEW LAB target'
proof=read(root/'source-proof.json');manifest=read(base/'target-source-manifest.json')
assert sha(base/'target-source-manifest.json')==proof['baselineManifestSha256']
assert len(manifest['files'])==497
for rel,h in manifest['files'].items():assert sha(base/rel)==h,('Frozen V3 drift',rel)
for rel,h in proof['outputPins'].items():assert sha(root/'source'/rel)==h,('Candidate drift',rel)
assert sha(root/'target-source-manifest.json')==proof['targetManifestSha256']
assert sha(root/'pool-color-split.patch')==proof['sourcePatchSha256']
for item in proof['requiredProofs']:
 pp=Path(item['path']);assert sha(pp)==item['sha256'];doc=read(pp);assert doc['status']==item['status']
 for field in ['sourcePins','artifactSha256','documentationSupplementPins']:
  for path,h in doc.get(field,{}).items():assert sha(Path(path))==h,('Host/protocol pin drift',path)
 if 'hostProofPath' in doc:
  hp=Path(doc['hostProofPath']);assert sha(hp)==doc['hostProofSha256'];hd=read(hp)
  for field in ['sourcePins','artifactSha256']:
   for path,h in hd.get(field,{}).items():assert sha(Path(path))==h,('Nested host pin drift',path)
if not a.execute:
 print('PASS read-only candidate/source/host closure; root --execute required');raise SystemExit(0)
ignore=shutil.ignore_patterns('obj','bin','build','__pycache__','*.elf','*.elf.sym','*.log','*.err','*.p2s','*.cfg','ps2link.run')
for name in ['game','tyra']:shutil.copytree(base/name,target/name,ignore=ignore)
for rel,h in proof['outputPins'].items():
 dst=target/rel;dst.write_bytes((root/'source'/rel).read_bytes());assert sha(dst)==h
expected=read(root/'target-source-manifest.json')
for rel,h in expected['files'].items():assert sha(target/rel)==h,('Integrated source mismatch',rel)
(target/'target-source-manifest.json').write_text(json.dumps(expected,indent=2)+'\n',encoding='utf-8',newline='\n')
shutil.copyfile(root/'README.md',target/'README.md')
record={'status':'ROOT_CREATED_PRIVATE_POOL_CACHE_SAME_ELF_INPUT_NOT_NATIVE_ACCEPTED','sourceFiles':497,'sourceProofSha256':sha(root/'source-proof.json'),'integrationScriptSha256':sha(Path(__file__)),'baselineManifestSha256':proof['baselineManifestSha256'],'outputPins':proof['outputPins'],'requiredProofs':proof['requiredProofs'],'nativeAccepted':False,'runtimeAccepted':False}
(target/'integration-proof.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8',newline='\n')
print('Created NEW INPUT; root freeze/native/device/runtime gates still pending')
