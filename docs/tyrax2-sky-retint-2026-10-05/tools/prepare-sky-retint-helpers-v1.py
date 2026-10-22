from pathlib import Path
import shutil,json,hashlib,re
b=Path('F:/Projects/tyrax2-lab-20261001');h=b/'sky-retint-root-helpers-v1';c=b/'sky-retint-controls-v1';f=b/'sky-retint-physical-v1'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for src,out in [('core-reuse-root-helpers-v5',h),('core-reuse-controls-v5',c)]:
 assert not out.exists();shutil.copytree(b/src,out,ignore=shutil.ignore_patterns('__pycache__'))
 for p in out.rglob('*'):
  if p.is_file() and p.suffix in ('.py','.ps1','.json','.md'):
   s=p.read_text(encoding='utf-8-sig').replace('core-reuse-census-physical-v5','sky-retint-physical-v1').replace('core-reuse-root-helpers-v5','sky-retint-root-helpers-v1').replace('core-reuse-controls-v5','sky-retint-controls-v1')
   s=s.replace('choices=(18,)','choices=(19,)').replace('choices=[18]','choices=[19]').replace('kind==18','kind==19') if p.parent==h or p.name=='analyze-night-cli.py' else s
   p.write_bytes(s.encode())
p=c/'analyze-night.py';s=p.read_text().replace('16,17,18)','16,17,18,19)')
marker="  r.update(producerObserverFlags=flags"
block='''  if kind==19:
   for p,o in gs:
    v=fm[(p,o,0)];need(n(v,'calls')>0,'sky retint calls')
    need(n(v,'units3')==n(v,'units4')==0,'sky retint parity/fallback')
    need(n(v,'units1')==(n(v,'calls')if flags[p]else 0),'sky retint activation')
    need(n(v,'units2')==(1152*n(v,'calls')if flags[p]else 0),'sky retint full array oracle')
    for st in range(5):
     row=fm[(p,o,st)];need(n(row,'timedCalls')==n(row,'reads')==n(row,'ticks')==0,'sky retint no scoped clocks')
     if st>=2 or (st==1 and not flags[p]):need(all(n(row,k)==0 for k in('calls','units1','units2','units3','units4')),'sky unused/off stages')
    v=fm[(p,o,1)]
    if flags[p]:need(n(v,'calls')==n(v,'units1')==n(v,'units2')==n(fm[(p,o,0)],'calls') and n(v,'units3')==n(v,'units4')==0,'sky geometry/color stamps')
   r['candidateActivationSparseOnly']=True
'''
assert marker in s;s=s.replace(marker,block+marker);p.write_bytes(s.encode())
shutil.copyfile(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
a=json.loads((h/'host-authority.json').read_text());a['sourcePins']={str(p):sha(p)for p in [c/'analyze-loop.py',c/'analyze-night.py',c/'analyze-night-cli.py',c/'corona_controls.py',c/'source-controls/night_plan.hpp',c/'source-controls/stapip_vu1_shared_defines.h']};a['pricingSourceManifestSha256']=sha(f/'target-source-manifest.json');(h/'host-authority.json').write_bytes((json.dumps(a,indent=2)+'\n').encode())
(h/'source-review.md').write_bytes(b'# Exact sky colors-only candidate\n\nRuntime RGB changes retain positions, UVs, geometry stamps and bounds; initialization and scene transitions retain the original full builder. Cold checks compare all arrays against an independent original full build and leave its equal output live. No new clock reads; cold activation is sparse, common code footprint is unpriced. Production promotion requires physical parity, repeatable prices and warm visual/lifecycle qualification.\n')
for typ in ('start','wait'):
 p=b/f'{typ}-core-reuse-physical-root-v5.ps1';s=p.read_text().replace('core-reuse-root-helpers-v5','sky-retint-root-helpers-v1').replace('core-reuse-census-physical-v5','sky-retint-physical-v1').replace('--kind 18','--kind 19');(b/f'{typ}-sky-retint-physical-root-v1.ps1').write_bytes(s.encode())
p=b/'release-core-reuse-v5.py';s=p.read_text().replace('core-reuse-census-physical-v5','sky-retint-physical-v1').replace('core-reuse-root-helpers-v5','sky-retint-root-helpers-v1').replace('core-reuse-native-root-review-v5','sky-retint-native-root-review-v1').replace('core-reuse-host-controls-v5','sky-retint-host-controls-v1').replace('ROOT_RELEASE_PRIVATE_CLIP_PLANE_SPECIALIZE','ROOT_RELEASE_PRIVATE_SKY_RETINT');(b/'release-sky-retint-v1.py').write_bytes(s.encode())
print('prepared kind19 dedicated helpers; controls pending')
