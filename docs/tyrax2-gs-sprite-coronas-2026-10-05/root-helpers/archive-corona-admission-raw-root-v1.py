from pathlib import Path
import json,hashlib,shutil,os,signal,time
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001');stem='night-ablation-emulator-corona-admission-order0-20261005';a=lab/(stem+'-launch');d=json.loads((a/'owned-launch.json').read_text());p=Path('/proc')/str(d['pid']);assert p.joinpath('exe').resolve()==Path(d['exe']);args=p.joinpath('cmdline').read_bytes();assert d['profile'].encode() in args and (d['fixture']+'/game/bin/vehicle-playground.elf').encode() in args
out=lab/(stem+'-root-raw');assert not out.exists();out.mkdir();run=Path(d['profile']).parent;log=run/'emulator.log';assert 'NIGHTDONE order=0 valid=1 loops=5400' in log.read_text();live=Path(d['fixture'])/'game/bin/night-ablation.log'
for src,name in [(log,'stdout.log'),(live,'night-ablation.log'),(run/'stdout.log','emulator-stdout.log')]:shutil.copyfile(src,out/name)
record={'status':'ROOT_COMPLETE_5400_RAW_ARCHIVED_PARSER_REJECTION_NOT_ACCEPTED','pid':d['pid'],'commandLine':args.decode().replace('\0',' '),'launchSha256':hashlib.sha256((a/'owned-launch.json').read_bytes()).hexdigest(),'reason':'Three phase images captured, DONE observed. Initial kind9 parser failed missing records due unhandled emulator timestamp prefixes; retain unchanged bytes for separate reanalysis.','files':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in out.iterdir() if f.is_file()}}
(out/'root-raw-proof.json').write_text(json.dumps(record,indent=2)+'\n');os.kill(d['pid'],signal.SIGTERM)
for i in range(100):
 if not p.exists():break
 time.sleep(.1)
assert not p.exists();sock=Path('/mnt/wslg/runtime-dir/pcsx2.sock.28205')
if sock.exists():assert str(sock) not in Path('/proc/net/unix').read_text();sock.unlink()
(out/'root-owned-stop.json').write_text(json.dumps({'status':'EXACT_OWNED_COMPLETE_PROCESS_STOPPED_AFTER_RAW_ARCHIVE','pid':d['pid'],'liveArtifactRetained':True},indent=2)+'\n');print(record['status'],d['pid'])

