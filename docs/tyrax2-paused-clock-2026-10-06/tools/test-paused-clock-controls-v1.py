from pathlib import Path
import importlib.util,re,json,hashlib,subprocess,sys
b=Path('F:/Projects/tyrax2-lab-20261001');c=b/'paused-clock-controls-v1';h=b/'paused-clock-root-helpers-v1';out=c/'synthetic';out.mkdir();sp=importlib.util.spec_from_file_location('n',c/'analyze-night.py');n=importlib.util.module_from_spec(sp);sp.loader.exec_module(n);results=[];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for order,stem in [(0,'night-ablation-ps2-sky-retint-order0-20261005'),(1,'night-ablation-ps2-sky-retint-order1-20261005-retry2')]:
 e=b/(stem+'-evidence');s=(e/'stdout.log').read_text(encoding='utf8').replace('kind=19','kind=20');a=(e/'night-ablation.log').read_text(encoding='utf8');dt={ (int(x['phase']),int(x['offset'])):x['dtBits']for x in n.loop.rows(s,'NIGHTCONTEXT')}
 def row(m):
  r=dict(x.split('=')for x in m[0].split()[2:]);p,o,st,en=[int(r[k])for k in ('phase','offset','stage','enabled')];v=[1,en,1-en,0,0]if st==0 else [1-en,1-en,0,0,0]if st==1 else [0]*5
  for k,x in zip(('calls','units1','units2','units3','units4'),v):r[k]=str(x)
  line='LOG: NIGHTPROD '+' '.join(k+'='+x for k,x in r.items())
  return ('LOG: NIGHTCLOCK scene=0 paused='+str(en)+' before=00000000 after='+('00000000'if en else'38e00000')+' dt='+dt[p,o]+'\n'if st==0 else'')+line
 s=re.sub(r'LOG: NIGHTPROD [^\r\n]+',row,s);n.analyze(s,a,'host',20,order,0,0);results.append('positive'+str(order));(out/f'order{order}.stdout').write_bytes(s.encode());(out/f'order{order}.artifact').write_bytes(a.encode())
 for old,new in [('paused=1','paused=0'),('after=00000000','after=38e00000'),('after=38e00000','after=00000000'),('scene=0 paused=','scene=1 paused=')]:
  bad=s.replace('LOG: NIGHTCLOCK '+old,'LOG: NIGHTCLOCK '+new,1)if old.startswith('scene')else re.sub(r'(LOG: NIGHTCLOCK [^\n]*)'+re.escape(old),lambda m:m[1]+new,s,count=1)
  if bad==s:continue
  try:n.analyze(bad,a,'host',20,order,0,0)
  except ValueError:results.append('rejected'+str(order)+old)
  else:raise AssertionError(old)
 for stage,key,value in [(0,'calls',0),(0,'units3',1),(1,'units4',1),(2,'calls',1)]:
  active=re.search(r'LOG: NIGHTPROD [^\n]*stage='+str(stage)+r' enabled=1[^\n]*',s)[0];bad=s.replace(active,re.sub(key+r'=\d+',key+'='+str(value),active),1)
  try:n.analyze(bad,a,'host',20,order,0,0)
  except ValueError:results.append('rejected'+str(order)+str(stage)+key)
  else:raise AssertionError(key)
 cmd=[sys.executable,str(c/'analyze-night-cli.py'),'--stdout',str(out/f'order{order}.stdout'),'--artifact',str(out/f'order{order}.artifact'),'--environment','host','--kind','20','--order',str(order),'--joint','0','--restored','0','--report',str(out/f'order{order}.cli.json')];r=subprocess.run(cmd,capture_output=True);assert r.returncode==0,r.stdout+r.stderr;results.append('cli'+str(order))
proof=dict(status='PASS_KIND20_HOST_SYNTHETIC_AND_CLI_ONLY',results=results,sourcePins={str(p):sha(p)for p in [c/'analyze-night.py',c/'analyze-night-cli.py']},physicalExecutionAccepted=False);p=b/'paused-clock-host-controls-v1.json';p.write_bytes((json.dumps(proof,indent=2)+'\n').encode());(h/'host-controls-proof.json').write_bytes(p.read_bytes());(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode());print(proof['status'],len(results))
