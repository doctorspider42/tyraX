from pathlib import Path
import hashlib,json,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001')
repo=Path('F:/Projects/tyra-editor')
out=repo/'docs/tyrax2-night-shift-2026-10-06'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,data):
 p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((json.dumps(data,indent=2)+'\n').encode())
def copy(src,dst):
 dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);assert sha(src)==sha(dst)
def tree(src,dst):
 assert src.exists(),src
 for f in src.rglob('*'):
  if f.is_file() and '__pycache__' not in f.parts and f.suffix in ('.json','.jsonl','.py','.ps1','.md','.log','.err','.cfg','.txt','.cpp','.hpp','.h','.png','.run'):
   copy(f,dst/f.relative_to(src))
for fixture in ('cycle-reuse-physical-v1','object-route-physical-v1','spot-result-physical-v1','spot-result-physical-v2'):
 root=lab/fixture;manifest=json.loads((root/'target-source-manifest.json').read_text())
 for name in ('target-source-manifest.json','root-native-provenance.json','root-source-freeze.json','root-runtime-authority.json','runtime-assets-manifest.json'):
  copy(root/name,out/'fixtures'/fixture/name)
 for rel,digest in manifest['files'].items():
  src=root/rel;assert sha(src)==digest,(fixture,rel)
  dst=out/'source-blobs'/digest
  if not dst.exists():copy(src,dst)
for family,versions in (('cycle-reuse',(1,)),('object-route',(1,)),('spot-result',(1,2))):
 for v in versions:
  for suffix in ('controls','root-helpers','native-root-review','target-abi'):
   tree(lab/f'{family}-{suffix}-v{v}',out/'controls'/f'{family}-{suffix}-v{v}')
  extra=lab/f'{family}-source-controls-v{v}'
  if extra.exists():tree(extra,out/'controls'/extra.name)
  for pattern in (f'*{family}*v{v}.py',f'*{family}*v{v}.ps1',f'{family}-host-controls-v{v}.json'):
   for src in lab.glob(pattern):
    if src.is_file():copy(src,out/'helpers'/src.name)
for v in range(48,52):copy(lab/f'night-ablation-native-v{v}.log',out/'native'/f'night-ablation-native-v{v}.log')
for name in ('record-night-root-visual-review-v1.py','run-claude-night-review-v1.py','verify-night-shift-archive-v1.py'):
 copy(lab/name,out/'helpers'/name)
copy(lab/'wild-pool-table-runtime-tools-v3/normalize-physical.py',out/'helpers/normalize-physical.py')
copy(lab/'minimal-stage-stapip-physical-v10/tools/complete-native-provenance.py',out/'helpers/complete-native-provenance.py')
for family in ('cycle-reuse','object-route','spot-result-v2'):
 for order in (0,1):
  stem=f'night-ablation-ps2-{family}-order{order}-20261006'
  tree(lab/(stem+'-evidence'),out/'physical'/stem)
 stem=f'night-ablation-emulator-{family}-order0-20261006'
 tree(lab/(stem+'-evidence'),out/'emulator'/stem)
 tree(lab/(stem+'-launch'),out/'emulator-launch'/stem)
tree(lab/'night-ablation-ps2-spot-result-order1-20261006-rejected-oracle',out/'rejected/spot-result-v1-oracle')
for family in ('cycle-reuse','object-route','spot-result-v2'):
 for order in (0,1):
  stem=f'night-ablation-ps2-{family}-order{order}-20261006'
  for suffix in ('.err','-launcher.log','-launcher.err','-postrun-reset.log','-postrun-reset.err'):
   src=lab/(stem+suffix)
   if src.exists():copy(src,out/'physical-host'/src.name)
for stem in ('cycle-reuse-between-orders-reset-20261006','object-route-between-orders-reset-20261006','spot-result-v2-start-reset-20261006','spot-result-v2-between-orders-reset-20261006'):
 for ext in ('.log','.err','.pid'):
  src=lab/(stem+ext)
  if src.exists():copy(src,out/'physical-host'/src.name)
if (lab/'claude-night-review-20261006/completion.json').exists():tree(lab/'claude-night-review-20261006',out/'claude-review')
copy(Path(__file__),out/'helpers'/Path(__file__).name)
files={str(f.relative_to(out)).replace('\\','/'):sha(f) for f in sorted(out.rglob('*')) if f.is_file() and f.name!='payload-manifest.json'}
write(out/'payload-manifest.json',{'status':'FROZEN_NIGHT_SHIFT_DIAGNOSTIC_EVIDENCE','files':files,'fileCount':len(files),'sourceBlobCount':len(list((out/'source-blobs').iterdir())),'binaryPolicy':'ELF, symbol files, host executables and object binaries omitted; artifact hashes retained in provenance. Assets bound by manifest, not copied.','performancePolicy':'Only accepted physical same-ELF contrasts are prices. Rejected oracle and emulator costs are not hardware performance evidence.'})
print('Archived',len(files),'files',sum(f.stat().st_size for f in out.rglob('*') if f.is_file()),'bytes')
