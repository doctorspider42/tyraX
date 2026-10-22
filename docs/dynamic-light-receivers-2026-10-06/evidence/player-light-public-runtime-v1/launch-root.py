"""ROOT ONLY Linux emulator boot after explicit root input review. No build/PS2."""
from pathlib import Path
import argparse,json,os,re,subprocess,time
from runtime_common import sha,verify_review,path
p=argparse.ArgumentParser();p.add_argument('--review',type=Path,required=True);p.add_argument('--emulator-root',type=Path,default=Path('/home/spider/tyrax2-debugger-linux-v1'));p.add_argument('--out',type=Path,required=True);p.add_argument('--existing-xvfb-pid',type=int);a=p.parse_args();assert os.name!='nt'and not a.out.exists();record,fixture=verify_review(a.review)
slot=29207;socket=Path('/mnt/wslg/runtime-dir')/('pcsx2.sock.'+str(slot));assert not socket.exists(),'occupied PINE; inspect exact owner'
def proc(pid):
 d=Path('/proc')/str(pid);return str((d/'exe').resolve()),[x.decode()for x in(d/'cmdline').read_bytes().split(b'\0')if x]
a.out.mkdir(parents=True);xowned=False
if a.existing_xvfb_pid:
 xe,xa=proc(a.existing_xvfb_pid);assert Path(xe).name=='Xvfb'and ':93'in xa;xpid=a.existing_xvfb_pid
else:
 assert not Path('/tmp/.X93-lock').exists(),'DISPLAY93 exists: root must supply its reviewed Xvfb pid'
 xp=subprocess.Popen(['Xvfb',':93','-screen','0','1024x768x24','-nolisten','tcp'],stdout=(a.out/'xvfb.log').open('wb'),stderr=subprocess.STDOUT,start_new_session=True);xpid=xp.pid;xowned=True;time.sleep(.4);assert xp.poll()is None
run=a.out/'runtime';config=run/'profile/PCSX2/inis';config.mkdir(parents=True);s=(a.emulator_root/'profile/PCSX2/inis/PCSX2.ini').read_text(encoding='utf8');s,n=re.subn(r'^PINESlot\s*=.*$',f'PINESlot = {slot}',s,flags=re.M);assert n==1
s=re.sub(r'^(StartPaused|ShowOnStartup)\s*=.*$',r'\1 = false',s,flags=re.M);audio=re.search(r'^\[SPU2/Output\]\n(.*?)(?=^\[|\Z)',s,re.M|re.S);assert audio;block,n=re.subn(r'^Backend\s*=.*$','Backend = Null',audio.group(0),flags=re.M);assert n==1;s=s[:audio.start()]+block+s[audio.end():];(config/'PCSX2.ini').write_text(s,encoding='utf8')
env=os.environ.copy();env.update(DISPLAY=':93',QT_QPA_PLATFORM='xcb',LIBGL_ALWAYS_SOFTWARE='1');env.pop('WAYLAND_DISPLAY',None)
elf=path(record['elf']);exe=a.emulator_root/'squashfs-root/usr/bin/pcsx2-qt';args=[str(a.emulator_root/'squashfs-root/AppRun'),'-datapath',str(run/'profile'),'-logfile',str(run/'emulator.log'),'-batch','-nogui','-elf',str(elf)]
owned=dict(status='ROOT_INPUT_REVIEWED_LAUNCH_PREPARED',fixture=str(fixture),elf=str(elf),elfSha256=sha(elf),exe=str(exe),exeSha256=sha(exe),args=args,profile=str(run/'profile'),log=str(run/'emulator.log'),review=str(a.review.resolve()),reviewSha256=sha(a.review),buildRecord=record,pineSlot=slot,xvfbPid=xpid,xvfbOwned=xowned,helperSha256=sha(Path(__file__)),configSha256=sha(config/'PCSX2.ini'),audioOutputQualified=False,runtimeQualified=False)
(a.out/'launch-preparation.json').write_text(json.dumps(owned,indent=2)+'\n',encoding='utf8')
child=subprocess.Popen(args,cwd=fixture/'bin',env=env,stdout=(run/'stdout.log').open('wb'),stderr=subprocess.STDOUT,start_new_session=True);owned.update(status='OWNED_PUBLIC_RECEIVER_EMULATOR_LAUNCHED',pid=child.pid);(a.out/'owned-launch.json').write_text(json.dumps(owned,indent=2)+'\n',encoding='utf8');print('OWNED_PUBLIC_RECEIVER_EMULATOR_LAUNCHED',child.pid)
