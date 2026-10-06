from pathlib import Path
import argparse,subprocess,sys,re,json
p=argparse.ArgumentParser();p.add_argument('stdout',type=Path);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists();a.out.mkdir();ls=a.artifact.read_text().splitlines()
j=next(i for i,s in enumerate(ls)if s.startswith('LOG: CLOSEOUTRENDER phase=0 i=0 '));k=next(i for i,s in enumerate(ls)if s.startswith('LOG: CLOSEOUTRENDER phase=1 i=0 '));c=next(i for i,s in enumerate(ls)if s.startswith('LOG: CLOSEOUTRENDERCALLS phase=0 '))
def field(s,key,v):return re.sub(r'\b'+key+r'=\d+',key+'='+str(v),s,count=1)
def patch(i,key,v):return ls[:i]+[field(ls[i],key,v)]+ls[i+1:]
cases={'missing-render-row':ls[:j]+ls[j+1:],'duplicate-render-row':ls+[ls[j]],'stage-sum':patch(j,'s12',0),'work-sum':patch(j,'w1',0),'wait-disagreement':patch(j,'wait',0),'read-disagreement':patch(j,'reads',0),'disabled-clock':patch(k,'reads',2),'stage-call-count':patch(c,'s1',0)}
result={}
for name,lines in cases.items():
 artifact=a.out/(name+'.txt');artifact.write_text('\n'.join(lines)+'\n');out=a.out/(name+'.json');r=subprocess.run([sys.executable,str(Path(__file__).with_name('analyze-render.py')),str(a.stdout),'--artifact',str(artifact),'--out',str(out)],capture_output=True,text=True)
 (a.out/(name+'.stderr')).write_text(r.stderr);assert r.returncode!=0,name;result[name]=dict(rejected=True,returncode=r.returncode)
(a.out/'controls.json').write_text(json.dumps(dict(status='PASS_EIGHT_INVALID_RENDER_ARTIFACTS_REJECTED',controls=result),indent=2)+'\n');print('PASS_EIGHT_INVALID_RENDER_ARTIFACTS_REJECTED')
