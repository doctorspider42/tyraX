"""Root-only fresh diagnostic launch; no inherited profile, outputs or authority."""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess

p=argparse.ArgumentParser()
p.add_argument('--arm',type=int,choices=(0,1),required=True)
p.add_argument('--vertices',type=int,choices=(96,192),required=True)
p.add_argument('--repeats',type=int,choices=(1,2,3),required=True)
p.add_argument('--stem',required=True)
a=p.parse_args()
assert re.fullmatch('pool2-static-[a-z0-9-]+',a.stem)
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001')
fixture=lab/'wild-pool2-static-probe-physical-v1'
game=fixture/'game/bin'
archive=lab/(a.stem+'-launch')
run=Path('/home/spider')/a.stem
assert not archive.exists() and not run.exists()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
manifest=json.loads((fixture/'target-source-manifest.json').read_text())
assert manifest['frozen'] and len(manifest['files'])==500
for rel,h in manifest['files'].items():assert sha(fixture/rel)==h,rel
native=lab/'wild-pool2-static-probe-native-review-v1/proof.json'
n=json.loads(native.read_text())
assert n['status'].startswith('PASS_') and not n.get('blockers'),n['status']
provenance=json.loads((fixture/'root-native-provenance.json').read_text())
elf=game/'vehicle-playground.elf'
assert provenance['build_exit_code']==0 and provenance['selected_elf_sha256']==sha(elf)
assert provenance['target_source_manifest_sha256']==sha(fixture/'target-source-manifest.json')
assert n['actualELFSha256']==sha(elf) and n['actualSymbolSha256']==sha(elf.with_suffix('.elf.sym'))
assert n['sourceManifestSha256']==sha(fixture/'target-source-manifest.json') and n['diagnosticOnly']
for rel,h in json.loads((fixture/'runtime-assets-manifest.json').read_text())['files'].items():assert sha(game/rel)==h,rel
archive.mkdir()
cfg=game/'pool2-probe.cfg'
cfg.write_bytes(f'{a.arm} {a.vertices} {a.repeats}\n'.encode())
shutil.copy2(cfg,archive/cfg.name)
root=Path('/home/spider/tyrax2-debugger-linux-v1')
ini=run/'profile/PCSX2/inis/PCSX2.ini'
ini.parent.mkdir(parents=True)
s=(root/'profile/PCSX2/inis/PCSX2.ini').read_text()
for key,value in [('PINESlot','28205'),('vuThread','false'),('StartPaused','false')]:
 s,count=re.subn(r'^'+key+r'\s*=.*$',key+' = '+value,s,flags=re.M)
 assert count==1,(key,count)
assert re.search(r'^TogglePause\s*=\s*Keyboard/Space\s*$',s,re.M)
audio=re.search(r'^\[SPU2/Output\]\n(.*?)(?=^\[|\Z)',s,re.M|re.S)
assert audio
block,n=re.subn(r'^Backend\s*=.*$','Backend = Null',audio.group(0),flags=re.M)
assert n==1
s=s[:audio.start()]+block+s[audio.end():]
ini.write_text(s)
settings=ini.parent/'debuggersettings'
settings.mkdir()
preload=archive/'preload'
subprocess.run(['python3',str(lab/'wild-pool2-breakpoint-preload-v1/prepare-preload.py'),
 '--elf',str(elf),'--sym',str(elf.with_suffix('.elf.sym')),
 '--debugger-settings-dir',str(settings),'--out',str(preload)],check=True)
b=json.loads((preload/'proof.json').read_text())
payload=preload/Path(b['destination']).name
shutil.copy2(payload,settings/payload.name)
assert sha(settings/payload.name)==b['payloadSha256']
shutil.copy2(ini,archive/'PCSX2-before.ini')
slot=28205
socket=Path(f'/mnt/wslg/runtime-dir/pcsx2.sock.{slot}')
assert not socket.exists(),'PINE occupied; inspect owner'
exe=root/'squashfs-root/usr/bin/pcsx2-qt'
assert sha(exe)=='c4629baf9c5daca45a9df6ad1ee84a188aeb025627b822a3ea62bdc48b23da18'
env=os.environ.copy()
env.update(DISPLAY=':93',QT_QPA_PLATFORM='xcb',LIBGL_ALWAYS_SOFTWARE='1')
env.pop('WAYLAND_DISPLAY',None)
args=[str(root/'squashfs-root/AppRun'),'-datapath',str(run/'profile'),
 '-logfile',str(run/'emulator.log'),'-debugger','-elf',str(elf)]
proc=subprocess.Popen(args,cwd=game,env=env,stdout=(run/'stdout.log').open('wb'),stderr=subprocess.STDOUT,start_new_session=True)
record=dict(pid=proc.pid,args=args,exe=str(exe),exeSha256=sha(exe),fixture=str(fixture),stem=a.stem,
 arm=a.arm,vertices=a.vertices,repeats=a.repeats,profile=str(run/'profile'),ini=str(ini),
 socket=str(socket),elf=str(elf),elfSha256=sha(elf),symbolSha256=sha(elf.with_suffix('.elf.sym')),
 sourceManifestSha256=sha(fixture/'target-source-manifest.json'),nativeProofSha256=sha(native),
 nativeProof=str(native),cfgSha256=sha(cfg),preload=b,profileBeforeSha256=sha(ini),
 helperSha256=sha(Path(__file__)),deviceTimingAccepted=False)
(archive/'owned-launch.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2),flush=True)
