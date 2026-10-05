"""Root-only fresh emulator boot, preserving launch authority before execution."""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess
p=argparse.ArgumentParser(); p.add_argument('--fixture',type=Path,required=True); p.add_argument('--stem',required=True); p.add_argument('--kind',type=int,choices=(12,),required=True); p.add_argument('--order',type=int,choices=(0,1),required=True); p.add_argument('--restored',type=int,default=0); a=p.parse_args()
assert re.fullmatch('night-ablation-[a-z0-9-]+',a.stem)
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001'); fixture=a.fixture; game=fixture/'game/bin'; archive=lab/(a.stem+'-launch'); run=Path('/home/spider')/a.stem
assert fixture.parent==lab and not archive.exists() and not run.exists()
assert not (game/'night-ablation.log').exists() and not (game/'ps2link.run').exists()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
from runtime_guard import verify_fixture
verify_fixture(fixture)
manifest=json.loads((fixture/'target-source-manifest.json').read_text()); assert manifest['frozen']
for rel,digest in manifest['files'].items(): assert sha(fixture/rel)==digest,rel
native=json.loads((fixture/'root-native-provenance.json').read_text()); elf=game/'vehicle-playground.elf'; assert native['selected_elf_sha256']==sha(elf) and native['target_source_manifest_sha256']==sha(fixture/'target-source-manifest.json')
joint=0; assert a.kind==12 and a.restored==0
assert a.kind==12 and a.restored==0
(game/'night-ablation.cfg').write_bytes(f'{a.kind} {a.order} {joint} {a.restored}\n'.encode()); (game/'quiet-night.cfg').write_bytes(b'1\n')
archive.mkdir()
for file in ('night-ablation.cfg','quiet-night.cfg','vehicle-playground.elf','vehicle-playground.elf.sym'):shutil.copy2(game/file,archive/file)
for file in ('target-source-manifest.json','root-native-provenance.json','runtime-assets-manifest.json','root-source-freeze.json','root-runtime-authority.json'):shutil.copy2(fixture/file,archive/file)
assets=json.loads((fixture/'runtime-assets-manifest.json').read_text())['files']
for rel,digest in assets.items():assert sha(game/rel)==digest,rel
root=Path('/home/spider/tyrax2-debugger-linux-v1'); configdir=run/'profile/PCSX2/inis'; configdir.mkdir(parents=True)
slot=28205; assert not Path(f'/mnt/wslg/runtime-dir/pcsx2.sock.{slot}').exists(),'PINE slot occupied/stale: inspect exact owner'
s=(root/'profile/PCSX2/inis/PCSX2.ini').read_text();s,n=re.subn(r'^PINESlot\s*=.*$',f'PINESlot = {slot}',s,flags=re.M); assert n==1
s=re.sub(r'^(StartPaused|ShowOnStartup)\s*=.*$',r'\1 = false',s,flags=re.M)
# Host-only correctness profile: suppress known missing-ALSA SDL error modal.
audio=re.search(r'^\[SPU2/Output\]\n(.*?)(?=^\[|\Z)',s,re.M|re.S);assert audio
block,n=re.subn(r'^Backend\s*=.*$','Backend = Null',audio.group(0),flags=re.M);assert n==1
s=s[:audio.start()]+block+s[audio.end():]
(configdir/'PCSX2.ini').write_text(s)
env=os.environ.copy(); env.update(DISPLAY=':93',QT_QPA_PLATFORM='xcb',LIBGL_ALWAYS_SOFTWARE='1');env.pop('WAYLAND_DISPLAY',None)
args=[str(root/'squashfs-root/AppRun'),'-datapath',str(run/'profile'),'-logfile',str(run/'emulator.log'),'-batch','-nogui','-elf',str(elf)]
process=subprocess.Popen(args,cwd=game,env=env,stdout=(run/'stdout.log').open('w'),stderr=subprocess.STDOUT,start_new_session=True)
record={'pid':process.pid,'args':args,'exe':str(root/'squashfs-root/usr/bin/pcsx2-qt'),'exeSha256':sha(root/'squashfs-root/usr/bin/pcsx2-qt'),'fixture':str(fixture),'stem':a.stem,'kind':a.kind,'order':a.order,'joint':joint,'restored':a.restored,'elfSha256':sha(elf),'profile':str(run/'profile'),'launchArchive':str(archive),'helperSha256':sha(Path(__file__)),'launchFiles':{f.name:sha(f) for f in archive.iterdir() if f.is_file()}}
(archive/'owned-launch.json').write_bytes((json.dumps(record,indent=2)+'\n').encode()); print(json.dumps(record,indent=2))
