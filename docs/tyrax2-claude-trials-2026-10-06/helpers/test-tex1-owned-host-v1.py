from pathlib import Path
import re,json,hashlib,subprocess,sys,importlib.util
b=Path('F:/Projects/tyrax2-lab-20261001');c=b/'tex1-owned-controls-v1';h=b/'tex1-owned-root-helpers-v1';syn=c/'synthetic';syn.mkdir(exist_ok=True);sp=importlib.util.spec_from_file_location('n',c/'analyze-night.py');n=importlib.util.module_from_spec(sp);sp.loader.exec_module(n);checks=[]
for order in (0,1):
 e=b/f'night-ablation-ps2-object-route-order{order}-20261006-evidence';s=(e/'stdout.log').read_text(encoding='utf8').replace('kind=22','kind=28');art=(e/'night-ablation.log').read_text(encoding='utf8')
 def row(m):
  r=dict(x.split('=')for x in m[0].split()[2:]);st,en=int(r['stage']),int(r['enabled']);v=[134,134,134*en,268,0]if st==0 else [0,134*(1-en),0,0,0]if st==1 else[0]*5
  for k,x in zip(('calls','units1','units2','units3','units4','timedCalls','reads','ticks'),v+[0,0,0]):r[k]=str(x)
  return 'LOG: NIGHTPROD '+' '.join(k+'='+x for k,x in r.items())
 s=re.sub(r'LOG: NIGHTPROD [^\r\n]+',row,s);n.analyze(s,art,'host',28,order,0,0);checks.append('positive'+str(order))
 for old,new in [('calls=134','calls=0'),('units1=134','units1=135'),('units3=268','units3=267'),('units4=0','units4=1'),('timedCalls=0','timedCalls=1'),('units2=134','units2=133')]:
  try:n.analyze(s.replace(old,new,1),art,'host',28,order,0,0)
  except ValueError:checks.append('rejected'+str(order)+old)
  else:raise AssertionError(old)
 (syn/f'order{order}.stdout').write_bytes(s.encode());(syn/f'order{order}.artifact').write_bytes(art.encode());p=syn/f'order{order}.cli.json'
 if p.exists():p.unlink()
 r=subprocess.run([sys.executable,str(c/'analyze-night-cli.py'),'--stdout',str(syn/f'order{order}.stdout'),'--artifact',str(syn/f'order{order}.artifact'),'--environment','host','--kind','28','--order',str(order),'--joint','0','--restored','0','--report',str(p)],capture_output=True);assert r.returncode==0,r.stdout+r.stderr;checks.append('cli'+str(order))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();proof=dict(status='PASS_KIND28_HOST_SYNTHETIC_AND_CLI_ONLY',checks=checks,sourceOracleProofSha256=sha(b/'tex1-owned-source-controls-v1/proof.json'),nativeExecutionAccepted=False);p=b/'tex1-owned-host-controls-v1.json';p.write_bytes((json.dumps(proof,indent=2)+'\n').encode());(h/'host-controls-proof.json').write_bytes(p.read_bytes());print(proof['status'],len(checks))
