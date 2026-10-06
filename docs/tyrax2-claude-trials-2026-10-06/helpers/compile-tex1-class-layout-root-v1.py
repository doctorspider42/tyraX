from pathlib import Path
import json,hashlib,struct,subprocess
b=Path('/mnt/f/Projects/tyrax2-lab-20261001');o=b/'tex1-owned-class-layout-root-v1';assert not o.exists();o.mkdir()
t=Path('/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f');compiler=t/'ee/bin/mips64r5900el-ps2-elf-g++';objcopy=t/'ee/bin/mips64r5900el-ps2-elf-objcopy';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();rows={}
for name,engine in [('baseline',Path('/mnt/f/Projects/tyra-editor/vendor/tyra/engine')),('candidate',b/'tex1-owned-physical-v1/tyra/engine')]:
 cpp=o/(name+'.cpp');obj=o/(name+'.o');raw=o/(name+'.bin');cpp.write_bytes(b'#include "renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp"\nextern "C" const unsigned layout[] = {sizeof(Tyra::StaPipQBufferRenderer),alignof(Tyra::StaPipQBufferRenderer)};\n')
 cmds=[[str(compiler),'-std=c++17','-D_EE','-O2','-G0','-I',str(engine/'inc'),'-I',str(t/'ps2sdk/ee/include'),'-I',str(t/'ps2sdk/common/include'),'-I',str(t/'ps2sdk/ports/include'),'-c',str(cpp),'-o',str(obj)],[str(objcopy),'-O','binary','-j','.rodata',str(obj),str(raw)]]
 for i,cmd in enumerate(cmds):
  p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(o/f'{name}-command-{i}.log').write_bytes(p.stdout);assert p.returncode==0,(cmd,p.returncode,p.stdout.decode(errors='replace'))
 values=struct.unpack('<2I',raw.read_bytes());rows[name]={'bytes':values[0],'alignment':values[1],'commands':cmds,'pins':{str(p):sha(p) for p in [engine/'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp',cpp,obj,raw,compiler,objcopy]}}
assert rows['candidate']['bytes']>rows['baseline']['bytes'] and rows['candidate']['alignment']==rows['baseline']['alignment']
proof={'status':'ROOT_ACTUAL_R5900_CLASS_LAYOUT_GROWTH_RECORDED','layouts':rows,'growthBytes':rows['candidate']['bytes']-rows['baseline']['bytes'],'limitations':'C++ class growth only; target whole build must use matching headers. Not packet/VU ABI or runtime cost. No object binary redistributed.'}
(o/'proof.json').write_bytes((json.dumps(proof,indent=2)+'\n').encode());print(proof['status'],proof['growthBytes'])
