"""Archive a completed fixed-case probe, then stop only its exact owned process."""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, signal, subprocess, time

p=argparse.ArgumentParser();p.add_argument('--stem',required=True);p.add_argument('--slot',type=int,required=True);a=p.parse_args()
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001');archive=lab/(a.stem+'-launch')
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
d=json.loads((archive/'owned-launch.json').read_text());proc=Path('/proc')/str(d['pid'])
assert (proc/'exe').resolve()==Path(d['exe']) and sha(Path(d['exe']))==d['exeSha256']
args=[s.decode() for s in (proc/'cmdline').read_bytes().split(b'\0') if s]
assert args[args.index('-datapath')+1]==d['profile'] and args[args.index('-elf')+1]==d['elf']
v=json.loads((archive/'verified-capture.json').read_text());assert v['status']=='PASS_OFFLINE_SINGLE_PAUSED_STATIC_PROBE_CAPTURE_ONLY'
assert (v['arm'],v['vertices'],v['repeats'])==(d['arm'],d['vertices'],d['repeats'])
run=Path(d['profile']).parent;log=run/'emulator.log';text=log.read_text(errors='strict')
assert 'POOL2PROBE_REJECT' not in text
from protocol import verify
protocol=verify(text,d['arm'],d['vertices'],d['repeats'],clip=0)
assert v['sourceManifestSha256']==d['sourceManifestSha256'] and v['nativeProofSha256']==d['nativeProofSha256']
assert v['files'][str(archive/'pool2-probe.cfg')]==d['cfgSha256']
assert v['nonconstantInteriorFogValidated'] and v['finalEpochRGBAValidated'] and v['inputTopologyValidated'] and v['finalEpoch']==d['repeats']
assert sha(Path(d['fixture'])/'runtime-assets-manifest.json')==d['runtimeAssetsManifestSha256']
crc=d['preload']['pcsx2CRC'];assert 'Game CRC = '+crc in text
settings=Path(d['preload']['destination']);settingsData=json.loads(settings.read_text())
assert settingsData['Breakpoints'][0]['OFFSET'].lower()==d['preload']['haltPC'].lower()
state=Path(d['profile'])/'PCSX2/sstates'/f'vehicle-playground ({crc}).{a.slot:02d}.p2s'
assert v['files'][str(state)]==sha(state)
for old,new in [(state,archive/'state.p2s'),(log,archive/'emulator.log'),(run/'stdout.log',archive/'emulator-stdout.log'),(Path(d['ini']),archive/'PCSX2-after.ini'),(settings,archive/'debugger-settings-after.json')]:
 assert not new.exists();shutil.copy2(old,new);assert sha(old)==sha(new)
env={**os.environ,'DISPLAY':':93'}
tree=subprocess.check_output(['xwininfo','-root','-tree'],env=env,text=True)
windows=re.findall(r'(0x[0-9a-fA-F]+) "vehicle-playground[^"\n]*"',tree);assert len(windows)==1
window=windows[0];prop=subprocess.check_output(['xprop','-id',window,'_NET_WM_PID'],env=env,text=True)
assert re.search(r'=\s*'+str(d['pid'])+r'\s*$',prop)
image=archive/'raster.png';assert not image.exists();subprocess.run(['import','-window',window,str(image)],env=env,check=True)
record=dict(status='PASS_SOURCE_NATIVE_OWNED_FIXED_CASE_PROTOCOL_AND_PAUSED_CAPTURE_BOUND',
 arm=d['arm'],vertices=d['vertices'],repeats=d['repeats'],pid=d['pid'],sourceManifestSha256=d['sourceManifestSha256'],
 elfSha256=d['elfSha256'],nativeProofSha256=d['nativeProofSha256'],cfgSha256=d['cfgSha256'],
 validatedCaptureSha256=sha(archive/'verified-capture.json'),captureReportSha256=sha(archive/'capture-report.json'),
 finalReadyMarker=v['readyMarker'],haltPC=v['registers']['pc'],stateSha256=sha(archive/'state.p2s'),
 actualMicrocodeSha256=v['actualMicrocodeSha256'],actualVuMemorySha256=v['vuMemorySha256'],
 files={f.name:sha(f) for f in archive.iterdir() if f.is_file()},helperSha256=sha(Path(__file__)),
 warmFreshnessIndependentEpoch=True,finalColorEpoch=d['repeats'],protocol=protocol,runtimeAssetsManifestSha256=d['runtimeAssetsManifestSha256'],pairedPackedEqualityAccepted=False,performanceAccepted=False,
 limits=['Final requested color epoch is proven in saved VU output; earlier iterations are protocol-bound without individual snapshots.',
 'All counters and cold expansion oracle disabled. Snapshot drains are correctness observer operations; no target timing result.',
 'One capture does not establish paired output equality, GS consumption, hardware precision or raster equality.'])
out=archive/'completed-probe-proof.json';assert not out.exists();out.write_text(json.dumps(record,indent=2)+'\n')
stop=archive/'owned-stop.json';assert not stop.exists();stop.write_text(json.dumps(dict(pid=d['pid'],args=args,reason='completed-fixed-case-capture',completedProofSha256=sha(out)),indent=2)+'\n')
os.kill(d['pid'],signal.SIGTERM)
deadline=time.monotonic()+10
while time.monotonic()<deadline and Path(d['socket']).exists():time.sleep(.1)
if proc.exists():
 assert (proc/'exe').resolve()==Path(d['exe'])
 current=(proc/'cmdline').read_bytes();assert d['profile'].encode() in current and d['elf'].encode() in current
 second=archive/'owned-stop-second-signal.json';assert not second.exists()
 second.write_text(json.dumps(dict(pid=d['pid'],reason='first graceful signal left exact owned completed diagnostic alive'),indent=2)+'\n')
 os.kill(d['pid'],signal.SIGTERM)
 until=time.monotonic()+10
 while time.monotonic()<until and proc.exists():time.sleep(.1)
 assert not proc.exists(),'Owned process stayed alive after second signal; preserve and inspect'
if Path(d['socket']).exists():
 import stat
 socket=Path(d['socket']);assert str(socket)=='/mnt/wslg/runtime-dir/pcsx2.sock.28205'
 assert not proc.exists() and str(socket) not in Path('/proc/net/unix').read_text()
 st=socket.stat();assert stat.S_ISSOCK(st.st_mode)
 cleanup=archive/'owned-stale-socket-cleanup.json';assert not cleanup.exists()
 cleanup.write_text(json.dumps(dict(pid=d['pid'],socket=str(socket),inode=st.st_ino,ownedProcessDead=True,noBoundProcNetUnixEntry=True),indent=2)+'\n')
 socket.unlink()
assert not Path(d['socket']).exists()
print(record['status'],sha(out),flush=True)
