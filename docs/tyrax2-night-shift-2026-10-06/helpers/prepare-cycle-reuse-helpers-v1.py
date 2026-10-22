from pathlib import Path
import shutil,json,hashlib,re,importlib.util,subprocess,sys
b=Path('F:/Projects/tyrax2-lab-20261001');h=b/'cycle-reuse-root-helpers-v1';c=b/'cycle-reuse-controls-v1';f=b/'cycle-reuse-physical-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for old,out in [('paused-clock-root-helpers-v1',h),('paused-clock-controls-v1',c)]:
 assert not out.exists();shutil.copytree(b/old,out,ignore=shutil.ignore_patterns('__pycache__','synthetic'))
 for p in out.rglob('*'):
  if p.is_file()and p.suffix in('.py','.ps1','.json','.md'):
   s=p.read_text(encoding='utf8').replace('paused-clock','cycle-reuse')
   if p.parent==h or p.name=='analyze-night-cli.py':s=re.sub(r'\b20\b','21',s)
   p.write_bytes(s.encode())
p=c/'analyze-night.py';s=p.read_text();s=s.replace(',20)',',20,21)');start=s.index('  if kind==20:');end=s.index('  r.update(producerObserverFlags',start)
s=s[:start]+'''  if kind==21:
   clocks=loop.rows(stdout,'NIGHTCLOCK');ev=loop.rows(stdout,'NIGHTEVAL');need(len(clocks)==len(ev)==6,'six cycle/clock witnesses')
   for clock,row,(p,o) in zip(clocks,ev,sorted(gs)):
    need(set(clock)==set('scene paused before after dt'.split()),'clock schema');need(n(clock,'scene')==0 and n(clock,'paused')==1 and clock['before']==clock['after']=='00000000','both arms exact paused midnight')
    ctx=next(x for x in loop.rows(stdout,'NIGHTCONTEXT')if n(x,'phase')==p and n(x,'offset')==o);need(clock['dt']==ctx['dtBits']and 0<int(clock['dt'],16)<0x3f000000,'real dt bound')
    need(set(row)==set('scene reused compared mismatches'.split())and n(row,'scene')==0 and n(row,'reused')==flags[p]and n(row,'compared')==37 and n(row,'mismatches')==0,'exact cycle output and reuse activation')
    v=fm[(p,o,0)];need([n(v,k)for k in('calls','units1','units2','units3','units4')]==[1,flags[p],37,0,0],'cycle counter accounting')
    for st in range(5):
     v=fm[(p,o,st)];need(n(v,'timedCalls')==n(v,'reads')==n(v,'ticks')==0,'no additional scoped clocks')
     if st==1:need(n(v,'calls')==n(v,'units1')and n(v,'calls')<=1 and n(v,'units2')==n(v,'units3')==n(v,'units4')==0,'retint mediator')
     if st>=2:need(all(n(v,k)==0 for k in('calls','units1','units2','units3','units4')),'unused stage')
   r['cycleReuseColdWitnesses']=ev;r['cycleFullOutputsCompared']=37;r['activationSparseOnly']=True
'''+s[end:];p.write_bytes(s.encode());shutil.copyfile(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
a=json.loads((h/'host-authority.json').read_text());a['sourcePins']={str(p):sha(p)for p in [c/'analyze-loop.py',c/'analyze-night.py',c/'analyze-night-cli.py',c/'corona_controls.py',c/'source-controls/night_plan.hpp',c/'source-controls/stapip_vu1_shared_defines.h']};a['pricingSourceManifestSha256']=sha(f/'target-source-manifest.json');(h/'host-authority.json').write_bytes((json.dumps(a,indent=2)+'\n').encode())
(h/'source-review.md').write_bytes(b'# Kind21 exact cycle evaluation reuse\n\nBoth arms pause midnight, preserve promoted sky retint and restore every cycle output. Reuse key is scene plus exact input hour bits. Cold original evaluation checks all37 floats outside price windows. No new clocks or waits. Same-ELF prices include common footprint and any existing waits; sparse reuse witnesses do not count every priced hit.\n')
(h/'README.md').write_bytes((h/'source-review.md').read_bytes())
for typ in('start','wait'):
 s=(b/f'{typ}-paused-clock-physical-root-v1.ps1').read_text().replace('paused-clock','cycle-reuse').replace('--kind 20','--kind 21');(b/f'{typ}-cycle-reuse-physical-root-v1.ps1').write_bytes(s.encode())
s=(b/'release-paused-clock-v1.py').read_text().replace('paused-clock','cycle-reuse').replace('ROOT_RELEASE_PRIVATE_SKY_RETINT','ROOT_RELEASE_PRIVATE_CYCLE_REUSE');(b/'release-cycle-reuse-v1.py').write_bytes(s.encode())
sp=importlib.util.spec_from_file_location('n',c/'analyze-night.py');n=importlib.util.module_from_spec(sp);sp.loader.exec_module(n);syn=c/'synthetic';syn.mkdir();results=[]
for order in(0,1):
 e=b/f'night-ablation-ps2-paused-clock-order{order}-20261006-evidence';s=(e/'stdout.log').read_text(encoding='utf8').replace('kind=20','kind=21');art=(e/'night-ablation.log').read_text(encoding='utf8');s=re.sub(r'(LOG: NIGHTCLOCK scene=0 paused=)[01]',r'\g<1>1',s);s=re.sub(r'(LOG: NIGHTCLOCK [^\n]*after=)[0-9a-f]+',r'\g<1>00000000',s)
 def row(match):
  r=dict(x.split('=')for x in match[0].split()[2:]);st,en=int(r['stage']),int(r['enabled']);v=[1,en,37,0,0]if st==0 else[0]*5
  for k,x in zip(('calls','units1','units2','units3','units4'),v):r[k]=str(x)
  return ('LOG: NIGHTEVAL scene=0 reused='+str(en)+' compared=37 mismatches=0\n'if st==0 else'')+'LOG: NIGHTPROD '+' '.join(k+'='+x for k,x in r.items())
 s=re.sub(r'LOG: NIGHTPROD [^\r\n]+',row,s);n.analyze(s,art,'host',21,order,0,0);results.append('positive'+str(order))
 for old,new in [('reused=1','reused=0'),('compared=37','compared=36'),('mismatches=0','mismatches=1'),('paused=1','paused=0'),('scene=0 reused=','scene=1 reused=')]:
  bad=s.replace(old,new,1)
  try:n.analyze(bad,art,'host',21,order,0,0)
  except ValueError:results.append('rejected'+str(order)+old)
  else:raise AssertionError(old)
 (syn/f'order{order}.stdout').write_bytes(s.encode());(syn/f'order{order}.artifact').write_bytes(art.encode());r=subprocess.run([sys.executable,str(c/'analyze-night-cli.py'),'--stdout',str(syn/f'order{order}.stdout'),'--artifact',str(syn/f'order{order}.artifact'),'--environment','host','--kind','21','--order',str(order),'--joint','0','--restored','0','--report',str(syn/f'order{order}.cli.json')],capture_output=True);assert r.returncode==0,r.stdout+r.stderr;results.append('cli'+str(order))
proof=dict(status='PASS_KIND21_HOST_SYNTHETIC_AND_CLI_ONLY',results=results,physicalExecutionAccepted=False);p=b/'cycle-reuse-host-controls-v1.json';p.write_bytes((json.dumps(proof,indent=2)+'\n').encode());(h/'host-controls-proof.json').write_bytes(p.read_bytes());(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode());print(proof['status'],len(results))
