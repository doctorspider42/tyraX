from pathlib import Path
import subprocess,json,hashlib
root=Path('/mnt/f/Projects/tyrax2-lab-20261001/tyrax2-closeout-receivers-v1')
tool='/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f/ee/bin/mips64r5900el-ps2-elf-'
em=Path('/home/spider/.cache/tyrax/native/engines/6b2836217bebd236c5d83bf1/tyra/engine');gm=Path('/home/spider/.cache/tyrax/native/projects/a7d97586b2c6d84f1a8a7ea3')
objs=[em/'obj/renderer/core/paths/path1/vif1_queue.o',gm/'obj/gen/game_vehicles.gen.o']
results=[]
for obj in objs:
 sections=subprocess.check_output([tool+'objdump','-h',str(obj)],text=True)
 names=[l.split()[1]for l in sections.splitlines()if len(l.split())>2 and l.split()[1].startswith('.rodata')]
 dumps=''.join(subprocess.check_output([tool+'objdump','-s','-j',n,str(obj)],text=True) for n in names)
 symbols=subprocess.check_output([tool+'nm','-C',str(obj)],text=True)
 forbidden=['NATIVEVIF','ORDEREDFRAME','VEHCONTACT','VEH pos','VEH surf']
 assert all(x not in dumps for x in forbidden)
 if obj==objs[0]:
  assert 'getAvailableRAM' not in symbols
  asm=subprocess.check_output([tool+'objdump','-dr','-C',str(obj)],text=True)
  import re
  block=re.search(r'<Tyra::Vif1Queue::arenaFrameEnd\(\)>:(.*?)(?=\n[0-9a-f]+ <|\Z)',asm,re.S).group(1)
  assert 'printf' not in block and 'fflush' not in block and 'getAvailableRAM' not in block
  (root/'actual-arenaFrameEnd-v4.txt').write_text(block)
 results.append(dict(object=str(obj),sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),readOnlySections=names,forbiddenHealthyStringsAbsent=True,arenaReportFunctionsAbsent=obj==objs[0]))
(root/'quiet-object-proof-v4.json').write_text(json.dumps(dict(status='PASS_FOCUSED_NATIVE_QUIET_OBJECTS',objects=results,doesNotClaimNoELFFileIO=True,emulatorHotWindowIOPending=True),indent=2)+'\n')
print('PASS_FOCUSED_NATIVE_QUIET_OBJECTS')
