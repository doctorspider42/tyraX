from pathlib import Path
import shutil,json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'night-producer-probe-physical-v2';h=b/'night-producer-root-helpers-v1';c=b/'night-producer-controls-v1';assert not h.exists()and not c.exists();shutil.copytree(b/'light-pick-far-root-helpers-v1',h);shutil.copytree(b/'light-pick-far-controls-v1',c)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for p in h.iterdir():
 if p.suffix in('.py','.ps1','.md'):
  s=p.read_text(encoding='utf8').replace('light-pick-far-physical-v1',f.name).replace('light-pick-far-root-helpers-v1',h.name).replace('light-pick-far-controls-v1',c.name).replace('choices=(12,)','choices=(13,)').replace('a.kind==12','a.kind==13').replace("d['kind']==12","d['kind']==13").replace('kind==12 and order','kind==13 and order').replace('NIGHTFARPICKPHASE','NIGHTPRODPHASE').replace('farPickEnabled','producerObserverEnabled');p.write_bytes(s.encode())
shutil.copy2(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
p=c/'analyze-night.py';s=p.read_text(encoding='utf8').replace(',9,12)',',9,12,13)');start=s.index(' if kind==12:');end=s.index(' # Native dialect9:',start)
s=s[:start]+''' if kind==13:
  fp=loop.unique(loop.rows(stdout,'NIGHTPRODPHASE'),'phase',range(3));flags=[int((p==1)!=(order==1))for p in range(3)];fg=loop.rows(stdout,'NIGHTPROD');need(len(fg)==30,'30 producer rows');fm={}
  for p,row in fp.items():need(set(row)=={'phase','enabled'}and n(row,'enabled')==flags[p],'producer observer phase')
  for row in fg:
   need(set(row)==set('phase offset stage enabled calls units1 units2 units3 units4 timedCalls reads ticks'.split()),'producer schema');p,o,st=[n(row,k)for k in('phase','offset','stage')];key=(p,o,st);need((p,o)in gs and st in range(5)and key not in fm,'producer ownership');fm[key]=row
   need(n(row,'enabled')==flags[p],'actual observer state');need(all(n(row,k)<=0xffffffff for k in row if k!='ticks'),'producer uint32');tc,rd,t=[n(row,k)for k in('timedCalls','reads','ticks')];need(rd==2*tc and t<=tc*0xffffffff,'scope clocks partition')
   if not flags[p]or o==750:need(tc==rd==t==0,'no early/off producer timing')
  need(set(fm)=={(p,o,st)for p in range(3)for o in(750,1155)for st in range(5)},'complete producer ownership')
  need(any(n(row,'timedCalls')>0 for key,row in fm.items()if key[1]==1155),'observer actual activation')
  r.update(producerObserverFlags=flags,producerColdWindows={f'{p}:{o}:{st}':row for(p,o,st),row in fm.items()},producerScopesIncludePreemption=True,producerTimedWindowLoops=320,commonProducerBranchesPriced=False)
'''+s[end:];s=s.replace("'Far light broad-phase candidate elapsed contrast' if kind==12", "'Producer observer enabled net tax contrast' if kind==13 else 'Far light broad-phase candidate elapsed contrast' if kind==12");p.write_bytes(s.encode())
p=c/'analyze-night-cli.py';p.write_bytes(p.read_text(encoding='utf8').replace('choices=[12]','choices=[13]').encode())
files=['analyze-loop.py','analyze-night.py','analyze-night-cli.py','corona_controls.py','source-controls/night_plan.hpp','source-controls/stapip_vu1_shared_defines.h'];(h/'host-authority.json').write_bytes((json.dumps(dict(sourcePins={str(c/n):sha(c/n)for n in files},pricingSourceManifestSha256=sha(f/'target-source-manifest.json')),indent=2)+'\n').encode())
(h/'source-review.md').write_bytes(b'Private kind13 observer only; all source effect calculations retained. Five disjoint scopes: receiver production, hull sampling/fold, beam scratch, corona commit, cone commit. Render submissions excluded. Sparse activation at750/1155; Count reads and accumulations only800..1119 when enabled. Same-ELF both observer orders price net induced tax; common inactive branches/code footprint unpriced. Scope times retain preemption and measurement instructions, not pure EE exclusive bills.\n')
for n in('compile-light-pick-far-abi-root-v1.py','audit-light-pick-far-native-root-v1.py'):
 s=(b/n).read_text(encoding='utf8').replace('light-pick-far-physical-v1',f.name).replace('PASS_ROOT_FAR_PICK_ACTUAL_NATIVE_501_SOURCE_LINKED_ABI_ASSETS','PASS_ROOT_PRODUCER_OBSERVER_ACTUAL_NATIVE_501_SOURCE_LINKED_ABI_ASSETS');(b/n.replace('light-pick-far','night-producer')).write_bytes(s.encode())
for n in('start','wait'):
 s=(b/f'{n}-light-pick-far-physical-root-v1.ps1').read_text(encoding='utf8').replace('light-pick-far-root-helpers-v1',h.name).replace('--kind 12','--kind 13');(b/f'{n}-night-producer-physical-root-v1.ps1').write_bytes(s.encode())
print(h,c)
