"""Root-only completion of an already observed diagnostic ready halt."""
from pathlib import Path
import argparse, importlib.util, json, subprocess
p=argparse.ArgumentParser();p.add_argument('--stem',required=True);p.add_argument('--slot',type=int,required=True);a=p.parse_args()
root=Path(__file__).parent;lab=root.parent;work=lab/'wild-pool2-static-probe-design-v2';archive=lab/(a.stem+'-launch')
d=json.loads((archive/'owned-launch.json').read_text());proc=Path('/proc')/str(d['pid']);assert (proc/'exe').resolve()==Path(d['exe'])
assert d['profile'].encode() in (proc/'cmdline').read_bytes()
s=importlib.util.spec_from_file_location('reader',work/'savestate-reader-pinned.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
c=m.Pine(unix=d['socket']);assert c.status()==1 and c.read32(int(d['preload']['readyAddress'],16))==0x504f4f32;c.close()
state=Path(d['profile'])/'PCSX2/sstates'/f'vehicle-playground ({d["preload"]["pcsx2CRC"]}).{a.slot:02d}.p2s'
elf=Path(d['elf']);fixture=Path(d['fixture'])
subprocess.run(['python3',str(work/'savestate-reader-pinned.py'),'--unix',d['socket'],'--layout',str(lab/'pcsx2-savestate-controls/layout-linux64.json'),
 '--elf',str(elf),'--save-slot',str(a.slot),'--state',str(state),'--expect-pc','0x'+d['preload']['haltPC'],'--out',str(archive/'capture-report.json')],check=True)
subprocess.run(['python3',str(work/'verify-capture.py'),'--state',str(state),'--elf',str(elf),'--sym',str(elf.with_suffix('.elf.sym')),
 '--capture-report',str(archive/'capture-report.json'),'--cfg',str(archive/'pool2-probe.cfg'),'--profile',d['ini'],
 '--source-manifest',str(fixture/'target-source-manifest.json'),'--native-proof',d['nativeProof'],'--out',str(archive/'verified-capture.json')],check=True)
subprocess.run(['python3',str(root/'finish-probe-v2.py'),'--stem',a.stem,'--slot',str(a.slot)],check=True)
