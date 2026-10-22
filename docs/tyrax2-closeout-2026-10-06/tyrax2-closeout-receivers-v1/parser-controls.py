"""Pure synthetic protocol controls: not runtime/performance evidence."""
from pathlib import Path
import argparse,json,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists();a.out.mkdir();root=Path(__file__).parent;results=[]
def line(label,**fields):return 'LOG: '+label+' '+' '.join(f'{k}={v}'for k,v in fields.items())+'\n'
def fixture(order,kind):
 stdout=line('CLOSEOUTCONFIG',order=order,kind=kind,valid=1,phases=3,phaseFrames=1800,taxFirst=800,taxLoops=320,sampleFirst=900,sampleCount=128,ordinaryClocks=1,addedWaits=0,frameProfile=0,hardwareTrace=0,devkit=0,sampleBytes=6144,chunkBytes=420);artifact=''
 for phase in range(3):
  middle=(phase==1)!=(order==1);players=int(kind==2 or(kind==0 and middle));hidden=int(kind==2 and middle);sampler=int(kind!=1 or middle)
  stdout+=line('CLOSEOUTPHASE',phase=phase,first=phase*1800,players=players,geometryHidden=hidden,sampler=sampler,countReads=262 if sampler else 6)
  for offset in(750,1155):stdout+=line('CLOSEOUTCONTEXT',phase=phase,offset=offset,players=players,scene=0,night=1,paused=1,hourZero=1,vehicleDriver=108,sceneGeneration=1,video=0,display=1,depth=0,width=512,height=448,refreshMilliHz=59940,requested=1,dtBits='3c888888',clockBits='41200000',positionX='00000000',positionY='4028b436',positionZ='c2a19b64',lookX='00000000',lookY='3f9a5e32',lookZ='c2949b64',geometryMatched=11 if kind==2 else 0,geometryReady=1 if kind==2 else 0,geometryRejected=0,visibleMask=0 if hidden else 2047,activeMask=2047,savedMask=2047,valid=1)
  for i in range(5):artifact+=line('CLOSEOUTCHUNK',phase=phase,i=i,first=phase*1800+800+i*64,loops=64,elapsed=64*5013504,pacing=64*294912,totalFlips=64,renderedFlips=64,syntheticFlips=0)
  for i in range(128 if sampler else 0):artifact+=line('CLOSEOUTRAW',phase=phase,i=i,frame=phase*1800+900+i,entryPeriod=5013504,wholeLoop=5013504,pacing=294912,totalFlips=1,renderedFlips=1,syntheticFlips=0,presentPeriod=5013504,renderedPeriod=5013504,lastKind=1,sequence=1,context=0)
  artifact+=line('CLOSEOUTWINDOW',phase=phase,sampler=sampler,raw=128 if sampler else 0,chunks=5,valid=1)
 stdout+=line('CLOSEOUTDONE',order=order,kind=kind,valid=1,loops=5400);return stdout,artifact
def run(name,s,f,reject=False):
 dest=a.out/name;dest.mkdir();(dest/'stdout.log').write_text(s,encoding='utf8');(dest/'artifact.txt').write_text(f,encoding='utf8');r=subprocess.run([sys.executable,str(root/'analyze.py'),str(dest/'stdout.log'),'--artifact',str(dest/'artifact.txt'),'--out',str(dest/'result.json')],capture_output=True);(dest/'parser.stdout').write_bytes(r.stdout);(dest/'parser.stderr').write_bytes(r.stderr);passed=(r.returncode!=0)==reject;results.append(dict(name=name,expectedReject=reject,exitCode=r.returncode,passed=passed));assert passed,name
for kind in range(3):
 for order in range(2):run(f'positive-kind{kind}-order{order}',*fixture(order,kind))
s,f=fixture(0,0);run('timestamp-positive',''.join('[ 6.4840] '+l+'\n'for l in s.splitlines()),f)
run('missing-done',s[:s.index('LOG: CLOSEOUTDONE')],f,True)
run('missing-raw',s,'\n'.join(l for l in f.splitlines()if not l.startswith('LOG: CLOSEOUTRAW phase=0 i=0 '))+'\n',True)
run('pal-reject',s.replace('refreshMilliHz=59940','refreshMilliHz=50000'),f,True)
run('bad-depth',s.replace('depth=0','depth=3'),f,True)
run('duplicate-phase',s+next(l+'\n'for l in s.splitlines()if l.startswith('LOG: CLOSEOUTPHASE')),f,True)
run('missing-context','\n'.join(l for l in s.splitlines()if not l.startswith('LOG: CLOSEOUTCONTEXT phase=0 offset=750'))+'\n',f,True)
run('wrong-count-read',s.replace('countReads=262','countReads=263'),f,True)
run('bad-cold-camera',s.replace('positionY=4028b436','positionY=4028b437'),f,True)
run('phase-output-invalid',s,f.replace('valid=1','valid=0'),True)
run('unsigned-half-range',s,f.replace('wholeLoop=5013504','wholeLoop=2147483648'),True)
s,f=fixture(0,2);run('geometry-not-hidden',s.replace('visibleMask=0 ','visibleMask=2047 '),f,True)
(a.out/'proof.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_CLOSEOUT_PARSER_CONTROLS',cases=results,syntheticOnly=True,actualRuntimeQualified=False),indent=2)+'\n',encoding='utf8');print('PASS_SYNTHETIC_CLOSEOUT_PARSER_CONTROLS',len(results))
