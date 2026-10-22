from pathlib import Path
import hashlib,json,re,subprocess
b=Path('/mnt/f/Projects/tyrax2-lab-20261001');f=b/'pmu-scene-physical-v2';o=b/'pmu-wrapper-compile-root-v1';assert not o.exists();o.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
h=f/'tyra/engine/inc/debug/night_ablation.hpp';before=sha(h);cpp=o/'wrappers.cpp';obj=o/'wrappers.o'
s='#include "debug/night_ablation.hpp"\n'
for name,ret in [('readPcr','uint32_t'),('readI','uint32_t'),('readD','uint32_t'),('configureStopped','void'),('resetEnable','void'),('stopOwned','void')]:
 s+=f'extern "C" __attribute__((noinline,used)) {ret} pmu_export_{name}(){{'+('return ' if ret!='void' else '')+f'NightPMU::{name}();'+'}\n'
cpp.write_bytes(s.encode());t=Path('/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f/ee/bin');cc=t/'mips64r5900el-ps2-elf-g++';dump=t/'mips64r5900el-ps2-elf-objdump'
commands=[[str(cc),'-std=c++17','-O2','-G0','-I',str(f/'tyra/engine/inc'),'-c',str(cpp),'-o',str(obj)],[str(dump),'-d',str(obj)]]
for i,cmd in enumerate(commands):
 p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(o/f'command-{i}.log').write_bytes(p.stdout);assert p.returncode==0,(cmd,p.returncode,p.stdout.decode(errors='replace'))
text=(o/'command-1.log').read_text();counts={op:len(re.findall(r'\b'+re.escape(op)+r'\b',text)) for op in ('mfps','mfpc','mtpc','mtps','sync.p','mtc0')};assert counts=={'mfps':1,'mfpc':2,'mtpc':2,'mtps':3,'sync.p':7,'mtc0':0},counts;assert sha(h)==before
proof={'status':'ROOT_ACTUAL_R5900_COMPILE_ONLY_PMU_WRAPPER_OPCODES','sourceManifestSha256':sha(f/'target-source-manifest.json'),'commands':commands,'counts':counts,'pins':{str(p):sha(p) for p in [h,cpp,obj,cc,dump,o/'command-0.log',o/'command-1.log']},'runtimeReleaseAccepted':False,'limitations':'Actual standalone object opcodes, not linked application placement. No runtime, event-rate/lifetime qualification or miss/cost claim; fixture remains unfrozen and blocked.'}
(o/'proof.json').write_bytes((json.dumps(proof,indent=2)+'\n').encode());print(proof['status'],counts)
