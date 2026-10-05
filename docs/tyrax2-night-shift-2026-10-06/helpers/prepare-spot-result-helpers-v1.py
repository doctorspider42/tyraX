from pathlib import Path
import shutil,json,hashlib,re,importlib.util,subprocess,sys
b=Path('F:/Projects/tyrax2-lab-20261001');h=b/'spot-result-root-helpers-v1';c=b/'spot-result-controls-v1';f=b/'spot-result-physical-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for old,out in [('cycle-reuse-root-helpers-v1',h),('cycle-reuse-controls-v1',c)]:
 assert not out.exists();shutil.copytree(b/old,out,ignore=shutil.ignore_patterns('__pycache__','synthetic'))
 for p in out.rglob('*'):
  if p.is_file()and p.suffix in('.py','.ps1','.json','.md'):
   s=p.read_text(encoding='utf8').replace('cycle-reuse','spot-result')
   if p.parent==h or p.name=='analyze-night-cli.py':s=re.sub(r'\b21\b','23',s)
   p.write_bytes(s.encode())
p=c/'analyze-night.py';s=p.read_text().replace(',21)',',21,23)');start=s.index('  if kind==21:');end=s.index('  r.update(producerObserverFlags',start)
s=s[:start]+'''  if kind==23:
   for p,o in gs:
    v=fm[p,o,0];calls,eligible,reused,compared,mismatch=[n(v,k)for k in('calls','units1','units2','units3','units4')]
    need(calls>0 and 0<eligible<=calls and 0<=reused<=eligible and compared==15*calls and mismatch==0,'complete local-light scalar output parity')
    need(reused>0 if flags[p]else reused==0,'positive enabled memo / disabled no-reuse')
    for st in range(5):
     v=fm[p,o,st];need(n(v,'timedCalls')==n(v,'reads')==n(v,'ticks')==0,'no additional memo clocks')
     if st:need(all(n(v,k)==0 for k in('calls','units1','units2','units3','units4')),'unused memo stages')
   r['spotResultFieldsComparedPerCall']=15;r['memoActivationSparseOnly']=True;r['lightSelectionAndExistingWaitsPreserved']=True
'''+s[end:];p.write_bytes(s.encode());shutil.copyfile(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
a=json.loads((h/'host-authority.json').read_text());a['sourcePins']={str(p):sha(p)for p in [c/'analyze-loop.py',c/'analyze-night.py',c/'analyze-night-cli.py',c/'corona_controls.py',c/'source-controls/night_plan.hpp',c/'source-controls/stapip_vu1_shared_defines.h']};a['pricingSourceManifestSha256']=sha(f/'target-source-manifest.json');(h/'host-authority.json').write_bytes((json.dumps(a,indent=2)+'\n').encode())
(h/'source-review.md').write_bytes(b'# Kind23 whole local-light result memo\n\n32 bounded entries keyed by29 exact input words. Enabled finite inputs only; collisions require full key equality or replace. No borrowed pointers, no approximation, selection/influence rejection unchanged. Cold original function compares enabled and14 float fields at750/1155. Existing waits retained; no new clocks. Reuse counts are sparse witnesses, not full-window totals. Common key/preparation/code footprint is unpriced.\n');(h/'README.md').write_bytes((h/'source-review.md').read_bytes())
for typ in('start','wait'):
 s=(b/f'{typ}-cycle-reuse-physical-root-v1.ps1').read_text().replace('cycle-reuse','spot-result').replace('--kind 21','--kind 23');(b/f'{typ}-spot-result-physical-root-v1.ps1').write_bytes(s.encode())
s=(b/'release-cycle-reuse-v1.py').read_text().replace('cycle-reuse','spot-result').replace('CYCLE_REUSE','SPOT_RESULT');(b/'release-spot-result-v1.py').write_bytes(s.encode())
sp=importlib.util.spec_from_file_location('n',c/'analyze-night.py');n=importlib.util.module_from_spec(sp);sp.loader.exec_module(n);syn=c/'synthetic';syn.mkdir();results=[]
for order in(0,1):
 e=b/f'night-ablation-ps2-paused-clock-order{order}-20261006-evidence';s=(e/'stdout.log').read_text(encoding='utf8').replace('kind=20','kind=23');art=(e/'night-ablation.log').read_text(encoding='utf8');s=re.sub(r'LOG: NIGHTCLOCK[^\n]*\n','',s)
 def row(match):
  r=dict(x.split('=')for x in match[0].split()[2:]);st,en=int(r['stage']),int(r['enabled']);v=[134,114,102*en,2010,0]if st==0 else[0]*5
  for k,x in zip(('calls','units1','units2','units3','units4'),v):r[k]=str(x)
  return 'LOG: NIGHTPROD '+' '.join(k+'='+x for k,x in r.items())
 s=re.sub(r'LOG: NIGHTPROD [^\r\n]+',row,s);n.analyze(s,art,'host',23,order,0,0);results.append('positive'+str(order))
 for old,new in [('calls=134','calls=0'),('units1=114','units1=0'),('units2=102','units2=999'),('units3=2010','units3=2009'),('units4=0','units4=1')]:
  bad=s.replace(old,new,1)
  try:n.analyze(bad,art,'host',23,order,0,0)
  except ValueError:results.append('rejected'+str(order)+old)
  else:raise AssertionError(old)
 (syn/f'order{order}.stdout').write_bytes(s.encode());(syn/f'order{order}.artifact').write_bytes(art.encode());r=subprocess.run([sys.executable,str(c/'analyze-night-cli.py'),'--stdout',str(syn/f'order{order}.stdout'),'--artifact',str(syn/f'order{order}.artifact'),'--environment','host','--kind','23','--order',str(order),'--joint','0','--restored','0','--report',str(syn/f'order{order}.cli.json')],capture_output=True);assert r.returncode==0,r.stdout+r.stderr;results.append('cli'+str(order))
proof=dict(status='PASS_KIND23_HOST_SYNTHETIC_AND_CLI_ONLY',results=results,physicalExecutionAccepted=False);p=b/'spot-result-host-controls-v1.json';p.write_bytes((json.dumps(proof,indent=2)+'\n').encode());(h/'host-controls-proof.json').write_bytes(p.read_bytes());(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode());print(proof['status'],len(results))
