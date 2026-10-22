"""Snapshot/compile A41 ownership model; no emulator or hardware-performance claim."""
import argparse,hashlib,json,re,shutil,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('--ee-cxx',type=Path,required=True)
p.add_argument('--adapter',action='store_true')
a=p.parse_args();out=a.out.resolve();assert not out.exists()
out.mkdir(parents=True)
repo=Path(__file__).resolve().parents[1]
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
files=('tyrax2-hybrid-copy-protocol.hpp','verify-hybrid-copy-protocol.cpp','verify-hybrid-copy-protocol.py')
if a.adapter:
 files=('tyrax2-hybrid-copy-protocol.hpp','tyrax2-hybrid-copy-adapter.hpp','tyrax2-hybrid-copy-ee.hpp',
        'verify-hybrid-copy-adapter.cpp','verify-hybrid-copy-packet.cpp','verify-hybrid-copy-protocol.py')
pins={}
for name in files:
 shutil.copy2(repo/'tools'/name,out/name);pins[name]=sha(out/name)
expected='HYBRIDCOPY guards=144 schedules=161280 lifecycle=PASS late-copy-detection=PASS\n'
if a.adapter:expected='HYBRIDADAPTER guards=46 old-copy-FINISH=PASS IRQ-publication=PASS fallback=PASS\n'
def run(name,args):
 result=subprocess.run(args,text=True,capture_output=True)
 (out/f'{name}.stdout.log').write_text(result.stdout)
 (out/f'{name}.stderr.log').write_text(result.stderr)
 assert result.returncode==0,(name,result.returncode,result.stderr)
 return result.stdout
hostVersion=run('host-version',['g++','--version'])
eeVersion=run('ee-version',[str(a.ee_cxx),'--version'])
source=str(out/('verify-hybrid-copy-adapter.cpp' if a.adapter else 'verify-hybrid-copy-protocol.cpp'))
common=['-std=c++17','-Wall','-Wextra','-Werror']
commands=[]
for label,flags in [('optimized',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 cmd=['g++',*common,*flags,source,'-o',str(out/label)]
 commands.append(cmd);run(f'{label}-compile',cmd)
 assert run(f'{label}-run',[str(out/label)])==expected
validatorSha=None
if a.adapter:
 ee=(out/'tyrax2-hybrid-copy-ee.hpp').read_text()
 start=ee.index('  bool validPacket(');end=ee.index('  void publishPacket(',start)
 method=ee[start:end];validatorSha=hashlib.sha256(method.encode()).hexdigest()
 template=(out/'verify-hybrid-copy-packet.cpp').read_text()
 assert template.count('  // ACTUAL_VALIDATOR')==1
 generated=out/'packet-test-generated.cpp'
 generated.write_text(template.replace('  // ACTUAL_VALIDATOR',method))
 for label,flags in [('optimized',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
  cmd=['g++',*common,*flags,str(generated),'-o',str(out/f'packet-{label}')]
  commands.append(cmd);run(f'packet-{label}-compile',cmd)
  assert run(f'packet-{label}-run',[str(out/f'packet-{label}')])=='HYBRIDPACKET cases=644 actual-tail-validator=PASS\n'
obj=out/'protocol-ee.o'
targetFlags=[];sdkPins={}
if a.adapter:
 sdk=a.ee_cxx.resolve().parents[2]/'ps2sdk'
 targetFlags=['-D_EE','-I',str(sdk/'ee/include'),'-I',str(sdk/'common/include')]
 for rel in ('ee/include/kernel.h','common/include/tamtypes.h'):
  sdkPins[rel]=sha(sdk/rel)
cmd=[str(a.ee_cxx),*common,*targetFlags,'-O2','-c',source,'-o',str(obj)]
commands.append(cmd);run('ee-compile',cmd)
raw=obj.read_bytes();assert raw[:6]==b'\x7fELF\x01\x01'
assert struct.unpack_from('<HH',raw,16)==(1,8),'EE relocatable MIPS object required'
irqSymbols=[]
if a.adapter:
 objdump=a.ee_cxx.with_name(a.ee_cxx.name.removesuffix('g++')+'objdump')
 disassembly=run('ee-disassembly',[str(objdump),'-drC',str(obj)])
 for method in ('blankStartISR','blankEndISR','copyFinishISR'):
  sections=[part for part in disassembly.split('Disassembly of section ')
            if re.search(rf'<Tyra::HybridCopyAdapter<Tyra::HybridCopyEE>::{method}\(\)>:',part)]
  assert len(sections)==1
  assert not re.search(r'R_MIPS_\w+\s+(SyncDCache|DIntr|EIntr|FlushCache|malloc|free)\b',sections[0])
  irqSymbols.append(method)
assert all(sha(out/name)==digest and sha(repo/'tools'/name)==digest for name,digest in pins.items())
report={'valid':True,'sources':pins,'commands':commands,'hostCompiler':hostVersion,
 'eeCompiler':eeVersion,'eeObjectSha256':sha(obj),'guards':46 if a.adapter else 144,
 'schedulesPerHostMode':0 if a.adapter else 161280,'mode':'adapter' if a.adapter else 'protocol',
 'sdkHeaderPins':sdkPins,'IRQWithoutDirectKernelHeapRelocations':irqSymbols,
 'actualPacketValidatorSha256':validatorSha,'packetCasesPerHostMode':644 if a.adapter else 0,
 'hostModes':['optimized','ASan+UBSan'],'fixedABI':{'protocolBytes':36,'readyBytes':12},
 'limits':['Isolated component, not integrated actual Core/queue target runtime.',
  'Main/ISR transitions require an external atomic critical section and memory publication.',
  'Readiness/copy-DMA/exclusive-FINISH predicates are modeled, not hardware-derived.',
  'Adapter real backend cross-compiles with MMIO; its indirect queue proof still needs a bound integration.',
  'IRQ relocation inspection checks direct references, not the full indirect caller/callee closure.',
  'Real copy-window callback is not bound: delayed ISR/end-handler status is not physical blank-budget proof.',
  'ISR claims must be called immediately at VBlank start; active blank alone does not establish a remaining copy budget.',
  'Cross-compilation is not target execution, cache/lifetime qualification or speedup.']}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
inventory={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
print(json.dumps({'valid':True,'mode':report['mode'],'guards':report['guards'],
 'schedulesPerMode':report['schedulesPerHostMode'],'eeCompile':True}))
