"""Host-only oracle, no PS2 compilation or launch."""
from pathlib import Path
import subprocess,json,hashlib,shutil
root=Path(__file__).resolve().parent;host=root/'host'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
compiler=shutil.which('g++');assert compiler
runs=[]
for opt in ['O0','O2']:
 exe=host/('oracle-'+opt+'.exe');args=[compiler,'-std=c++17','-'+opt,'-Wall','-Wextra','-Werror','-static','-I',str(host),str(host/'oracle.cpp'),'-o',str(exe)]
 compile=subprocess.run(args,capture_output=True,text=True)
 (host/(opt+'-compile.log')).write_text(compile.stdout+compile.stderr,encoding='utf-8',newline='\n');assert compile.returncode==0,compile.stderr
 run=subprocess.run([str(exe)],capture_output=True,text=True)
 (host/(opt+'-run.log')).write_text(run.stdout+run.stderr,encoding='utf-8',newline='\n');assert run.returncode==0 and run.stdout.startswith('PASS '),run.stdout+run.stderr
 runs.append({'optimization':opt,'compileCommand':args,'compileExit':compile.returncode,'runCommand':[str(exe)],'runExit':run.returncode,'stdout':run.stdout,'stderr':run.stderr,'executableSha256':sha(exe)})
sourcePins={str(p):sha(p) for p in [root/'source/game/src/gen/game_lighting.gen.cpp',root/'pool-color-split.patch',Path(__file__)]+list(host.rglob('*.hpp'))+[host/'tyra',host/'oracle.cpp',host/'original.inc',host/'candidate.inc']}
artifacts={str(p):sha(p) for p in host.iterdir() if p.suffix in ['.exe','.log']}
proof={'status':'PASS_HOST_SAME_ELF_POOL_CACHE_EXTRACTED_DUAL_ARM_BYTE_VERSION_REASON_CONTROLS_ONLY','runs':runs,'sourcePins':sourcePins,'artifactSha256':artifacts,'actualGeneratedBagArrayHeader':True,'actualColorHeaderAndConstructor':True,'rendererAndVec4HostShims':True,'nativeAccepted':False,'runtimeAccepted':False,'physicalActivationMeasured':False,'performanceGainClaimed':False}
(root/'host-proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8',newline='\n');print(json.dumps(runs,indent=2))
