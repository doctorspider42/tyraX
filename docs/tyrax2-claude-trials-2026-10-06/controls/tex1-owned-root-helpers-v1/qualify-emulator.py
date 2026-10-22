"""Root-only emulator observation, strict evidence closure and exact owned stop."""
from pathlib import Path
import argparse,json,re,subprocess,time,os,hashlib,datetime
p=argparse.ArgumentParser();p.add_argument('--stem',required=True);a=p.parse_args();assert a.stem.startswith('night-ablation-emulator-') and all(c.isalnum()or c=='-'for c in a.stem)
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001');archive=lab/(a.stem+'-launch');record=archive/'owned-launch.json';d=json.loads(record.read_text());log=Path(d['profile']).parent/'emulator.log';proc=Path('/proc')/str(d['pid']);assert(proc/'exe').resolve()==Path(d['exe']);assert d['kind']==28
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();env=os.environ.copy();env.update(DISPLAY=':93');seen={};deadline=time.monotonic()+240
def phase(text):
 rows=re.findall(r'LOG: NIGHTPHASE phase=(\d+) ',text);return int(rows[-1])if rows else None
while time.monotonic()<deadline:
 text=log.read_text(errors='replace')if log.exists()else'';current=phase(text)
 artifact=Path(d['fixture'])/'game/bin/night-ablation.log';art=artifact.read_text(errors='replace')if artifact.exists()else''
 if current is not None and current not in seen and f'NIGHTWINDOW phase={current} 'in art:
  tree=subprocess.check_output(['xwininfo','-root','-tree'],env=env,text=True);rows=re.findall(r'(0x[0-9a-fA-F]+) "vehicle-playground[^"\n]*"',tree);assert len(rows)==1
  window=rows[0];pidprop=subprocess.check_output(['xprop','-id',window,'_NET_WM_PID'],env=env,text=True);assert re.search(r'=\s*'+str(d['pid'])+r'\s*$',pidprop)
  filename=f'phase-{current}.png';image=archive/filename;assert not image.exists();before=sha(log)
  subprocess.run(['import','-window',window,str(image)],env=env,check=True,timeout=10);after=log.read_text(errors='replace');assert phase(after)==current,'capture crossed phase boundary; image retained but rejected'
  extraRows=re.findall(r'LOG: NIGHTEXTRAPHASE phase='+str(current)+r' first=\d+ extraMask=(\d+) appliedExtraMask=(\d+)',after);assert len(extraRows)==1 and extraRows[0][0]==extraRows[0][1]
  wildRows=re.findall(r'LOG: NIGHTTABLEPHASE phase='+str(current)+r' first=\d+ selected=(\d+) enabled=(\d+) appliedSelected=(\d+) appliedEnabled=(\d+)',after);assert len(wildRows)==1 and wildRows[0][0]==wildRows[0][2] and wildRows[0][1]==wildRows[0][3] and int(wildRows[0][0])==1 and int(wildRows[0][1])==1
  coronaRows=re.findall(r'LOG: NIGHTCORONAPHASE phase='+str(current)+r' first=\d+ selected=(\d+) enabled=(\d+)' ,after);assert len(coronaRows)==1 and coronaRows[0]==('0','0'); rr=re.findall(r'LOG: NIGHTPRODPHASE phase='+str(current)+r' enabled=(\d+)',after);assert len(rr)==1 and int(rr[0])==int((current==1)!=(d['order']==1))
  seen[current]=dict(filename=filename,sha256=sha(image),phase=current,coronaSpriteEnabled=0,producerObserverEnabled=int(rr[0]),extraMask=int(extraRows[0][0]),window=window,ownedPid=d['pid'],beforeLogSha256=before,afterLogSha256=sha(log),warmCaptureAfterWindowExport=True,artifactSha256=sha(artifact),utc=datetime.datetime.now(datetime.timezone.utc).isoformat());print('Qualified owned phase raster',current,flush=True)
 if 'LOG: NIGHTDONE 'in text:
  assert set(seen)=={0,1,2},'all three phase rasters required; preserve incomplete attempt'
  out=archive/'raster-capture-proof.json';assert not out.exists();out.write_text(json.dumps(dict(status='PASS_OWNED_EMULATOR_THREE_PHASE_CAPTURE_IDENTITY',helperSha256=sha(Path(__file__)),launchRecordSha256=sha(record),images=seen,qualitativeVisualReviewPending=True,physicalPerformanceAccepted=False),indent=2)+'\n');break
 time.sleep(.1)
else:raise RuntimeError('Incomplete emulator at240s; owned process/inputs retained for diagnosis')
fixture=Path(d['fixture']);command=['python3',str(lab/'tex1-owned-root-helpers-v1/freeze-completed.py'),'--launch',str(record),'--stdout',str(log),'--artifact',str(fixture/'game/bin/night-ablation.log'),'--environment','emulator','--out',str(lab/(a.stem+'-evidence'))];subprocess.run(command,check=True)
subprocess.run(['python3',str(Path(__file__).with_name('stop-emulator.py')),'--stem',a.stem],check=True)
for i in range(50):
 if not Path('/mnt/wslg/runtime-dir/pcsx2.sock.28205').exists():break
 time.sleep(.1)
assert not Path('/mnt/wslg/runtime-dir/pcsx2.sock.28205').exists(),'owned socket has not closed'
print('Completed strict three-phase emulator qualification; images require review',flush=True)
