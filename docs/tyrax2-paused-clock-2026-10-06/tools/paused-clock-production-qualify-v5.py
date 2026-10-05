from pathlib import Path
import json,hashlib,subprocess,os,time,re,signal,shutil
b=Path('/mnt/f/Projects/tyrax2-lab-20261001');g=b/'paused-clock-production-vehicle-v1/game';out=b/'paused-clock-production-v5-runtime';assert not out.exists() and not (g/'bin/log.txt').exists();out.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((b/'paused-clock-production-v2-source-manifest.json').read_text());assert all(sha(g/n)==v for n,v in manifest.items());build=json.loads((b/'paused-clock-production-v2-build-exit.json').read_text(encoding='utf-8-sig'));assert build['exitCode']==0;assert '[editor] Native build complete:' in (b/'paused-clock-production-native-v2.log').read_text();elf=g/'bin/vehicle-playground.elf';assert not Path('/mnt/wslg/runtime-dir/pcsx2.sock.28205').exists()
root=Path('/home/spider/tyrax2-debugger-linux-v1');run=Path('/home/spider/paused-clock-production-v5');assert not run.exists();inis=run/'profile/PCSX2/inis';inis.mkdir(parents=True);s=(root/'profile/PCSX2/inis/PCSX2.ini').read_text();s=re.sub(r'^PINESlot\s*=.*$','PINESlot = 28205',s,flags=re.M);s=re.sub(r'^(StartPaused|ShowOnStartup)\s*=.*$',r'\1 = false',s,flags=re.M);audio=re.search(r'^\[SPU2/Output\]\n(.*?)(?=^\[|\Z)',s,re.M|re.S);assert audio;block=re.sub(r'^Backend\s*=.*$','Backend = Null',audio[0],flags=re.M);s=s[:audio.start()]+block+s[audio.end():];(inis/'PCSX2.ini').write_text(s)
exe=root/'squashfs-root/usr/bin/pcsx2-qt';assert sha(exe)=='c4629baf9c5daca45a9df6ad1ee84a188aeb025627b822a3ea62bdc48b23da18';env=os.environ.copy();env.update(DISPLAY=':93',QT_QPA_PLATFORM='xcb',LIBGL_ALWAYS_SOFTWARE='1');env.pop('WAYLAND_DISPLAY',None);log=run/'emulator.log';args=[str(root/'squashfs-root/AppRun'),'-datapath',str(run/'profile'),'-logfile',str(log),'-batch','-nogui','-elf',str(elf)];p=subprocess.Popen(args,cwd=g,env=env,stdout=(run/'stdout.log').open('w'),stderr=subprocess.STDOUT,start_new_session=True);owned=dict(pid=p.pid,args=args,exe=str(exe),exeSha256=sha(exe),elfSha256=sha(elf),symbolSha256=sha(elf.with_suffix('.elf.sym')),sourceManifestSha256=sha(b/'paused-clock-production-v2-source-manifest.json'),buildRecordSha256=sha(b/'paused-clock-production-v2-build-exit.json'),helperSha256=sha(Path(__file__)));(out/'owned-launch.json').write_text(json.dumps(owned,indent=2)+'\n');print('Owned production emulator',p.pid,flush=True)
emulog=log;log=g/'bin/log.txt'
seen={};deadline=time.monotonic()+240
while time.monotonic()<deadline:
 text=log.read_text(errors='replace') if log.exists() else ''
 assert 'ASSERT' not in text.upper(), 'Target assertion: preserve owned process and outputs'
 rows=re.findall(r'LOG: CLOCKPOLICY scene=(\d+) night=(\d+) hour=([0-9.]+) paused=(\d+) dt=([0-9.]+)',text)
 if rows:
  scene,night,hour,paused,dt=rows[-1];key=scene+'-'+night
  assert int(paused)==1 and float(hour)==(0 if int(night) else 12) and float(dt)>0
  if key not in seen and len(rows)>=2 and rows[-2][0:2]==rows[-1][0:2]:
   tree=subprocess.check_output(['xwininfo','-root','-tree'],env=env,text=True);wins=re.findall(r'(0x[0-9a-fA-F]+) "vehicle-playground[^"\n]*"',tree);assert len(wins)==1;win=wins[0];prop=subprocess.check_output(['xprop','-id',win,'_NET_WM_PID'],env=env,text=True);assert re.search(r'=\s*'+str(p.pid)+r'\s*$',prop);image=out/f'scene-{key}.png';subprocess.run(['import','-window',win,str(image)],env=env,check=True,timeout=10);seen[key]=dict(scene=int(scene),night=int(night),image=image.name,sha256=sha(image));print('Policy capture',key,flush=True)
 if 'LOG: CLOCKAPI motion=COMPLETE' in text:break
 assert p.poll() is None,'emulator exited before completion';time.sleep(.1)
else:raise RuntimeError('Incomplete qualification; owned process retained')
assert 'LOG: CLOCKAPI boot-contract=PASS' in text and set(seen)=={'0-0','0-1','1-0','1-1','2-0','2-1'},seen
assert all(sha(g/n)==v for n,v in manifest.items())
shutil.copyfile(emulog,out/'emulator.log');shutil.copyfile(log,out/'target-log.txt');shutil.copyfile(run/'stdout.log',out/'stdout.log');shutil.copyfile(b/'paused-clock-production-v2-source-manifest.json',out/'source-manifest.json')
proof=dict(status='PASS_GENERATED_TARGET_CLOCK_LIFECYCLE_AND_THREE_SCENE_DAY_NIGHT',images=seen,checks=len(rows),visualReviewPending=True,physicalPricingAccepted=False,logSha256=sha(out/'target-log.txt'));(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');proc=Path('/proc')/str(p.pid);assert proc.joinpath('exe').resolve()==exe and str(run/'profile').encode()in proc.joinpath('cmdline').read_bytes().split(b'\0');os.kill(p.pid,signal.SIGTERM);(out/'owned-stop.json').write_text(json.dumps(dict(pid=p.pid,reason='completed_clock_lifecycle_three_scenes_day_night'),indent=2)+'\n');print(proof['status'],flush=True)
