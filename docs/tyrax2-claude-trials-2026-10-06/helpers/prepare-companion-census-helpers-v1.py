from pathlib import Path
import shutil,json,hashlib,re
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'companion-census-physical-v1';c=b/'companion-census-controls-v1';h=b/'companion-census-root-helpers-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for old,d in [('object-route-controls-v1',c),('object-route-root-helpers-v1',h)]:
 shutil.copytree(b/old,d,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','synthetic'))
 for p in d.rglob('*'):
  if p.is_file()and p.suffix in('.py','.ps1','.json','.md'):
   s=p.read_text(encoding='utf8').replace('object-route','companion-census')
   if p.parent==h or p.name=='analyze-night-cli.py':s=re.sub(r'\b22\b','24',s)
   p.write_bytes(s.encode())
p=c/'analyze-night.py';s=p.read_text(encoding='utf8').replace(',21,22)',',21,22,24)');start=s.index('  if kind==22:');end=s.index('  r.update(producerObserverFlags',start)
s=s[:start]+s[end:]
start=s.index(" r['interpretation']=")
new=''' if kind==24:
  need(all(n(row,k)==0 for row in fm.values()for k in ('calls','units1','units2','units3','units4','timedCalls','reads','ticks')),'no producer scope/counter clocks in activation census')
  contexts=loop.rows(stdout,'NIGHTCONTEXT');need(len(contexts)==6,'six actual mood contexts');cm={}
  schema=set('phase offset scene night video display depth width height refreshMilliHz requested dtBits clockBits rasterWidth rasterHeight scaleX scaleY ilChoice ilProbing ilBlock ilFrame ilAccepted ilPipelined frameYield'.split())
  for row in contexts:
   need(set(row)==schema,'exact census context schema');key=n(row,'phase'),n(row,'offset');need(key in gs and key not in cm,'context identity');cm[key]=row;need(n(row,'night')==flags[key[0]],'actual phase mood')
   for k in ('dtBits','clockBits'):need(loop.re.fullmatch('[0-9a-fA-F]{8}',row[k]) is not None,'finite encoded context bits')
   need(n(row,'scene')==0,'fixed scene')
  need(set(cm)==set(gs),'complete actual contexts')
  census=loop.rows(stdout,'CENSUS');need(len(census)==30,'30 role summaries');cs={}
  for row in census:
   need(set(row)==set('phase offset night role'.split())|{f'f{i}' for i in range(25)},'exact census schema');p,o,role=[n(row,k)for k in ('phase','offset','role')];key=p,o,role;need((p,o)in gs and role in range(5) and key not in cs,'census identity');cs[key]=row
   need(n(row,'night')==flags[p],'census phase mood');v=[n(row,f'f{i}')for i in range(25)];need(all(x<=0xffffffffffffffff for x in v),'uint64 census');need(v[1]+v[2]==v[0],'entered accepted/culled partition');need(all(v[i]<=v[0] for i in range(3,10)),'adjacent identity subsets');need(v[9]<=min(v[3],v[5],v[6],v[7],v[8]),'eligible exact key subset');need(v[20]<=min(v[1],v[9]),'accepted adjacent subset');need(v[10]+v[11]+v[12]<=v[0],'bbox cache access subset')
  need(set(cs)=={(p,o,r)for p,o in gs for r in range(5)},'all cold producer roles');need(all(sum(n(cs[p,o,r],'f0')for r in range(5))>0 for p,o in gs),'actual census activation')
  r.update(companionCensus=cs and {f'{p}:{o}:{rr}':row for(p,o,rr),row in cs.items()},phaseNightFlags=flags,coldCounterFrames=[750,1155],newCountClockReads=0,commonCensusBranchFootprintUnpriced=True,observerTaxMeasured=False)
'''
s=s[:start]+new+s[start:];s=s.replace("r['interpretation']='Producer observer", "r['interpretation']='Day/night activation census; no observer-tax contrast' if kind==24 else 'Producer observer");p.write_bytes(s.encode())
shutil.copyfile(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
p=c/'analyze-loop.py';s=p.read_text(encoding='utf8').replace('def analyze(stdout,artifact,order,environment,samplers):','def analyze(stdout,artifact,order,environment,samplers,phase_nights=None):')
s=s.replace("  for k in ('night','video','display','depth','width','height','refreshMilliHz','requested'):need(number(row,k)==number(config[0],k),'sparse video/mood drift '+k)","  need(key[0] in range(3),'context phase range');need(phase_nights is None or len(phase_nights)==3 and all(x in (0,1)for x in phase_nights),'phase mood contract')\n  for k in ('night','video','display','depth','width','height','refreshMilliHz','requested'):need(number(row,k)==(phase_nights[key[0]]if k=='night'and phase_nights is not None else number(config[0],k)),'sparse video/mood drift '+k)")
p.write_bytes(s.encode());p=c/'analyze-night.py';s=p.read_text(encoding='utf8').replace('r=loop.analyze(stdout,artifact,order,environment,[x[1] for x in specs])',"r=loop.analyze(stdout,artifact,order,environment,[x[1] for x in specs],[int((p==1)!=(order==1))for p in range(3)]if kind==24 else None)");p.write_bytes(s.encode())

# Authority remains deliberately unreleased until manifest/root-native proof exists.
a=json.loads((h/'host-authority.json').read_text());a['sourcePins']={str(p):sha(p)for p in [c/'analyze-loop.py',c/'analyze-night.py',c/'analyze-night-cli.py',c/'corona_controls.py',c/'source-controls/night_plan.hpp',c/'source-controls/stapip_vu1_shared_defines.h']};mf=f/'target-source-manifest.json';a['pricingSourceManifestSha256']=sha(mf if mf.exists()else f/'draft-source-manifest.json');a['draftUnreleased']=not mf.exists();(h/'host-authority.json').write_bytes((json.dumps(a,indent=2)+'\n').encode())
(h/'source-review.md').write_bytes(b'# Kind24 draft census\n\nDay/night/day or night/day/night, full sampler in every phase. Sparse counts only750/1155; no added Count clocks. Common branch footprint unpriced, day/night is not observer tax. Roles main/AO/emission/env/other annotate actual submitted calls. Core identity captures entered bags, accepted/culled partitions and exact numerical geometry/model/view/frustum/partition identity; accepted adjacent separately. Bbox and retained/baked actual event seams. Draft source is not frozen; helpers unreleased.\n')
(h/'README.md').write_bytes((h/'source-review.md').read_bytes());(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode())
# Prepare only: root later binds reviewed frozen source + actual native proof.
s=(b/'release-object-route-v1.py').read_text(encoding='utf8').replace('object-route','companion-census').replace('OBJECT_ROUTE','COMPANION_CENSUS');(b/'release-companion-census-v1.py').write_bytes(s.encode())
import runpy
runpy.run_path(str(b/'repair-companion-binding-v1.py'))
print('Draft helpers prepared; rerun after root freeze to repin reviewed manifest.')

import subprocess,sys
subprocess.run([sys.executable,str(b/'test-companion-census-host-v1.py')],check=True)

subprocess.run([sys.executable,str(b/'test-companion-binding-host-v1.py')],check=True)
