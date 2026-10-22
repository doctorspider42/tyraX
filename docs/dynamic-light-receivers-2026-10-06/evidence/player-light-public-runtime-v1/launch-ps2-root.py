from pathlib import Path
import argparse, subprocess, json, time, os
from runtime_common import verify_review, sha, path
p=argparse.ArgumentParser();p.add_argument('--review',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
assert os.name=='nt' and not a.out.exists()
r,f=verify_review(a.review)
owners=subprocess.run(['powershell','-NoProfile','-Command',"Get-CimInstance Win32_Process -Filter \"name='ps2client.exe'\" | Select-Object -ExpandProperty ProcessId"],capture_output=True,check=True).stdout.strip();assert not owners,owners
assert subprocess.run(['ping','-n','1','-w','1000','192.168.100.150'],capture_output=True).returncode==0
a.out.mkdir();b=f/'bin';marker=b/'ps2link.run';marker.write_bytes(b'ps2link');assert marker.read_bytes()==b'ps2link'
# Retain old emulator state and require the console to create a fresh snapshot.
for n in ['livedbg.bin','livedbg.cmd','livepad.bin']:
 q=b/n
 if q.exists():(a.out/('before-'+n)).write_bytes(q.read_bytes());q.unlink()
client=Path('F:/Projects/tyra-editor/tools/ps2client/bin/ps2client.exe')
args=[str(client),'-h','192.168.100.150','execee','host:'+path(r['elf']).name]
c=subprocess.Popen(args,cwd=b,stdout=(a.out/'ps2client.stdout').open('wb'),stderr=(a.out/'ps2client.stderr').open('wb'),creationflags=subprocess.CREATE_NO_WINDOW)
record=dict(status='ROOT_OWNED_PUBLIC_RECEIVER_PS2_LAUNCH_ATTEMPT',pid=c.pid,args=args,cwd=str(b),startedUnix=time.time(),clientSha256=sha(client),review=str(a.review),reviewSha256=sha(a.review),elfSha256=r['elfSha256'],symbolSha256=r['symbolSha256'],markerSha256=sha(marker),runtimeQualified=False,hardwarePerformanceQualified=False)
(a.out/'owned-launch.json').write_text(json.dumps(record,indent=2)+'\n');print(record['status'],c.pid)
