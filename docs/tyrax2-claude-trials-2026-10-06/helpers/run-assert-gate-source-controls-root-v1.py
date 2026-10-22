from pathlib import Path
import subprocess,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');out=lab/'assert-gate-source-controls-v1'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
commands=[['wsl','-d','Ubuntu','--','g++','-std=c++17','-O2','/mnt/f/Projects/tyrax2-lab-20261001/assert-gate-source-controls-v1/assertions.cpp','-o','/mnt/f/Projects/tyrax2-lab-20261001/assert-gate-source-controls-v1/checks'],['wsl','-d','Ubuntu','--','/mnt/f/Projects/tyrax2-lab-20261001/assert-gate-source-controls-v1/checks']]
results=[]
for i,cmd in enumerate(commands):
 log=out/f'root-command-{i}.log';assert not log.exists();r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log.write_bytes(r.stdout);results.append(dict(command=cmd,exitCode=r.returncode,logSha256=sha(log)));assert r.returncode==0,r.stdout.decode(errors='replace')
assert b'PASS_EXTRACTED_TWELVE_ASSERTIONS_12_INVALID_3_VALID' in (out/'root-command-1.log').read_bytes()
proof=dict(status='PASS_ROOT_EXECUTED_EXTRACTED_ORIGINAL_ASSERTIONS_HOST_ONLY',commands=results,sourceSha256=sha(out/'assertions.cpp'),validCases=3,invalidFirstFailureCases=12,targetABIPredicted=False,wholeRenderNegativeInputSafetyEstablished=False)
(out/'root-proof.json').write_bytes((json.dumps(proof,indent=2)+'\n').encode());print(proof['status'])
