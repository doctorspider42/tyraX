from pathlib import Path
import shutil,json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'player-light-receivers-physical-v1';c=b/'player-light-receivers-controls-v1';h=b/'player-light-receivers-root-helpers-v1';assert not c.exists()and not h.exists();shutil.copytree(b/'wild-gs-sprite-corona-controls-v2',c);shutil.copytree(b/'corona24-pricing-root-helpers-v1',h)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for p in h.iterdir():
 if p.suffix in('.py','.ps1','.md'):
  s=p.read_text().replace('corona24-pricing-physical-v1','player-light-receivers-physical-v1').replace('corona24-pricing-root-helpers-v1','player-light-receivers-root-helpers-v1').replace('wild-gs-sprite-corona-controls-v2','player-light-receivers-controls-v1')
  s=s.replace('choices=(9,)','choices=(10,11)').replace('a.kind==9','a.kind in (10,11)').replace("d['kind']==9","d['kind'] in (10,11)").replace('kind==9 and order','kind in (10,11) and order')
  p.write_bytes(s.encode())
shutil.copy2(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
p=c/'analyze-night.py';s=p.read_text().replace('in(5,6,7,8,9)','in(5,6,7,8,9,10,11)').replace('in(3,5,6,7,8,9)','in(3,5,6,7,8,9,10,11)').replace('in(7,8,9)','in(7,8,9,10,11)').replace('in(8,9)','in(8,9,10,11)')
s=s.replace('if kind==3 else [0,0,0]','if kind==3 else [int((p==1)!=(order==1)) for p in range(3)] if kind==11 else [0,0,0]')
a=' # Native dialect9: full inherited sampler plus actual typed cold request accounting.';assert s.count(a)==1
s=s.replace(a,''' if kind in (10,11):
  receiverPhases=loop.unique(loop.rows(stdout,'NIGHTRECEIVERPHASE'),'phase',range(3))
  receiverRows=loop.rows(stdout,'NIGHTRECEIVERGATES');need(len(receiverRows)==6,'six receiver gates');receiverMap={}
  for p,row in receiverPhases.items():
   expected=(kind-9) if ((p==1)!=(order==1)) else 0
   need(set(row)=={'phase','mode'} and n(row,'mode')==expected,'receiver phase mode')
  for row in receiverRows:
   need(set(row)==set('phase offset mode bagAllowed bagRejected sampleAllowed sampleRejected'.split()),'receiver schema')
   key=(n(row,'phase'),n(row,'offset'));need(key not in receiverMap and key in gs,'receiver ownership');receiverMap[key]=row
   need(n(row,'mode')==n(receiverPhases[key[0]],'mode'),'receiver actual mode')
   need(all(n(row,k)<=0xffffffff for k in row),'receiver unsigned bounds')
   if n(row,'mode')==0:need(n(row,'bagRejected')==n(row,'sampleRejected')==0,'full receiver arm rejection')
   else:need(n(row,'bagAllowed')>0 and n(row,'bagRejected')>0,'restricted arm must reach both classes')
  need(set(receiverMap)==set(gs),'receiver identities')
  r.update(receiverModes=[n(receiverPhases[p],'mode')for p in range(3)],receiverSparseWindows=receiverMap if False else {f'{p}:{o}':row for(p,o),row in receiverMap.items()},receiverWholeTimedActivationKnown=False,qualityTradeoff=True)
'''+a)
p.write_bytes(s.encode())
p=c/'analyze-night-cli.py';s=p.read_text().replace('choices=[0,1,2,3,5,6,7,8,9]','choices=[10,11]');p.write_bytes(s.encode())
# Raster observer: all effects retained except explicitly selected pool cut.
p=h/'qualify-emulator.py';s=p.read_text();a="assert len(coronaRows)==1 and coronaRows[0][0]=='1' and int(coronaRows[0][1])==int((current==1)!=(d['order']==1))";assert a in s;s=s.replace(a,"assert len(coronaRows)==1 and coronaRows[0]==('0','0'); rr=re.findall(r'LOG: NIGHTRECEIVERPHASE phase='+str(current)+r' mode=(\\d+)',after);assert len(rr)==1 and int(rr[0])==((d['kind']-9) if ((current==1)!=(d['order']==1)) else 0)");p.write_bytes(s.encode())
# Pin the actual expanded host dialect, never reuse old authorities.
files=['analyze-loop.py','analyze-night.py','analyze-night-cli.py','corona_controls.py','source-controls/night_plan.hpp','source-controls/stapip_vu1_shared_defines.h']
(h/'host-authority.json').write_bytes((json.dumps(dict(sourcePins={str(c/n):sha(c/n)for n in files},pricingSourceManifestSha256=sha(f/'target-source-manifest.json')),indent=2)+'\n').encode())
(h/'source-review.md').write_bytes(b'Private receiver experiment draft. Two same-ELF comparisons: full vs player/driven vehicle; full vs same receivers plus scene pool cut. Existing loop sampler unchanged. No sprite arm. Native and path review required before runtime release.\n')
(f/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(f/'target-source-manifest.json'),sourceFiles=501,privateReceiverExperiment=True),indent=2)+'\n').encode())
print(c,h)
