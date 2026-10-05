"""Root-only launcher; host arrival clocks add no commands/reads to PS2."""
from pathlib import Path
import argparse,subprocess,json,time,datetime,hashlib
p=argparse.ArgumentParser();p.add_argument('--stem',required=True);a=p.parse_args();lab=Path('F:/Projects/tyrax2-lab-20261001');launch=lab/(a.stem+'-launch');d=json.loads((launch/'owned-launch.json').read_text());game=Path(d['fixture'])/'game/bin';exe=Path('F:/Projects/tyra-editor/tools/ps2client/bin/ps2client.exe');raw=lab/(a.stem+'.log');err=lab/(a.stem+'.err');witness=launch/'host-arrivals.jsonl';assert not raw.exists() and not err.exists() and not witness.exists()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
def now():return dict(monotonicNs=time.monotonic_ns(),utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
from runtime_guard import verify_fixture
verify_fixture(Path(d['fixture']))
assert d['kind']==14 and d['joint']==d['restored']==0
for filename,h in d['launchFiles'].items():assert sha(launch/filename)==h,filename
command=[str(exe),'-h','192.168.100.150','execee','host:vehicle-playground.elf'];start=now()
proc=subprocess.Popen(command,cwd=game,stdout=subprocess.PIPE,stderr=err.open('wb'),creationflags=subprocess.CREATE_NO_WINDOW)
(lab/'ps2client.pid').write_text(str(proc.pid));(launch/'observed-launch.json').write_text(json.dumps(dict(command=command,pid=proc.pid,start=start,exeSha256=sha(exe),helperSha256=sha(Path(__file__)),hostOnly=True,newPS2Commands=False),indent=2)+'\n');print('Owned client started',proc.pid,flush=True)
pending=b'';offset=0;finished=False
with raw.open('wb') as out,witness.open('w',encoding='utf8') as records:
 records.write(json.dumps(dict(event='host-launch',**start))+'\n');records.flush()
 while True:
  chunk=proc.stdout.read1(8192)
  if not chunk:break
  stamp=now();out.write(chunk);out.flush();pending+=chunk
  while b'\n' in pending:
   line,pending=pending.split(b'\n',1);offset+=len(line)+1
   if any(tag in line for tag in (b'loadelf:',b'LOG: NIGHTPHASE ',b'LOG: NIGHTCAMERA ',b'LOG: NIGHTDONE ')):
    records.write(json.dumps(dict(event='raw-line-arrival',rawEndOffset=offset,line=line.decode('latin1'),**stamp))+'\n');records.flush()
   if b'LOG: NIGHTDONE ' in line:finished=True;print('Actual NIGHTDONE observed',flush=True)
 records.write(json.dumps(dict(event='client-eof',completedMarkerObserved=finished,returnCode=proc.wait(),**now()))+'\n');records.flush()
print('Owned client stream closed',flush=True)
