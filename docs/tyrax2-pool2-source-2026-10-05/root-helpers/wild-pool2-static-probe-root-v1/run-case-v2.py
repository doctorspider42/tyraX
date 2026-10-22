"""Root-only fixed-case execution; validated entry snapshot authorizes one observed Run."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, struct, subprocess, time

p=argparse.ArgumentParser()
p.add_argument('--arm',type=int,choices=(0,1),required=True)
p.add_argument('--vertices',type=int,choices=(96,192),required=True)
p.add_argument('--repeats',type=int,choices=(1,2,3),required=True)
p.add_argument('--slot',type=int,required=True)
a=p.parse_args();assert 162<=a.slot<=171
root=Path(__file__).parent;lab=root.parent;work=lab/'wild-pool2-static-probe-design-v2'
stem=f'pool2-static-{"a" if a.vertices==96 else "b"}{a.vertices}-arm{a.arm}-repeat{a.repeats}-20261005'
archive=lab/(stem+'-launch');assert not archive.exists()
launchLog=lab/(stem+'-launcher.log');assert not launchLog.exists()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
subprocess.run(['python3',str(root/'launch-probe-v3.py'),'--arm',str(a.arm),'--vertices',str(a.vertices),
 '--repeats',str(a.repeats),'--stem',stem],stdout=launchLog.open('wb'),stderr=subprocess.STDOUT,check=True)
d=json.loads((archive/'owned-launch.json').read_text());elf=Path(d['elf'])
spec=importlib.util.spec_from_file_location('reader',work/'savestate-reader-pinned.py');reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
entry=struct.unpack_from('<I',elf.read_bytes(),24)[0];log=Path(d['profile']).parent/'emulator.log'
deadline=time.monotonic()+45
while time.monotonic()<deadline:
 try:
  text=log.read_text()
  if 'POOL2PROBE_REJECT' in text or 'POOL2PROBE_READY' in text:raise RuntimeError('Unexpected completed/rejected startup')
  if f'EntryPoint = 0x{entry:08X}' in text:
   client=reader.Pine(unix=d['socket']);status=client.status();client.close()
   if status==1:break
 except (OSError,TimeoutError):pass
 time.sleep(.1)
else:raise RuntimeError('No owned ELF entry pause; retain process for diagnosis')
states=Path(d['profile'])/'PCSX2/sstates';crc=d['preload']['pcsx2CRC']
entryState=states/f'vehicle-playground ({crc}).140.p2s'
common=['--unix',d['socket'],'--layout',str(lab/'pcsx2-savestate-controls/layout-linux64.json'),'--elf',str(elf)]
subprocess.run(['python3',str(work/'savestate-reader-pinned.py'),*common,'--save-slot','140',
 '--state',str(entryState),'--expect-pc',hex(entry),'--out',str(archive/'entry-capture-report.json')],check=True)
e=json.loads((archive/'entry-capture-report.json').read_text())
assert e['status']=='PASS_exact_tag_paused_state_decode_text_identity' and e['registers']['pc']==entry
env={**os.environ,'DISPLAY':':93'}
tree=subprocess.check_output(['xwininfo','-root','-tree'],env=env,text=True)
windows=re.findall(r'(0x[0-9a-fA-F]+) "PCSX2 Debugger"',tree);assert len(windows)==1
window=windows[0];prop=subprocess.check_output(['xprop','-id',window,'_NET_WM_PID'],env=env,text=True)
assert re.search(r'=\s*'+str(d['pid'])+r'\s*$',prop)
screen=archive/'entry-debugger.png';subprocess.run(['import','-window',window,str(screen)],env=env,check=True)
obs=dict(pid=d['pid'],window=window,display=':93',width=1322,height=974,x=110,y=47,
 observedAction='Run',observedPhase='ELF-entry-pause',observedPC=hex(entry),
 screenshot=str(screen),screenshotSha256=sha(screen),entryCaptureSha256=sha(archive/'entry-capture-report.json'),
 authority='Actual source-bound paused entry SaveState plus pinned owned debugger geometry; Run toolbar region observed in preceding root cases.')
(archive/'root-observed-entry-run.json').write_text(json.dumps(obs,indent=2)+'\n')
subprocess.run(['python3',str(lab/'wild-pool2-observed-run-v1/owned-observed-run-v2.py'),
 '--owned-launch',str(archive/'owned-launch.json'),'--observation',str(archive/'root-observed-entry-run.json'),
 '--out',str(archive/'root-run-action.json'),'--execute'],check=True)
deadline=time.monotonic()+60
while time.monotonic()<deadline:
 text=log.read_text()
 assert 'POOL2PROBE_REJECT' not in text,'Probe rejected; preserve diagnostics'
 if 'POOL2PROBE_READY' in text:
  client=reader.Pine(unix=d['socket']);status=client.status();ready=client.read32(int(d['preload']['readyAddress'],16));client.close()
  if status==1 and ready==0x504f4f32:break
 time.sleep(.1)
else:raise RuntimeError('No completed ready halt; retain process for diagnosis')
state=states/f'vehicle-playground ({crc}).{a.slot:02d}.p2s'
subprocess.run(['python3',str(work/'savestate-reader-pinned.py'),*common,'--save-slot',str(a.slot),
 '--state',str(state),'--expect-pc','0x'+d['preload']['haltPC'],'--out',str(archive/'capture-report.json')],check=True)
fixture=Path(d['fixture'])
subprocess.run(['python3',str(work/'verify-capture.py'),'--state',str(state),'--elf',str(elf),
 '--sym',str(elf.with_suffix('.elf.sym')),'--capture-report',str(archive/'capture-report.json'),
 '--cfg',str(archive/'pool2-probe.cfg'),'--profile',d['ini'],
 '--source-manifest',str(fixture/'target-source-manifest.json'),'--native-proof',d['nativeProof'],
 '--out',str(archive/'verified-capture.json')],check=True)
subprocess.run(['python3',str(root/'finish-probe-v2.py'),'--stem',stem,'--slot',str(a.slot)],check=True)
print('Completed fixed case',a.vertices,a.arm,a.repeats,flush=True)
