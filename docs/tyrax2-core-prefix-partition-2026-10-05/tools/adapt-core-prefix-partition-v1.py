from pathlib import Path
import shutil,json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'core-prefix-partition-physical-v1';h=b/'core-prefix-root-helpers-v1';c=b/'core-prefix-controls-v1';assert not h.exists()and not c.exists();shutil.copytree(b/'night-producer-root-helpers-v1',h);shutil.copytree(b/'night-producer-controls-v1',c);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for p in h.iterdir():
 if p.suffix in('.py','.ps1','.md'):
  s=p.read_text(encoding='utf8').replace('night-producer-probe-physical-v2',f.name).replace('night-producer-root-helpers-v1',h.name).replace('night-producer-controls-v1',c.name).replace('choices=(13,)','choices=(14,)').replace('a.kind==13','a.kind==14').replace("d['kind']==13","d['kind']==14").replace('kind==13 and order','kind==14 and order');p.write_bytes(s.encode())
shutil.copy2(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
p=c/'analyze-night.py';s=p.read_text(encoding='utf8').replace(',12,13)',',12,13,14)').replace('if kind==13:','if kind in(13,14):').replace("if kind==13 else 'Far", "if kind in(13,14) else 'Far")
a="  r.update(producerObserverFlags=flags,";assert a in s;s=s.replace(a,'''  if kind==14:
   for p,o in gs:
    a,b,c=[fm[(p,o,st)]for st in range(3)];need(n(b,'calls')==n(c,'calls')<=n(a,'calls'),'Core prefix cold entry partition');need(n(b,'units1')==n(c,'units1')<=n(a,'units1'),'Core prefix cold vertices');need(n(a,'units2')+n(a,'units3')<=n(a,'calls')and n(a,'units4')==0,'Core early returns');need(n(b,'units2')+n(b,'units4')<=n(b,'calls')and n(b,'units3')<=n(b,'calls'),'Core lighting/proxy bounds');need(all(n(c,k)==0 for k in('units2','units3','units4')),'Core tail reserved units')
    if o==1155:need(n(b,'timedCalls')==n(c,'timedCalls')<=n(a,'timedCalls'),'Core timed prefix partition')
    for st in(3,4):need(all(n(fm[(p,o,st)],k)==0 for k in('calls','units1','units2','units3','units4','timedCalls','reads','ticks')),'unused Core stages zero')
'''+a);p.write_bytes(s.encode())
p=c/'analyze-night-cli.py';p.write_bytes(p.read_text(encoding='utf8').replace('choices=[13]','choices=[14]').encode())
files=['analyze-loop.py','analyze-night.py','analyze-night-cli.py','corona_controls.py','source-controls/night_plan.hpp','source-controls/stapip_vu1_shared_defines.h'];(h/'host-authority.json').write_bytes((json.dumps(dict(sourcePins={str(c/n):sha(c/n)for n in files},pricingSourceManifestSha256=sha(f/'target-source-manifest.json')),indent=2)+'\n').encode())
(h/'source-review.md').write_bytes(b'Private kind14 Core prefix partition, three disjoint ranges in one linked ELF. Head/bounds/packager includes early returns; texture/program/light/BLSS/fog; objectData plus route predicates ending before baked replay. Original calls, waits, dispatch order and all game sources retained. Scope clocks only800..1119; cold counts750/1155. Existing waits/preemption remain inside source ranges; not pure EE bills. Common observer code/stack/data unpriced. Stages3/4 reservedzero. No cross-version subtraction from V8/V9/V10.\n')
for name in('compile-night-producer-abi-root-v1.py','audit-night-producer-native-root-v1.py'):
 s=(b/name).read_text(encoding='utf8').replace('night-producer-probe-physical-v2',f.name).replace('PASS_ROOT_PRODUCER_OBSERVER_ACTUAL_NATIVE_501_SOURCE_LINKED_ABI_ASSETS','PASS_ROOT_CORE_PARTITION_ACTUAL_NATIVE_501_SOURCE_LINKED_ABI_ASSETS');(b/name.replace('night-producer','core-prefix')).write_bytes(s.encode())
for name in('start','wait'):
 s=(b/f'{name}-night-producer-physical-root-v1.ps1').read_text(encoding='utf8').replace('night-producer-root-helpers-v1',h.name).replace('--kind 13','--kind 14');(b/f'{name}-core-prefix-physical-root-v1.ps1').write_bytes(s.encode())
s=(b/'release-night-producer-v1.py').read_text(encoding='utf8').replace('night-producer-probe-physical-v2',f.name).replace('night-producer-root-helpers-v1',h.name).replace('night-producer-native-root-review-v1-retry1','core-prefix-native-root-review-v1').replace('night-producer-host-controls-v1.json','core-prefix-host-controls-v1.json').replace('ROOT_RELEASE_PRIVATE_PRODUCER_OBSERVER','ROOT_RELEASE_PRIVATE_CORE_PREFIX_PARTITION');(b/'release-core-prefix-v1.py').write_bytes(s.encode())
print(h,c)
