from pathlib import Path
import shutil,json,hashlib,re
b=Path('F:/Projects/tyrax2-lab-20261001');h=b/'paused-clock-root-helpers-v1';c=b/'paused-clock-controls-v1';f=b/'paused-clock-physical-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for old,out in [('sky-retint-root-helpers-v1',h),('sky-retint-controls-v1',c)]:
 assert not out.exists();shutil.copytree(b/old,out,ignore=shutil.ignore_patterns('__pycache__','synthetic'))
 for p in out.rglob('*'):
  if p.is_file()and p.suffix in ('.py','.ps1','.json','.md'):
   s=p.read_text(encoding='utf8').replace('sky-retint-physical-v1','paused-clock-physical-v1').replace('sky-retint-root-helpers-v1','paused-clock-root-helpers-v1').replace('sky-retint-controls-v1','paused-clock-controls-v1')
   if p.parent==h or p.name=='analyze-night-cli.py':s=re.sub(r'\b19\b','20',s)
   p.write_bytes(s.encode())
p=c/'analyze-night.py';s=p.read_text();s=s.replace('16,17,18,19)','16,17,18,19,20)');start=s.index('  if kind==19:');end=s.index('  r.update(producerObserverFlags',start)
s=s[:start]+'''  if kind==20:
   clocks=loop.rows(stdout,'NIGHTCLOCK');need(len(clocks)==6,'six clock witnesses');clockMap={}
   for row,(p,o) in zip(clocks,sorted(gs)):
    need(set(row)==set('scene paused before after dt'.split()),'clock schema');need(n(row,'scene')==0 and n(row,'paused')==flags[p],'actual clock pause flag')
    a,z,d=[int(row[k],16)for k in('before','after','dt')];need(a==0 and 0<d<0x3f000000,'night hour0 and positive physical dt');need(z==0 if flags[p]else 0<z<0x3b000000,'paused exact hour / control advancement')
    context=next(x for x in loop.rows(stdout,'NIGHTCONTEXT')if n(x,'phase')==p and n(x,'offset')==o);need(row['dt']==context['dtBits'],'clock dt matches existing frame dt witness');clockMap[f'{p}:{o}']=row
   for p,o in gs:
    v=fm[(p,o,0)];need(n(v,'calls')==1 and n(v,'units1')==flags[p] and n(v,'units2')==1-flags[p] and n(v,'units3')==n(v,'units4')==0,'clock actual cold activation')
    v=fm[(p,o,1)];need(n(v,'calls')<=1 and n(v,'units1')==n(v,'calls') and n(v,'units2')==n(v,'units3')==n(v,'units4')==0,'retint mediator accounting')
    for st in range(5):
     row=fm[(p,o,st)];need(n(row,'timedCalls')==n(row,'reads')==n(row,'ticks')==0,'clock no new scoped clocks')
     if st>=2:need(all(n(row,k)==0 for k in('calls','units1','units2','units3','units4')),'clock unused stages')
   r['pausedClockColdWitnesses']=clockMap;r['candidateActivationSparseOnly']=True;r['retintMayBeInactiveWhenPaused']=True
'''+s[end:];p.write_bytes(s.encode());shutil.copyfile(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp');a=json.loads((h/'host-authority.json').read_text());a['sourcePins']={str(p):sha(p)for p in [c/'analyze-loop.py',c/'analyze-night.py',c/'analyze-night-cli.py',c/'corona_controls.py',c/'source-controls/night_plan.hpp',c/'source-controls/stapip_vu1_shared_defines.h']};a['pricingSourceManifestSha256']=sha(f/'target-source-manifest.json');(h/'host-authority.json').write_bytes((json.dumps(a,indent=2)+'\n').encode());(h/'source-review.md').write_bytes(b'# Kind20 exact requested-hour pause\n\nSkip only clock advancement when paused; active evaluation and physical dt remain. Both arms retain the promoted color-only sky path. Cold witnesses bind requested0, actual pause/hour and the unchanged frame dt. Retint counts are mediators, not pause activation. No clock-scope reads. Prices measure complete clock-policy contrast, including other dt-sensitive/effect work; no EE-only or sky-only attribution. Exact hour changes intentionally differ from the previous near-midnight approximation.\n')
for typ in ('start','wait'):
 s=(b/f'{typ}-sky-retint-physical-root-v1.ps1').read_text().replace('sky-retint-root-helpers-v1','paused-clock-root-helpers-v1').replace('sky-retint-physical-v1','paused-clock-physical-v1').replace('--kind 19','--kind 20');(b/f'{typ}-paused-clock-physical-root-v1.ps1').write_bytes(s.encode())
s=(b/'release-sky-retint-v1.py').read_text().replace('sky-retint-physical-v1','paused-clock-physical-v1').replace('sky-retint-root-helpers-v1','paused-clock-root-helpers-v1').replace('sky-retint-native-root-review-v1','paused-clock-native-root-review-v1').replace('sky-retint-host-controls-v1','paused-clock-host-controls-v1');(b/'release-paused-clock-v1.py').write_bytes(s.encode());print('Kind20 dedicated clock controls prepared')
