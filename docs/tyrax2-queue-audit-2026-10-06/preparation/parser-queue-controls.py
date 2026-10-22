from pathlib import Path
import argparse,json,subprocess,sys,re
p=argparse.ArgumentParser();p.add_argument('stdout',type=Path);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists();a.out.mkdir()
ls=a.artifact.read_text().splitlines();j=next(i for i,s in enumerate(ls)if s.startswith('LOG: CLOSEOUTQUEUE phase=0 i=0 '));off=next(i for i,s in enumerate(ls)if s.startswith('LOG: CLOSEOUTQUEUE phase=1 i=0 '));call=next(i for i,s in enumerate(ls)if s.startswith('LOG: CLOSEOUTQUEUECALLS phase=0 '))
def patch(i,k,v):return ls[:i]+[re.sub(r'\b'+k+r'=\d+',k+'='+str(v),ls[i],count=1)]+ls[i+1:]
cases={'missing':ls[:j]+ls[j+1:],'duplicate':ls+[ls[j]],'partition':patch(j,'q4',0),'wait':patch(j,'wait',0),'reads':patch(j,'reads',0),'off-clock':patch(off,'reads',2),'snapshot-activation':patch(j,'n3',0),'recorded-activation':patch(j,'n11',0),'scope-calls':patch(call,'q4',0)}
results={}
for name,text in cases.items():
 f=a.out/(name+'.txt');f.write_text('\n'.join(text)+'\n');r=subprocess.run([sys.executable,str(Path(__file__).with_name('analyze-queue.py')),str(a.stdout),'--artifact',str(f),'--out',str(a.out/(name+'.json'))],capture_output=True,text=True);(a.out/(name+'.stderr')).write_text(r.stderr);assert r.returncode!=0,name;results[name]=dict(rejected=True,returncode=r.returncode)
(a.out/'controls.json').write_text(json.dumps(dict(status='PASS_NINE_INVALID_QUEUE_ARTIFACTS_REJECTED',controls=results),indent=2)+'\n');print('PASS_NINE_INVALID_QUEUE_ARTIFACTS_REJECTED')
