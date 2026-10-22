from pathlib import Path
b=Path('F:/Projects/tyrax2-lab-20261001');src=(b/'sky-retint-production-qualify-v3.py').read_text(encoding='utf8');head=src[:src.index('seen={};deadline=')];head=head.replace('sky-retint-production-vehicle-v1','paused-clock-production-vehicle-v1').replace('sky-retint-production-v3','paused-clock-production-v1').replace('sky-retint-production-native-v3','paused-clock-production-native-v1');head=head.replace("assert '[editor] Native build complete:'", "assert '[editor] Native build complete:'")
body='''seen={};deadline=time.monotonic()+240
while time.monotonic()<deadline:
 text=log.read_text(errors='replace') if log.exists() else ''
 assert 'ASSERT' not in text.upper(), 'Target assertion: preserve owned process and outputs'
 rows=re.findall(r'LOG: CLOCKPOLICY scene=(\\d+) night=(\\d+) hour=([0-9.]+) paused=(\\d+) dt=([0-9.]+)',text)
 if rows:
  scene,night,hour,paused,dt=rows[-1];key=scene+'-'+night
  assert int(paused)==1 and float(hour)==(0 if int(night) else 12) and float(dt)>0
  if key not in seen:
   tree=subprocess.check_output(['xwininfo','-root','-tree'],env=env,text=True);wins=re.findall(r'(0x[0-9a-fA-F]+) "vehicle-playground[^"\\n]*"',tree);assert len(wins)==1;win=wins[0];prop=subprocess.check_output(['xprop','-id',win,'_NET_WM_PID'],env=env,text=True);assert re.search(r'=\\s*'+str(p.pid)+r'\\s*$',prop);image=out/f'scene-{key}.png';subprocess.run(['import','-window',win,str(image)],env=env,check=True,timeout=10);seen[key]=dict(scene=int(scene),night=int(night),image=image.name,sha256=sha(image));print('Policy capture',key,flush=True)
 if 'LOG: CLOCKAPI motion=COMPLETE' in text:break
 assert p.poll() is None,'emulator exited before completion';time.sleep(.1)
else:raise RuntimeError('Incomplete qualification; owned process retained')
assert 'LOG: CLOCKAPI boot-contract=PASS' in text and set(seen)=={'0-0','0-1','1-0','1-1','2-0','2-1'},seen
assert all(sha(g/n)==v for n,v in manifest.items())
shutil.copyfile(log,out/'emulator.log');shutil.copyfile(run/'stdout.log',out/'stdout.log');shutil.copyfile(b/'paused-clock-production-v1-source-manifest.json',out/'source-manifest.json')
proof=dict(status='PASS_GENERATED_TARGET_CLOCK_LIFECYCLE_AND_THREE_SCENE_DAY_NIGHT',images=seen,checks=len(rows),visualReviewPending=True,physicalPricingAccepted=False,logSha256=sha(out/'emulator.log'));(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\\n');proc=Path('/proc')/str(p.pid);assert proc.joinpath('exe').resolve()==exe and str(run/'profile').encode()in proc.joinpath('cmdline').read_bytes().split(b'\\0');os.kill(p.pid,signal.SIGTERM);(out/'owned-stop.json').write_text(json.dumps(dict(pid=p.pid,reason='completed_clock_lifecycle_three_scenes_day_night'),indent=2)+'\\n');print(proof['status'],flush=True)
'''
(b/'paused-clock-production-qualify-v1.py').write_text(head+body,encoding='utf8');print('Prepared private emulator lifecycle helper')
