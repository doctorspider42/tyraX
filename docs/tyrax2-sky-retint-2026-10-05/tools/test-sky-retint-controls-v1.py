from pathlib import Path
import sys,importlib.util,re,json,hashlib,subprocess
b=Path('F:/Projects/tyrax2-lab-20261001');c=b/'sky-retint-controls-v1';h=b/'sky-retint-root-helpers-v1';out=c/'synthetic';out.mkdir()
sp=importlib.util.spec_from_file_location('n',c/'analyze-night.py');n=importlib.util.module_from_spec(sp);sp.loader.exec_module(n)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
results=[]
for order,stem in [(0,'night-ablation-ps2-core-reuse-order0-20261005'),(1,'night-ablation-ps2-core-reuse-v5-order1-20261005')]:
 e=b/(stem+'-evidence');s=(e/'stdout.log').read_text(encoding='utf8').replace('kind=18','kind=19');a=(e/'night-ablation.log').read_text(encoding='utf8')
 def row(m):
  r=dict(x.split('=')for x in m[0].split()[2:]);p=int(r['phase']);st=int(r['stage']);en=int(r['enabled']);v=[1,en,1152*en,0,0]if st==0 else [en,en,en,0,0]if st==1 else [0]*5
  for k,x in zip(('calls','units1','units2','units3','units4'),v):r[k]=str(x)
  return 'LOG: NIGHTPROD '+' '.join(k+'='+x for k,x in r.items())
 s=re.sub(r'LOG: NIGHTPROD [^\r\n]+',row,s)
 n.analyze(s,a,'host',19,order,0,0);results.append('positive'+str(order));(out/f'order{order}.stdout').write_bytes(s.encode());(out/f'order{order}.artifact').write_bytes(a.encode())
 for key,val in [('units3',1),('units4',1),('units2',1151),('units1',0)]:
  active=re.search(r'LOG: NIGHTPROD [^\n]*stage=0 enabled=1[^\n]*',s)[0];bad=s.replace(active,re.sub(key+r'=\d+',key+'='+str(val),active),1)
  try:n.analyze(bad,a,'host',19,order,0,0)
  except ValueError:results.append('rejected'+str(order)+key)
  else:raise AssertionError(key)
 active=re.search(r'LOG: NIGHTPROD [^\n]*stage=1 enabled=1[^\n]*',s)[0]
 for key in ('units1','units2','units3'):
  bad=s.replace(active,re.sub(key+r'=\d+',key+'='+('1'if key=='units3'else'0'),active),1)
  try:n.analyze(bad,a,'host',19,order,0,0)
  except ValueError:results.append('rejectedStamp'+str(order)+key)
  else:raise AssertionError(key)
 cmd=[sys.executable,str(c/'analyze-night-cli.py'),'--stdout',str(out/f'order{order}.stdout'),'--artifact',str(out/f'order{order}.artifact'),'--environment','host','--kind','19','--order',str(order),'--joint','0','--restored','0','--report',str(out/f'order{order}.cli.json')];r=subprocess.run(cmd,capture_output=True);assert r.returncode==0,r.stdout+r.stderr;results.append('cli'+str(order))
proof=dict(status='PASS_KIND19_HOST_SYNTHETIC_AND_REAL_CLI_ONLY',results=results,sourcePins={str(p):sha(p)for p in [c/'analyze-night.py',c/'analyze-night-cli.py']},physicalExecutionAccepted=False)
p=b/'sky-retint-host-controls-v1.json';p.write_bytes((json.dumps(proof,indent=2)+'\n').encode());(h/'host-controls-proof.json').write_bytes(p.read_bytes())
(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode());print(proof['status'],len(results))
