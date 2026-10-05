from pathlib import Path
import shutil,json,hashlib,re,importlib.util,subprocess,sys
b=Path('F:/Projects/tyrax2-lab-20261001');h=b/'object-route-root-helpers-v1';c=b/'object-route-controls-v1';f=b/'object-route-physical-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for old,out in [('cycle-reuse-root-helpers-v1',h),('cycle-reuse-controls-v1',c)]:
 assert not out.exists();shutil.copytree(b/old,out,ignore=shutil.ignore_patterns('__pycache__','synthetic'))
 for p in out.rglob('*'):
  if p.is_file()and p.suffix in('.py','.ps1','.json','.md'):
   s=p.read_text(encoding='utf8').replace('cycle-reuse','object-route')
   if p.parent==h or p.name=='analyze-night-cli.py':s=re.sub(r'\b21\b','22',s)
   p.write_bytes(s.encode())
p=c/'analyze-night.py';s=p.read_text().replace(',21)',',21,22)');start=s.index('  if kind==21:');end=s.index('  r.update(producerObserverFlags',start)
s=s[:start]+'''  if kind==22:
   for p,o in gs:
    rows=[fm[p,o,st]for st in range(5)];calls=[n(x,'calls')for x in rows];need(calls[0]>0 and len(set(calls))==1,'five entered route scope counts')
    timed=[n(x,'timedCalls')for x in rows];need(len(set(timed))==1,'same full-window scope coverage');need(timed[0]>0 if flags[p]and o==1155 else timed[0]==0,'route timed activation')
    need(n(rows[0],'units1')>0 and n(rows[0],'units1')==n(rows[3],'units1'),'routed geometry units')
    need(n(rows[0],'units2')<=calls[0]and n(rows[0],'units3')<=calls[0],'head flags')
    need(n(rows[1],'units2')<=n(rows[1],'units1')<=calls[0]and n(rows[1],'units3')<=calls[0],'local spot flags')
    need(n(rows[2],'units2')<=n(rows[2],'units1')<=calls[0]and n(rows[2],'units4')<=calls[0],'clip/upload wait partition')
    need(all(n(rows[3],k)==0 for k in('units2','units3','units4')),'info unused counters')
    need(n(rows[4],'units1')<=calls[0]and n(rows[4],'units2')<=calls[0]and n(rows[4],'units3')==n(rows[4],'units4')==0,'route decision flags')
   r['scopeNames']=['uniformHead','spotLocalAndUpload','uniformTail','clipperAndInfo','routeFacts'];r['addedClockReadsSeparate']=True;r['scopeElapsedIncludesExistingWaits']=True
'''+s[end:];p.write_bytes(s.encode());shutil.copyfile(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
a=json.loads((h/'host-authority.json').read_text());a['sourcePins']={str(p):sha(p)for p in [c/'analyze-loop.py',c/'analyze-night.py',c/'analyze-night-cli.py',c/'corona_controls.py',c/'source-controls/night_plan.hpp',c/'source-controls/stapip_vu1_shared_defines.h']};a['pricingSourceManifestSha256']=sha(f/'target-source-manifest.json');(h/'host-authority.json').write_bytes((json.dumps(a,indent=2)+'\n').encode())
(h/'source-review.md').write_bytes(b'# Kind22 object route observer\n\nFive disjoint scopes: packet head through directional-light uploads; spot-local arithmetic and spot/custom uploads; clip/constants/options/remaining packet tail; clipper pointer and info; routing decisions before baked replay. Only800..1119 counted. Cold counters750/1155, clip capture synchronous waits counted separately. Existing FLUSHE and waits retained. Full-window elapsed includes scheduling; enabled net tax is not a per-scope subtraction. No candidate optimization.\n');(h/'README.md').write_bytes((h/'source-review.md').read_bytes())
for typ in('start','wait'):
 s=(b/f'{typ}-cycle-reuse-physical-root-v1.ps1').read_text().replace('cycle-reuse','object-route').replace('--kind 21','--kind 22');(b/f'{typ}-object-route-physical-root-v1.ps1').write_bytes(s.encode())
s=(b/'release-cycle-reuse-v1.py').read_text().replace('cycle-reuse','object-route').replace('CYCLE_REUSE','OBJECT_ROUTE');(b/'release-object-route-v1.py').write_bytes(s.encode())
sp=importlib.util.spec_from_file_location('n',c/'analyze-night.py');n=importlib.util.module_from_spec(sp);sp.loader.exec_module(n);syn=c/'synthetic';syn.mkdir();results=[]
for order in(0,1):
 e=b/f'night-ablation-ps2-paused-clock-order{order}-20261006-evidence';s=(e/'stdout.log').read_text(encoding='utf8').replace('kind=20','kind=22');art=(e/'night-ablation.log').read_text(encoding='utf8');s=re.sub(r'LOG: NIGHTCLOCK[^\n]*\n','',s)
 def row(match):
  r=dict(x.split('=')for x in match[0].split()[2:]);st,en,o=[int(r[k])for k in('stage','enabled','offset')];v=[[134,49000,0,0,2000],[134,134,30,0,300],[134,20,20,500,0],[134,49000,0,0,0],[134,100,134,0,0]][st];tc=42880 if en and o==1155 else 0
  for k,x in zip(('calls','units1','units2','units3','units4','timedCalls','reads','ticks'),v+[tc,2*tc,100*tc]):r[k]=str(x)
  return 'LOG: NIGHTPROD '+' '.join(k+'='+x for k,x in r.items())
 s=re.sub(r'LOG: NIGHTPROD [^\r\n]+',row,s);n.analyze(s,art,'host',22,order,0,0);results.append('positive'+str(order))
 for old,new in [('calls=134','calls=0'),('timedCalls=42880','timedCalls=0'),('reads=85760','reads=0'),('units1=49000','units1=1'),('units2=30','units2=999')]:
  bad=s.replace(old,new,1)
  try:n.analyze(bad,art,'host',22,order,0,0)
  except ValueError:results.append('rejected'+str(order)+old)
  else:raise AssertionError(old)
 (syn/f'order{order}.stdout').write_bytes(s.encode());(syn/f'order{order}.artifact').write_bytes(art.encode());r=subprocess.run([sys.executable,str(c/'analyze-night-cli.py'),'--stdout',str(syn/f'order{order}.stdout'),'--artifact',str(syn/f'order{order}.artifact'),'--environment','host','--kind','22','--order',str(order),'--joint','0','--restored','0','--report',str(syn/f'order{order}.cli.json')],capture_output=True);assert r.returncode==0,r.stdout+r.stderr;results.append('cli'+str(order))
proof=dict(status='PASS_KIND22_HOST_SYNTHETIC_AND_CLI_ONLY',results=results,physicalExecutionAccepted=False);p=b/'object-route-host-controls-v1.json';p.write_bytes((json.dumps(proof,indent=2)+'\n').encode());(h/'host-controls-proof.json').write_bytes(p.read_bytes());(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode());print(proof['status'],len(results))
