"""ROOT ONLY bounded progress + exact owned-game-window screenshot; no livedbg."""
from pathlib import Path
import argparse,os,re,subprocess,time,json,datetime
from owned_emulator_common import load,path,sha,owned
p=argparse.ArgumentParser();p.add_argument('--launch',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--seconds',type=float,default=1);a=p.parse_args();assert os.name=='posix' and 0<=a.seconds<=45 and not a.out.exists()
d=load(a.launch);owned(d);a.out.mkdir(parents=True);deadline=time.monotonic()+a.seconds
while time.monotonic()<deadline:owned(d);time.sleep(min(.2,max(0,deadline-time.monotonic())))
files=[a.launch.parent/'runtime/emulator.log',a.launch.parent/'runtime/stdout.log',path(d['fixture'])/'bin/log.txt',path(d['fixture'])/'bin/closeout-samples.txt'];snapshots=[];combined=''
for src in files:
 if not src.exists():continue
 data=src.read_bytes();target=a.out/(src.parent.name+'-'+src.name);target.write_bytes(data)
 snapshots.append({'source':str(src),'snapshot':str(target),'bytes':len(data),'sha256':sha(target)});combined+=data.decode(errors='replace')+'\n'
markers={name:re.findall(r'^.*CLOSEOUT'+name+r'\b.*$',combined,re.M) for name in ['CONFIG','PHASE','CONTEXT','DONE','WINDOW']}
env=os.environ.copy();env['DISPLAY']=':93';tree=subprocess.check_output(['xwininfo','-root','-tree'],env=env,text=True);matches=[]
for wid,title in re.findall(r'(0x[0-9a-fA-F]+) "([^"\n]*)"',tree):
 if not title.startswith(Path(d['elf']).stem+' ['):continue
 prop=subprocess.run(['xprop','-id',wid,'_NET_WM_PID'],env=env,capture_output=True,text=True)
 if re.search(r'=\s*'+str(d['pid'])+r'\s*$',prop.stdout):matches.append((wid,title))
assert len(matches)==1,'exact game raster window not established; no auxiliary-window fallback'
wid,title=matches[0];owned(d);image=a.out/'frame.png';subprocess.run(['import','-window',wid,str(image)],env=env,check=True,timeout=10);owned(d)
proof={'status':'OWNED_CLOSEOUT_LOG_PREFIX_AND_RASTER_IDENTITY_ONLY','snapshotUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'launchSha256':sha(a.launch),'markers':markers,'samplesRows':len(re.findall(r'\bCLOSEOUTRAW\b',combined)),'chunkRows':len(re.findall(r'\bCLOSEOUTCHUNK\b',combined)),'snapshots':snapshots,'window':wid,'title':title,'imageSha256':sha(image),'visualReviewPending':True,'strictSamplerAnalysisPending':True,'devicePerformanceQualified':False}
(a.out/'observe-proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'status':proof['status'],'doneMarkers':len(markers['DONE']),'rawRows':proof['samplesRows'],'chunkRows':proof['chunkRows']}))
