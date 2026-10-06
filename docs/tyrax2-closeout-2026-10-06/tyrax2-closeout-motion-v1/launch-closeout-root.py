"""ROOT ONLY. Run under Linux after root manually reviews native/source inputs."""
from pathlib import Path
import argparse,os,re,subprocess,time,json
from owned_emulator_common import sha,path,review,proc
p=argparse.ArgumentParser();p.add_argument('--review',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--xvfb-pid',type=int,default=19);p.add_argument('--emulator-root',type=Path,default=Path('/home/spider/tyrax2-debugger-linux-v1'));a=p.parse_args()
assert os.name=='posix' and not a.out.exists();r=review(a.review);fixture=path(r['fixture'])
assert fixture.is_dir() and fixture.resolve().is_relative_to(Path('/mnt/f/Projects/tyrax2-lab-20261001'))
xe,xa,xstart=proc(a.xvfb_pid);assert Path(xe).name=='Xvfb' and ':93' in xa
socket=Path('/mnt/wslg/runtime-dir/pcsx2.sock.29207');assert not socket.exists(),'PINE occupied; root must inspect exact owner'
for name in ['closeout-samples.txt','log.txt']:assert not (fixture/'bin'/name).exists(),'Archive stale output before launch: '+name
elf=path(next(x['path'] for x in r['inputs'] if x['role']=='elf'));assert elf.resolve().parent==(fixture/'bin').resolve()
run=a.out/'runtime';inis=run/'profile/PCSX2/inis';inis.mkdir(parents=True)
baseline=a.emulator_root/'profile/PCSX2/inis/PCSX2.ini';text=baseline.read_text()
for key,value in [('PINESlot','29207'),('EnablePINE','true'),('StartPaused','false'),('Renderer','13'),('FrameLimitEnable','true'),('NominalScalar','1')]:
 text,n=re.subn(r'^'+key+r'\s*=.*$',key+' = '+value,text,flags=re.M);assert n==1,key
audio=re.search(r'^\[SPU2/Output\]\n.*?(?=^\[|\Z)',text,re.M|re.S);assert audio
block,n=re.subn(r'^Backend\s*=.*$','Backend = Null',audio.group(0),flags=re.M);assert n==1
text=text[:audio.start()]+block+text[audio.end():];(inis/'PCSX2.ini').write_bytes(text.encode())
env=os.environ.copy();env.update(DISPLAY=':93',QT_QPA_PLATFORM='xcb',LIBGL_ALWAYS_SOFTWARE='1');env.pop('WAYLAND_DISPLAY',None)
exe=a.emulator_root/'squashfs-root/usr/bin/pcsx2-qt';args=[str(a.emulator_root/'squashfs-root/AppRun'),'-datapath',str(run/'profile'),'-logfile',str(run/'emulator.log'),'-batch','-nogui','-elf',str(elf)]
child=subprocess.Popen(args,cwd=fixture/'bin',env=env,stdout=(run/'stdout.log').open('wb'),stderr=subprocess.STDOUT,start_new_session=True)
initialExe,initialArgs,initialStart=proc(child.pid)
attempt={'status':'ROOT_OWNED_CLOSEOUT_LAUNCH_ATTEMPT','pid':child.pid,'procStartTicks':initialStart,'args':args,'reviewSha256':sha(a.review),'runtimeQualified':False}
(a.out/'launch-attempt.json').write_text(json.dumps(attempt,indent=2)+'\n')
deadline=time.monotonic()+5
try:
 while True:
  assert child.poll() is None,'launcher exited'
  pe,pa,ps=proc(child.pid)
  assert ps==initialStart,'PID identity changed during startup'
  if pe==str(exe.resolve()):break
  assert time.monotonic()<deadline,'AppRun did not exec exact emulator';time.sleep(.1)
except Exception as error:
 attempt.update(status='ROOT_OWNED_CLOSEOUT_STARTUP_FAILED',error=str(error),terminalExitConfirmed=child.poll() is not None)
 if child.poll() is None:
  currentExe,currentArgs,currentStart=proc(child.pid)
  if currentStart==initialStart:
   child.terminate();attempt['ownedChildStopRequested']=True
 (a.out/'launch-attempt.json').write_text(json.dumps(attempt,indent=2)+'\n')
 raise
d={'status':'ROOT_OWNED_CLOSEOUT_EMULATOR_LAUNCHED','pid':child.pid,'procStartTicks':ps,'exe':pe,'exeSha256':sha(exe),'args':args,'elf':str(elf),'elfSha256':sha(elf),'fixture':str(fixture),'profile':str(run/'profile'),'review':str(a.review.resolve()),'reviewSha256':sha(a.review),'configSha256':sha(inis/'PCSX2.ini'),'baselineConfigSha256':sha(baseline),'pineSlot':29207,'display':':93','xvfbPid':a.xvfb_pid,'xvfbStartTicks':xstart,'xvfbOwned':False,'helperSha256':sha(__file__),'audioQualified':False,'runtimeQualified':False}
(a.out/'owned-launch.json').write_text(json.dumps(d,indent=2)+'\n');print(json.dumps({'status':d['status'],'pid':child.pid}))
