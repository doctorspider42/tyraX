from pathlib import Path
import argparse,subprocess,sys,json,re
p=argparse.ArgumentParser();p.add_argument('stdout',type=Path);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists();a.out.mkdir()
original=a.artifact.read_text();lines=original.splitlines();j=next(i for i,s in enumerate(lines)if s.startswith('LOG: CLOSEOUTBAL phase=0 i=0 '))
def field(s,key,value):return re.sub(r'\b'+key+r'=\d+',key+'='+str(value),s,count=1)
tests={'missing-row':lambda ls:ls[:j]+ls[j+1:],'duplicate-row':lambda ls:ls+[ls[j]],'bad-marker-order':lambda ls:ls[:j]+[field(ls[j],'renderStart',0)]+ls[j+1:],'pacing-mismatch':lambda ls:ls[:j]+[field(ls[j],'pace2',0)]+ls[j+1:],'read-count':lambda ls:ls[:j]+[field(ls[j],'reads',0)]+ls[j+1:],'nested-wait':lambda ls:ls[:j]+[field(ls[j],'nested',1)]+ls[j+1:],'job-owner':lambda ls:ls[:j]+[field(ls[j],'firstOwner',0)]+ls[j+1:],'negative-exclusive':lambda ls:ls[:j]+[field(ls[j],'wait20',2147483647)]+ls[j+1:]}
results={}
for name,mutate in tests.items():
 artifact=a.out/(name+'.txt');artifact.write_text('\n'.join(mutate(lines))+'\n');out=a.out/(name+'.json')
 c=subprocess.run([sys.executable,str(Path(__file__).with_name('analyze-balance.py')),str(a.stdout),'--artifact',str(artifact),'--out',str(out)],capture_output=True,text=True)
 (a.out/(name+'.stdout')).write_text(c.stdout);(a.out/(name+'.stderr')).write_text(c.stderr);assert c.returncode!=0,name
 results[name]={'rejected':True,'returncode':c.returncode}
(a.out/'controls.json').write_text(json.dumps(dict(status='PASS_EIGHT_INVALID_BALANCE_ARTIFACTS_REJECTED',controls=results),indent=2)+'\n');print('PASS_EIGHT_INVALID_BALANCE_ARTIFACTS_REJECTED')
