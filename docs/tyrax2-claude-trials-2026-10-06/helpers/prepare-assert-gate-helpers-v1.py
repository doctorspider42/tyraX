from pathlib import Path
import shutil,json,hashlib,re,importlib.util,subprocess,sys
b=Path('F:/Projects/tyrax2-lab-20261001');h=b/'assert-gate-root-helpers-v1';c=b/'assert-gate-controls-v1';f=b/'assert-gate-physical-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for old,out in [('object-route-root-helpers-v1',h),('object-route-controls-v1',c)]:
 assert not out.exists();shutil.copytree(b/old,out,ignore=shutil.ignore_patterns('__pycache__','synthetic','host-kind9-*','preparation.json','helper-pins.json','host-controls-proof.json'))
 for p in out.rglob('*'):
  if p.is_file()and p.suffix in('.py','.ps1','.json','.md'):
   s=p.read_text(encoding='utf8').replace('object-route','assert-gate')
   if p.parent==h or p.name=='analyze-night-cli.py':s=re.sub(r'\b22\b','25',s)
   p.write_bytes(s.encode())
p=c/'analyze-night.py';s=p.read_text().replace(',21,22)',',21,22,25)');start=s.index('  if kind==22:');end=s.index('  r.update(producerObserverFlags',start)
s=s[:start]+'''  if kind==25:
   for p,o in gs:
    v=fm[p,o,0];calls=n(v,'calls');need(calls>0,'entered nonempty validation cold calls')
    need(n(v,'units1')==12*calls,'twelve active assertions compile-time guard')
    need(n(v,'units2')==12*calls and n(v,'units3')==0,'both cold arms execute every original assertion')
    need(n(v,'units4')==(calls if flags[p]else 0),'requested hot assertion bypass arm')
    for st in range(5):
     row=fm[p,o,st];need(n(row,'timedCalls')==n(row,'reads')==n(row,'ticks')==0,'assert gate adds no scope clocks')
     if st:need(all(n(row,k)==0 for k in('calls','units1','units2','units3','units4')),'unused assertion stages')
   r['candidateActivationSparseOnly']=True;r['coldValidationsExecutedBothArms']=True;r['hotBypassCountObserved']=False;r['assertionStatementCount']=12
'''+s[end:]
s=s.replace("r['interpretation']='Producer observer enabled net tax contrast'", "r['interpretation']='Private bounded-fixture original validation gate elapsed contrast' if kind==25 else 'Producer observer enabled net tax contrast'")
p.write_bytes(s.encode());shutil.copyfile(f/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
(h/'source-review.md').write_bytes((f/'source-review.md').read_bytes());(h/'README.md').write_bytes((f/'source-review.md').read_bytes())
sp=importlib.util.spec_from_file_location('n',c/'analyze-night.py');n=importlib.util.module_from_spec(sp);sp.loader.exec_module(n);syn=c/'synthetic';syn.mkdir();results=[]
for order in(0,1):
 e=b/f'night-ablation-ps2-object-route-order{order}-20261006-evidence';s=(e/'stdout.log').read_text(encoding='utf8').replace('kind=22','kind=25');art=(e/'night-ablation.log').read_text(encoding='utf8')
 def row(match):
  r=dict(x.split('=')for x in match[0].split()[2:]);st,en=[int(r[k])for k in('stage','enabled')];v=[134,1608,1608,0,134 if en else 0]if st==0 else [0]*5
  for k,x in zip(('calls','units1','units2','units3','units4','timedCalls','reads','ticks'),v+[0,0,0]):r[k]=str(x)
  return 'LOG: NIGHTPROD '+' '.join(k+'='+x for k,x in r.items())
 s=re.sub(r'LOG: NIGHTPROD [^\r\n]+',row,s);n.analyze(s,art,'host',25,order,0,0);results.append('positive'+str(order))
 mutations=[('calls=134','calls=0'),('units1=1608','units1=0'),('units2=1608','units2=0'),('units3=0','units3=1'),('units4=134','units4=0'),('reads=0','reads=2'),('timedCalls=0','timedCalls=1')]
 for old,new in mutations:
  bad=s.replace(old,new,1)
  try:n.analyze(bad,art,'host',25,order,0,0)
  except ValueError:results.append('rejected'+str(order)+old)
  else:raise AssertionError(old)
 (syn/f'order{order}.stdout').write_bytes(s.encode());(syn/f'order{order}.artifact').write_bytes(art.encode());r=subprocess.run([sys.executable,str(c/'analyze-night-cli.py'),'--stdout',str(syn/f'order{order}.stdout'),'--artifact',str(syn/f'order{order}.artifact'),'--environment','host','--kind','25','--order',str(order),'--joint','0','--restored','0','--report',str(syn/f'order{order}.cli.json')],capture_output=True);assert r.returncode==0,r.stdout+r.stderr;results.append('cli'+str(order))
proof=dict(status='PASS_KIND25_HOST_SYNTHETIC_AND_CLI_ONLY',results=results,physicalExecutionAccepted=False,sourceGateCorrectnessNotEstablishedByParser=True);p=b/'assert-gate-host-controls-v1.json';p.write_bytes((json.dumps(proof,indent=2)+'\n').encode());(h/'host-controls-proof.json').write_bytes(p.read_bytes())
a=dict(sourcePins={str(p):sha(p)for p in [c/'analyze-loop.py',c/'analyze-night.py',c/'analyze-night-cli.py',c/'corona_controls.py',c/'source-controls/night_plan.hpp',c/'source-controls/stapip_vu1_shared_defines.h']},pricingSourceManifestSha256=sha(f/'target-source-manifest.json'));(h/'host-authority.json').write_bytes((json.dumps(a,indent=2)+'\n').encode())
(h/'preparation.json').write_bytes((json.dumps(dict(status='DRAFT_NO_ROOT_RELEASE',frozen=False,sourceReviewRequired=True,nativeExecutionAccepted=False,inheritedRuntimeGuardPreserved=True),indent=2)+'\n').encode())
(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode());print(proof['status'],len(results))
