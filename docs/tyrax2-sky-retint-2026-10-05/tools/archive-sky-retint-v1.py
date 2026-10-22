from pathlib import Path
import shutil,json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');out=Path('docs/tyrax2-sky-retint-2026-10-05');out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();bins={}
def cp(src,dst):
 if src.suffix in ('.elf','.sym'):
  bins[str(src)]=dict(sha256=sha(src),bytes=src.stat().st_size);return
 dst.parent.mkdir(parents=True,exist_ok=True)
 if dst.exists():assert sha(dst)==sha(src),str(dst)
 else:shutil.copyfile(src,dst)
def tree(src,dst):
 for p in src.rglob('*'):
  if p.is_file()and '__pycache__' not in p.parts:cp(p,dst/p.relative_to(src))
fixtures=['core-reuse-census-physical-v1','core-reuse-census-physical-v2','core-reuse-census-physical-v5','sky-retint-physical-v1','sky-retint-physical-v2']
for name in fixtures:
 f=b/name;m=json.loads((f/'target-source-manifest.json').read_text(encoding='utf8'))
 for rel,digest in m['files'].items():
  p=f/rel;assert sha(p)==digest;cp(p,out/'source-blobs'/digest)
 for p in f.glob('*.json'):cp(p,out/'fixtures'/name/p.name)
for stem in ['night-ablation-ps2-core-reuse-order0-20261005','night-ablation-emulator-core-reuse-v2-order0-20261005','night-ablation-ps2-core-reuse-v5-order1-20261005','night-ablation-ps2-sky-retint-order1-20261005-retry2','night-ablation-ps2-sky-retint-order0-20261005','night-ablation-emulator-sky-retint-order0-20261005']:
 for suffix in ('-launch','-evidence','-rejected-inactive'):
  p=b/(stem+suffix)
  if p.exists():tree(p,out/'runs'/p.name)
for name in ['sky-retint-root-helpers-v1','sky-retint-root-helpers-v2','sky-retint-controls-v1','sky-retint-target-abi-v1','sky-retint-target-abi-v2','sky-retint-native-root-review-v1','sky-retint-native-root-review-v2','core-reuse-native-root-review-v5','core-reuse-target-abi-v5']:
 p=b/name
 if p.exists():tree(p,out/'tools'/name)
for pattern in ['prepare-sky-retint*.py','test-sky-retint*.py','compile-sky-retint*.py','audit-sky-retint*.py','release-sky-retint*.py','sky-retint-build-native*.ps1','start-sky-retint*.ps1','wait-sky-retint*.ps1','sky-retint-host-controls*.json']:
 for p in b.glob(pattern):cp(p,out/'tools'/p.name)
for name in ['night-ablation-native-v45.log','night-ablation-native-v45.err','night-ablation-native-v46.log','night-ablation-native-v46.err','sky-retint-editor-build-v1.log','sky-retint-production-refresh-v1.log']:
 cp(b/name,out/'builds'/name)
for stem in ['night-ablation-ps2-sky-retint-order1-20261005','night-ablation-ps2-sky-retint-order1-20261005-retry1']:
 for suffix in ('-launcher.err','-launcher.log','.log','.err'):
  p=b/(stem+suffix)
  if p.exists():cp(p,out/'rejected-launches'/p.name)
 for suffix in ('-launch',):
  p=b/(stem+suffix)
  if p.exists():tree(p,out/'rejected-launches'/p.name)
(out/'binary-hashes.json').write_bytes((json.dumps(bins,indent=2)+'\n').encode());m={str(p.relative_to(out)).replace('\\','/'):dict(sha256=sha(p),bytes=p.stat().st_size)for p in sorted(out.rglob('*'))if p.is_file()and p.name!='payload-manifest.json'};(out/'payload-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());print('Initial immutable sky/reuse payloads',len(m),'source blobs',len(list((out/'source-blobs').iterdir())))
