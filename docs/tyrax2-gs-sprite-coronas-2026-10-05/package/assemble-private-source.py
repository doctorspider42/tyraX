"""Restore immutable textual source drafts into a NEW private directory only."""
from pathlib import Path
import argparse,json,hashlib
ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--hash-only-source-root',type=Path,required=True);a=ap.parse_args();S=Path(__file__).resolve().parent;O=a.out.resolve();assert not O.exists();assert O.parent.exists();assert O!=S and S not in O.parents
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
proof=json.loads((S/'preparation-proof.json').read_text())
for rel,h in proof['outputs'].items():assert sha(S/rel)==h,rel
base=json.loads((S/'evidence-text/wild-pool2-ee-physical-v2/target-source-manifest.json').read_text(encoding='utf-8-sig'))['files']
price=json.loads((S/'evidence-text/wild-gs-sprite-corona-physical-v2/target-source-manifest.json').read_text(encoding='utf-8-sig'))['files']
diag=json.loads((S/'evidence-text/wild-gs-sprite-corona-probe-physical-v1/target-source-manifest.json').read_text(encoding='utf-8-sig'))['files']
diag2=json.loads((S/'evidence-text/wild-gs-sprite-corona-probe-physical-v2/target-source-manifest.json').read_text(encoding='utf-8-sig'))['files']
O.mkdir();results={}
for name,manifest in [('baseline2-source499',base),('pricing-v6-source500',price),('diagnostic-v1-source501',diag),('diagnostic-v2-capability-source501',diag2)]:
 for rel,h in manifest.items():
  choices=[S/'source-archive/full-baseline'/rel]
  if name!='baseline2-source499':choices.insert(0,S/'source-archive/postimage'/rel)
  if name=='diagnostic-v1-source501':choices.insert(0,S/'source-archive/diagnostic-v1'/rel)
  if name=='diagnostic-v2-capability-source501':choices.insert(0,S/'source-archive/diagnostic-v2'/rel)
  src=next((p for p in choices if p.exists() and sha(p)==h),None)
  if src is None:
   assert rel=='game/terrain-procedural.splat';src=a.hash_only_source_root/rel;assert sha(src)==h
  data=src.read_bytes();dst=O/name/rel;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(data);assert sha(dst)==h
 actual={str(p.relative_to(O/name)).replace('\\','/'):sha(p) for p in (O/name).rglob('*') if p.is_file()};assert actual==manifest
 results[name]={'files':len(actual),'allExactSourceBytes':True}
record={'status':'RESTORED_PRIVATE_TEXTUAL_SOURCE_ONLY_NOT_PRODUCT','sourceArchiveProofSha256':sha(S/'preparation-proof.json'),'restored':results,'hashOnlyBinarySourceRoot':str(a.hash_only_source_root),'diagnosticV2':'separately restored capability source, actual SPRITE output unproved','buildCacheDeviceRepositoryMutation':False,'gainClaim':False}
(O/'restore-proof.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');print(json.dumps(record,indent=2))
