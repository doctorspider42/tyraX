from pathlib import Path
import json,hashlib,shutil,os,signal,time
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001');a=lab/'corona-probe-v2-case1-arm1-repeat1-20261005-v2-launch';d=json.loads((a/'owned-launch.json').read_text());p=Path('/proc')/str(d['pid']);assert p.joinpath('exe').resolve()==Path(d['exe']);args=p.joinpath('cmdline').read_bytes();assert d['profile'].encode() in args and d['elf'].encode() in args
run=Path(d['profile']).parent;states=list((Path(d['profile'])/'PCSX2/sstates').glob('*.221.p2s'));assert len(states)==1
for src,name in [(states[0],'rejected-state.p2s'),(run/'emulator.log','rejected-emulator.log'),(run/'stdout.log','rejected-stdout.log')]:
 dst=a/name;assert not dst.exists();shutil.copyfile(src,dst)
record={'status':'ROOT_REJECTED_CORONA_CAPABILITY_COMPLETED_PRIMITIVE','pid':d['pid'],'commandLine':args.decode().replace('\0',' '),'nativeProofSha256':d['nativeProofSha256'],'elfSha256':d['elfSha256'],'reason':'Actual candidate input marker present, but completed primitive stayed TRIANGLE; no positive output/raster/promotion acceptance. Final READY was observed but decoder rejected.','files':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in a.iterdir() if f.is_file()}}
(a/'root-rejected-capture.json').write_text(json.dumps(record,indent=2)+'\n');os.kill(d['pid'],signal.SIGTERM)
for i in range(100):
 if not p.exists():break
 time.sleep(.1)
assert not p.exists(),'owned process alive';sock=Path(d['socket']);
if sock.exists():
 assert str(sock)=='/mnt/wslg/runtime-dir/pcsx2.sock.28205' and str(sock) not in Path('/proc/net/unix').read_text();sock.unlink()
(a/'root-owned-rejected-stop.json').write_text(json.dumps({'status':'EXACT_OWNED_REJECTED_PROCESS_STOPPED','pid':d['pid']},indent=2)+'\n');print(record['status'],d['pid'])
