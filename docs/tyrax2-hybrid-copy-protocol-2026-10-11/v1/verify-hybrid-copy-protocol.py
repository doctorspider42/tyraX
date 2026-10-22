"""Snapshot/compile A41 ownership model; no emulator or hardware-performance claim."""
import argparse,hashlib,json,shutil,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('--ee-cxx',type=Path,required=True)
a=p.parse_args();out=a.out.resolve();assert not out.exists()
out.mkdir(parents=True)
repo=Path(__file__).resolve().parents[1]
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
files=('tyrax2-hybrid-copy-protocol.hpp','verify-hybrid-copy-protocol.cpp','verify-hybrid-copy-protocol.py')
pins={}
for name in files:
 shutil.copy2(repo/'tools'/name,out/name);pins[name]=sha(out/name)
expected='HYBRIDCOPY guards=144 schedules=161280 lifecycle=PASS late-copy-detection=PASS\n'
def run(name,args):
 result=subprocess.run(args,text=True,capture_output=True)
 (out/f'{name}.stdout.log').write_text(result.stdout)
 (out/f'{name}.stderr.log').write_text(result.stderr)
 assert result.returncode==0,(name,result.returncode,result.stderr)
 return result.stdout
hostVersion=run('host-version',['g++','--version'])
eeVersion=run('ee-version',[str(a.ee_cxx),'--version'])
source=str(out/'verify-hybrid-copy-protocol.cpp')
common=['-std=c++17','-Wall','-Wextra','-Werror']
commands=[]
for label,flags in [('optimized',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 cmd=['g++',*common,*flags,source,'-o',str(out/label)]
 commands.append(cmd);run(f'{label}-compile',cmd)
 assert run(f'{label}-run',[str(out/label)])==expected
obj=out/'protocol-ee.o'
cmd=[str(a.ee_cxx),*common,'-O2','-c',source,'-o',str(obj)]
commands.append(cmd);run('ee-compile',cmd)
raw=obj.read_bytes();assert raw[:6]==b'\x7fELF\x01\x01'
assert struct.unpack_from('<HH',raw,16)==(1,8),'EE relocatable MIPS object required'
assert all(sha(out/name)==digest and sha(repo/'tools'/name)==digest for name,digest in pins.items())
report={'valid':True,'sources':pins,'commands':commands,'hostCompiler':hostVersion,
 'eeCompiler':eeVersion,'eeObjectSha256':sha(obj),'guards':144,'schedulesPerHostMode':161280,
 'hostModes':['optimized','ASan+UBSan'],'fixedABI':{'protocolBytes':36,'readyBytes':12},
 'limits':['Isolated protocol, not actual Core/queue/MMIO target adapter.',
  'Main/ISR transitions require an external atomic critical section and memory publication.',
  'Readiness/copy-DMA/exclusive-FINISH predicates are modeled, not hardware-derived.',
  'ISR claims must be called immediately at VBlank start; active blank alone does not establish a remaining copy budget.',
  'Cross-compilation is not target execution, cache/lifetime qualification or speedup.']}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
inventory={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
print(json.dumps({'valid':True,'guards':144,'schedulesPerMode':161280,'eeCompile':True}))
