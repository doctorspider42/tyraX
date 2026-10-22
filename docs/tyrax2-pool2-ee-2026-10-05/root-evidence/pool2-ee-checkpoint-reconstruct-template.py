"""Restore exact private Pool2 EE sources from a verified prior499 source-only base."""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,shutil
ROOT=Path(__file__).resolve().parent
def unique(pairs):
 d={}
 for k,v in pairs:
  if k in d:raise ValueError('Duplicate JSON key: '+k)
  d[k]=v
 return d
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'),object_pairs_hook=unique)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def relative(s):
 p=PurePosixPath(s)
 if not s or p.is_absolute() or any(x in('', '.', '..')for x in p.parts) or '\\'in s or ':'in s or p.as_posix()!=s:raise ValueError('Unsafe/noncanonical path '+s)
 return Path(*p.parts)
def files(root):
 out={}
 for p in root.rglob('*'):
  if p.is_symlink():raise ValueError('Symlink '+str(p))
  if p.is_file():
   k=p.relative_to(root).as_posix();relative(k)
   if k.casefold()in {s.casefold()for s in out}:raise ValueError('Case collision '+k)
   out[k]=sha(p)
 return out
def verify(root,want):
 for s,h in want.items():relative(s)
 if len({s.casefold()for s in want})!=len(want):raise ValueError('Case-colliding manifest')
 if files(root)!=want:raise ValueError('Source directory has missing/changed/extra files')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--mode',choices=('pricing','probe'),required=True);a=p.parse_args()
 authority=load(ROOT/'metadata/reconstruction-authority.json')
 for name,h in authority['manifests'].items():
  if sha(ROOT/relative(name))!=h:raise ValueError('Package manifest drift '+name)
 base=a.base.resolve(strict=True);want=load(ROOT/'metadata/base499-source-manifest.json')['files'];verify(base,want)
 out=a.out.absolute()
 if out.exists():raise ValueError('NEW output required')
 selected=['pricing']+(['probe']if a.mode=='probe'else[])
 # Validate every overlay and final manifest before creating any output.
 final=dict(want)
 for mode in selected:
  changes=authority['overlays'][mode]
  verify(ROOT/'postimages'/mode,{k:v['sha256']for k,v in changes.items()})
  for k,v in changes.items():
   if (k not in final)!=v['new']:raise ValueError('Incorrect new-file declaration '+k)
   if not v['new']and final[k]!=v['before']:raise ValueError('Incorrect overlay preimage '+k)
   final[k]=v['sha256']
 target=load(ROOT/f'metadata/{a.mode}/target-source-manifest.json')['files']
 if final!=target:raise ValueError('Overlay does not exactly produce target manifest')
 out.mkdir(parents=True,exist_ok=False)
 for name in want:
  dest=out/relative(name);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/relative(name),dest)
 for mode in selected:
  for name in authority['overlays'][mode]:
   dest=out/relative(name);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'postimages'/mode/relative(name),dest)
 verify(out,target)
 print(json.dumps({'status':'PASS_EXACT_SOURCE_ONLY_RESTORATION','mode':a.mode,'files':len(target),'out':str(out),'authoredResAndRuntimeAssetsRestored':False},indent=2))
if __name__=='__main__':main()
