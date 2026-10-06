from pathlib import Path
import importlib.util,re,json,subprocess,sys,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');c=b/'companion-census-controls-v1';h=b/'companion-census-root-helpers-v1';syn=c/'synthetic';syn.mkdir(exist_ok=True);sp=importlib.util.spec_from_file_location('n',c/'analyze-night.py');n=importlib.util.module_from_spec(sp);sp.loader.exec_module(n);results=[]
for order in (0,1):
 e=b/f'night-ablation-ps2-object-route-order{order}-20261006-evidence';s=(e/'stdout.log').read_text(encoding='utf8').replace('kind=22','kind=24');art=(e/'night-ablation.log').read_text(encoding='utf8')
 def zero(m):
  row=dict(x.split('=')for x in m[0].split()[2:]);
  for k in ('calls','units1','units2','units3','units4','timedCalls','reads','ticks'):row[k]='0'
  return 'LOG: NIGHTPROD '+' '.join(k+'='+v for k,v in row.items())
 s=re.sub(r'LOG: NIGHTPROD [^\n]+',zero,s)
 def context(m):
  row=dict(x.split('=')for x in m[0].split()[2:]);p=int(row['phase']);row['night']=str(int((p==1)!=(order==1)));return 'LOG: NIGHTCONTEXT '+' '.join(k+'='+v for k,v in row.items())
 s=re.sub(r'LOG: NIGHTCONTEXT [^\n]+',context,s)
 for p in range(3):
  for off in (750,1155):
   for role in range(5):
    v=[0]*25
    if role==0:v[:3]=[100,90,10];v[10]=100
    s+=f'\nLOG: CENSUS phase={p} offset={off} night={int((p==1)!=(order==1))} role={role} '+' '.join(f'f{i}={x}'for i,x in enumerate(v))+'\n'
 r=n.analyze(s,art,'host',24,order,0,0);results.append('positive'+str(order))
 for old,new in [('f0=100','f0=99'),('role=0','role=9'),('f9=0','f9=1'),('f20=0','f20=100'),('timedCalls=0','timedCalls=1'),('night='+str(int(order==1))+' role=0','night='+str(int(order!=1))+' role=0')]:
  bad=s.replace(old,new,1)
  try:n.analyze(bad,art,'host',24,order,0,0)
  except ValueError:results.append('reject'+str(order)+old)
  else:raise AssertionError(old)
 (syn/f'order{order}.stdout').write_bytes(s.encode());(syn/f'order{order}.artifact').write_bytes(art.encode());out=syn/f'order{order}.cli.json'
 if out.exists():out.unlink()
 q=subprocess.run([sys.executable,str(c/'analyze-night-cli.py'),'--stdout',str(syn/f'order{order}.stdout'),'--artifact',str(syn/f'order{order}.artifact'),'--environment','host','--kind','24','--order',str(order),'--joint','0','--restored','0','--report',str(out)],capture_output=True);assert q.returncode==0,q.stdout+q.stderr;results.append('cli'+str(order))
proof=dict(status='PASS_KIND24_HOST_SYNTHETIC_AND_CLI_ONLY',results=results,physicalExecutionAccepted=False);p=b/'companion-census-host-controls-v1.json';p.write_bytes((json.dumps(proof,indent=2)+'\n').encode());(h/'host-controls-proof.json').write_bytes(p.read_bytes());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode());print(proof['status'],len(results))
