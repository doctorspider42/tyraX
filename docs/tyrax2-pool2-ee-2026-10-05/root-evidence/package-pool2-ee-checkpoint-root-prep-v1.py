"""ROOT-only packaging after twelve-case closure; this design was not executed."""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,shutil
LAB=Path(__file__).resolve().parent
REPO=Path('F:/Projects/tyra-editor')
OLD=REPO/'docs/tyrax2-pool2-source-2026-10-05'
PRICING=LAB/'wild-pool2-ee-physical-v2'
PROBE=LAB/'wild-pool2-ee-probe-physical-v1'
EXPECTED_PRICING='769f1403389da4d3683a17806fce1b75b381a4cf4f8229ee4d3306a44ca806de'
TEXT={'.json','.jsonl','.py','.ps1','.hpp','.h','.cpp','.inc','.txt','.md','.log','.err','.cfg','.ini','.vclpp','.i','.sh','.s','.asm','.reference','.run'}
def unique(pairs):
 d={}
 for k,v in pairs:
  if k in d:raise ValueError('Duplicate JSON key '+k)
  d[k]=v
 return d
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'),object_pairs_hook=unique)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def safe(name):
 p=PurePosixPath(name)
 if p.is_absolute()or'..'in p.parts or'\\'in name or':'in name or p.as_posix()!=name:raise ValueError('Unsafe '+name)
 return Path(*p.parts)
