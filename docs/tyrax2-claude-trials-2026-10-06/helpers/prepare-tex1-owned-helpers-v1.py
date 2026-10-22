from pathlib import Path
import shutil,json,hashlib,re,subprocess,sys
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'tex1-owned-physical-v1';h=b/'tex1-owned-root-helpers-v1';c=b/'tex1-owned-controls-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for old,d in [('spot-result-controls-v2',c),('companion-census-root-helpers-v1',h)]:
 shutil.copytree(b/old,d,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','synthetic'))
 for p in d.rglob('*'):
  if p.is_file()and p.suffix in('.py','.ps1','.json','.md'):
   s=p.read_text(encoding='utf8').replace('spot-result','tex1-owned').replace('tex1-owned-physical-v2','tex1-owned-physical-v1').replace('tex1-owned-controls-v2','tex1-owned-controls-v1').replace('tex1-owned-root-helpers-v2','tex1-owned-root-helpers-v1').replace('companion-census','tex1-owned')
   if p.parent==h or p.name=='analyze-night-cli.py':s=re.sub(r'\b24\b','28',s)if old.startswith('companion')else re.sub(r'\b23\b','28',s)
   p.write_bytes(s.encode())
p=c/'analyze-night.py';s=p.read_text(encoding='utf8').replace(',21,23)',',21,23,28)');start=s.index('  if kind==23:');end=s.index('  r.update(producerObserverFlags',start)
s=s[:start]+'''  if kind==28:
   for p,o in gs:
    v=fm[p,o,0];calls,owned,candidate,compared,mismatch=[n(v,k)for k in('calls','units1','units2','units3','units4')];g=fm[p,o,1]
    need(calls>0 and 0<owned<=calls and compared==2*calls and mismatch==0,'every TEX1 full64bit parity')
    need(candidate==(owned if flags[p]else 0),'owned activation exact flags')
    need(n(g,'calls')+owned==calls and n(g,'units1')==calls-candidate,'generic/owned and actual original reads partition')
    need(all(n(g,k)==0 for k in ('units2','units3','units4')),'fallback unused counters')
    for st in range(5):
     v=fm[p,o,st];need(n(v,'timedCalls')==n(v,'reads')==n(v,'ticks')==0,'no added TEX1 clocks')
     if st>=2:need(all(n(v,k)==0 for k in ('calls','units1','units2','units3','units4')),'unused TEX1 stages')
   r.update(tex1Full64BitColdParity=True,ownedCandidateSparseOnly=True,genericMutableLodOriginalPath=True,previousSetInfoOrderingPreserved=True,abiClassGrowthRequiresActualTargetAudit=True,commonPreparedStateFootprintUnpriced=True)
'''+s[end:];s=s.replace("r['interpretation']='Producer observer","r['interpretation']='Same-ELF owned TEX1 candidate elapsed contrast; common prepared selector footprint unpriced' if kind==28 else 'Producer observer");p.write_bytes(s.encode());shutil.copyfile(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
a=json.loads((h/'host-authority.json').read_text(encoding='utf8'));a['sourcePins']={str(p):sha(p)for p in [c/'analyze-loop.py',c/'analyze-night.py',c/'analyze-night-cli.py',c/'corona_controls.py',c/'source-controls/night_plan.hpp',c/'source-controls/stapip_vu1_shared_defines.h']};mf=f/'target-source-manifest.json';a['pricingSourceManifestSha256']=sha(mf if mf.exists()else f/'draft-source-manifest.json');a['draftUnreleased']=not mf.exists();(h/'host-authority.json').write_bytes((json.dumps(a,indent=2)+'\n').encode());review=b'# Kind28 owned TEX1 draft\n\nOnly StaPipCore-owned initialized lod opts in after renderer init; private LOD fields were set in Core constructor. Generic public init resets owned=false and reads no LOD state; mutable external fields use original seven-field packing. Prepacked linear/nearest/current word mirrors existing setInfo, preserving sendObjectData BEFORE setInfo. Fullnight/tableOn both arms, full sampler each phase. Cold750/1155 compares all64 semantic TEX1 bits for every emission and counts owned/candidate/generic/original reads. No new Count clocks or waits. Common prepared-state and selector maintenance footprint is unpriced. Added C++ class state requires actual target ABI audit; packet/VU layout identity not established by host harness. Source remains draft until root review.\n';(h/'source-review.md').write_bytes(review);(h/'README.md').write_bytes(review)
s=(b/'release-companion-census-v1.py').read_text(encoding='utf8').replace('companion-census','tex1-owned').replace('COMPANION_CENSUS','TEX1_OWNED');s=re.sub(r"scope='[^']*'", "scope='Private owned TEX1 fullnight candidate, generic seven-field packing and previous-setInfo semantics retained. Cold complete64-bit oracle; no new clocks or waits. Actual target ABI class growth and linked image proof mandatory. Physical elapsed remains pending; common prepared-state footprint unpriced.'",s);(b/'release-tex1-owned-v1.py').write_bytes(s.encode())
subprocess.run([sys.executable,str(b/'test-tex1-owned-host-v1.py')],check=True)
(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode());print('kind28 draft controls prepared')
