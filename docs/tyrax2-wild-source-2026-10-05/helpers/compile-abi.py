"""Root-only actual R5900 header layout check; does not execute target code."""
from pathlib import Path
import argparse, hashlib, json, struct, subprocess
p=argparse.ArgumentParser();p.add_argument('--fixture',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
assert not a.out.exists();a.out.mkdir()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
headers=[a.fixture/'tyra/engine/inc/debug'/f for f in ('night_sampler.hpp','night_ablation.hpp','night_plan.hpp')]
cpp=a.out/'layout.cpp';cpp.write_bytes(b'#include "debug/night_sampler.hpp"\n#include "debug/night_ablation.hpp"\nextern "C" const unsigned night_layout[] = {sizeof(NightSampler::Sample), sizeof(NightSampler::samples), sizeof(NightSampler::Chunk), sizeof(NightSampler::chunks), sizeof(NightAblation::Counter), sizeof(NightAblation::commonCounter), sizeof(NightAblation::extraCounter), sizeof(NightAblation::WildCounter)};\n')
binpath=Path('/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f/ee/bin');compiler=binpath/'mips64r5900el-ps2-elf-g++';objcopy=binpath/'mips64r5900el-ps2-elf-objcopy'
obj=a.out/'layout.o';raw=a.out/'layout.bin'
commands=[[str(compiler),'-std=c++17','-O2','-G0','-I',str(a.fixture/'tyra/engine/inc'),'-c',str(cpp),'-o',str(obj)],[str(objcopy),'-O','binary','-j','.rodata',str(obj),str(raw)]]
for i,cmd in enumerate(commands):
 r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(a.out/f'command-{i}.log').write_bytes(r.stdout);assert r.returncode==0,(cmd,r.returncode)
sizes=struct.unpack('<8I',raw.read_bytes());assert sizes==(48,6144,28,420,16,112,48,44),sizes
record={'status':'PASS_ACTUAL_R5900_COMPILE_ONLY_NIGHT_LAYOUT','sizes':dict(zip(('Sample','samples','Chunk','chunks','Counter','commonCounter','extraCounter','WildCounter'),sizes)),'commands':commands,'inputHashes':{str(f):sha(f)for f in headers+[cpp,obj,raw,compiler,objcopy]},'runtimeOrFullApplicationFootprintAccepted':False,'helperSha256':sha(Path(__file__))}
(a.out/'proof.json').write_bytes((json.dumps(record,indent=2)+'\n').encode());print(record['status'],sizes)