def manifest(p):
 m=load(p);d=m['files']
 if not m.get('frozen'):raise ValueError('Unfrozen manifest '+str(p))
 for n in d:safe(n)
 if len({n.casefold()for n in d})!=len(d):raise ValueError('Case collisions')
 return d
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--actual-output-report',type=Path,action='append',required=True);ap.add_argument('--tag',default='20261005-v1');a=ap.parse_args()
 out=a.out.absolute()
 if out.exists():raise ValueError('NEW package output required')
 basepath=OLD/'metadata/pricing/target-source-manifest.json';pricepath=PRICING/'target-source-manifest.json';probepath=PROBE/'target-source-manifest.json'
 if sha(pricepath)!=EXPECTED_PRICING:raise ValueError('Pricing manifest changed')
 base=manifest(basepath);pricing=manifest(pricepath);probe=manifest(probepath)
 if (len(base),len(pricing),len(probe))!=(499,499,500):raise ValueError('Source count mismatch')
 deltas={}
 for mode,before,after,root in [('pricing',base,pricing,PRICING),('probe',pricing,probe,PROBE)]:
  if set(before)-set(after):raise ValueError('Undeclared deleted source')
  changed={n:{'before':before.get(n),'sha256':h,'new':n not in before}for n,h in after.items()if before.get(n)!=h}
  if len(changed)!=(5 if mode=='pricing'else 2):raise ValueError('Unexpected '+mode+' delta')
  for n,h in after.items():
   p=root/safe(n)
   if p.is_symlink()or sha(p)!=h:raise ValueError('Current source drift '+str(p))
  deltas[mode]=changed
 completed=[]
 for n in(96,192):
  for arm in(0,1):
   for repeat in(1,2,3):
    stem=f'pool2-ee-probe-{("a"if n==96 else"b")}{n}-arm{arm}-repeat{repeat}-{a.tag}-launch';p=LAB/stem/'completed-probe-proof.json';r=load(p)
    if not r['status'].startswith('PASS_')or(r['arm'],r['vertices'],r['repeats'])!=(arm,n,repeat):raise ValueError('Missing/rejected twelve-case closure '+stem)
    completed.append(LAB/stem)
 has_closure=False
 for p in a.actual_output_report:
  r=load(p)
  if r.get('status')=='ROOT_CLOSED_TWELVE_EE_LAZY_EPOCH_CAPTURES_SIX_PACKED_OUTPUT_PAIRS':
   if sha(p)!='a28f4a1d3dbd1098724ad7ca385f095f1d3b9826a36ef21ae49414b814dcb823' or len(r['captures'])!=12 or len(r['pairs'])!=6 or not r['finalEpochRGBAValidated']:raise ValueError('Incorrect root actual-output closure')
   has_closure=True
  elif not str(r.get('status','')).startswith('PASS_'):raise ValueError('Actual output report not accepted '+str(p))
 if not has_closure:raise ValueError('Pinned root twelve-capture/six-pair closure report required')
 out.mkdir(parents=True,exist_ok=False);copies=[];destinations=set()
 def copy(src,name,png=False):
  target=out/safe(name)
  if name.casefold()in destinations:raise ValueError('Duplicate destination '+name)
  if src.is_symlink()or not src.is_file():raise ValueError('Not ordinary file '+str(src))
  b=src.read_bytes()
  if png:
   if b[:8]!=b'\x89PNG\r\n\x1a\n':raise ValueError('Not PNG')
  elif b.startswith((b'\x7fELF',b'MZ',b'PK\x03\x04'))or b'\x00'in b:raise ValueError('Forbidden binary '+str(src))
  target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,target)
  if sha(src)!=sha(target):raise ValueError('Copy mismatch')
  destinations.add(name.casefold());copies.append({'original':str(src),'preserved':name,'sha256':sha(src)})
 def tree(src,name):
  if not src.is_dir():raise ValueError('Missing evidence directory '+str(src))
  for p in sorted(src.rglob('*')):
   if p.is_file()and '__pycache__'not in p.parts and (p.suffix.lower()in TEXT or p.name=='tyra'):copy(p,(Path(name)/p.relative_to(src)).as_posix())
 copy(basepath,'metadata/base499-source-manifest.json');copy(OLD/'metadata/base-497-source-manifest.json','metadata/historical-base497-source-manifest.json')
 tree(OLD/'controls','controls/prior-pool2')
 for p in sorted((LAB/'wild-pool2-ee-physical-v1').iterdir()):
  if p.is_file()and p.suffix.lower()in TEXT:copy(p,'negative/pricing-v1-asset-gate/'+p.name)
 for mode,root in [('pricing',PRICING),('probe',PROBE)]:
  copy(root/'target-source-manifest.json',f'metadata/{mode}/target-source-manifest.json')
  for n in deltas[mode]:copy(root/safe(n),'postimages/'+mode+'/'+n)
  for p in sorted(root.iterdir()):
   if p.is_file()and p.suffix.lower()in TEXT and p.name!='target-source-manifest.json':copy(p,f'metadata/{mode}/{p.name}')
 for prefix in ['wild-pool2-ee-producer-host-v1','wild-pool2-ee-producer-host-v2','wild-pool2-ee-probe-design-v1','wild-pool2-ee-probe-runtime-v1','wild-pool2-ee-root-v1','wild-pool2-ee-root-v2','wild-pool2-ee-root-v3','wild-pool2-ee-review-v1','wild-pool2-ee-operator-review-v1','wild-pool2-ee-native-root-review-v1','wild-pool2-ee-native-root-review-v2','wild-pool2-ee-physical-pair-root-review-v1','wild-pool2-ee-probe-native-root-review-v1','wild-pool2-ee-probe-independent-review-v1','wild-pool2-ee-target-abi-v1','wild-pool2-ee-probe-target-abi-v1','wild-pool2-ee-static-matrix-root-closure-v1']:
  tree(LAB/prefix,'laboratory/'+prefix)
 for p in sorted(LAB.iterdir()):
  if p.is_file()and p.suffix.lower()in TEXT and ('pool2-ee'in p.name):copy(p,'root-evidence/'+p.name)
 for order in(0,1):
  for environment in('ps2','emulator'):
   stem=f'night-ablation-{environment}-pool2-ee-order{order}-20261005'
   if environment=='emulator'and order==0:stem='night-ablation-emulator-pool2-ee-order0-attempt2-20261005'
   for suffix in('evidence','launch'):tree(LAB/(stem+'-'+suffix),'runs/'+stem+'/'+suffix)
   if environment=='emulator':
    for phase in range(3):copy(LAB/(stem+'-launch')/f'phase-{phase}.png',f'images/order{order}-phase{phase}.png',True)
 tree(LAB/'night-ablation-emulator-pool2-ee-order0-20261005-launch','negative/initial-sdl-attempt-launch')
 tree(LAB/'night-ablation-emulator-pool2-ee-order0-20261005-evidence','negative/initial-sdl-attempt-evidence')
 for directory in completed:tree(directory,'actual-output/'+directory.name)
 for i,p in enumerate(a.actual_output_report):copy(p,f'actual-output/reports/{i:02d}-{p.name}')
 copy(LAB/'pool2-ee-checkpoint-reconstruct-template.py','reconstruct.py');copy(LAB/'pool2-ee-checkpoint-README-proposal.md','README.md');copy(Path(__file__),'root-helpers/package-checkpoint.py')
 def save(name,record):
  target=out/safe(name);target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(record,indent=2)+'\n')
 authority={'baseFiles':499,'pricingFiles':499,'probeFiles':500,'overlays':deltas,'manifests':{n:sha(out/safe(n))for n in ['metadata/base499-source-manifest.json','metadata/pricing/target-source-manifest.json','metadata/probe/target-source-manifest.json']},'resourcesShipped':False,'authoredResRequiredForNativeAssetConversion':True}
 save('metadata/reconstruction-authority.json',authority);save('metadata/preservation-map.json',{'status':'BYTE_EXACT_TEXT_AND_SIX_APPROVED_IMAGES_NO_BUILD_BINARIES','copies':copies})
 save('SHA256.json',{p.relative_to(out).as_posix():sha(p)for p in sorted(out.rglob('*'))if p.is_file()and p.name!='SHA256.json'})
 print(json.dumps({'status':'PACKAGED_ONLY_RESTORATION_MUST_BE_EXERCISED_SEPARATELY','out':str(out),'preservedFiles':len(copies),'actualOutputCases':len(completed),'images':6},indent=2))
if __name__=='__main__':main()
