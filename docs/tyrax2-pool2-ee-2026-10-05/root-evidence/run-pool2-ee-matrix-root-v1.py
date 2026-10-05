from pathlib import Path
import subprocess,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');h=lab/'wild-pool2-ee-probe-runtime-v1';p=h/'preparation-proof.json';assert hashlib.sha256(p.read_bytes()).hexdigest()=='ef84865ef536fe65916d4ac8868dd347e512f1eb1948c31df97e8e5e0ea9ad6d'
for rel,want in json.loads(p.read_text())['outputs'].items(): assert hashlib.sha256((h/rel).read_bytes()).hexdigest()==want,rel
jobs=[(v,r,a)for v in(96,192)for r in(1,2,3)for a in(0,1)]
out=lab/'pool2-ee-probe-matrix-execution-v1.json';assert not out.exists();record={'status':'ROOT_SERIAL_CAPTURE_MATRIX_IN_PROGRESS','jobs':[]}
for i,(v,r,a)in enumerate(jobs):
 if i==0:continue
 cmd=['wsl','-d','Ubuntu','--','python3','/mnt/f/Projects/tyrax2-lab-20261001/wild-pool2-ee-probe-runtime-v1/run-case.py','--arm',str(a),'--vertices',str(v),'--repeats',str(r),'--slot',str(172+i),'--native-proof','/mnt/f/Projects/tyrax2-lab-20261001/wild-pool2-ee-probe-native-root-review-v1/proof.json']
 print('RUN_CASE',v,r,a,flush=True);result=subprocess.run(cmd);record['jobs'].append({'vertices':v,'repeats':r,'arm':a,'exit':result.returncode});out.write_text(json.dumps(record,indent=2)+'\n');assert result.returncode==0,'Rejected case: preserved current state, no further launches'
record['status']='ROOT_SERIAL_CAPTURE_MATRIX_CHILDREN_COMPLETED';out.write_text(json.dumps(record,indent=2)+'\n')
