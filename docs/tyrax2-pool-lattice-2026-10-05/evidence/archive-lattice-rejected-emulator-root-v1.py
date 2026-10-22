from pathlib import Path
import json,hashlib,subprocess,os,signal,shutil
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001');stem='night-ablation-emulator-pool-lattice-order0-20261005'
launch=lab/(stem+'-launch');d=json.loads((launch/'owned-launch.json').read_text());out=lab/(stem+'-rejected');assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
log=Path(d['profile']).parent/'emulator.log';live=Path(d['fixture'])/'game/bin/night-ablation.log'
assert 'LOG: NIGHTDONE order=0 valid=1 loops=5400' in log.read_text()
rejection=json.loads((lab/(stem+'-evidence/strict-analysis.json')).read_text());assert rejection['status'].startswith('REJECT') or 'FAIL' in rejection['status']
proc=Path('/proc')/str(d['pid']);args=[x.decode()for x in(proc/'cmdline').read_bytes().split(b'\0')if x];assert args[args.index('-datapath')+1]==d['profile'];assert(proc/'exe').resolve()==Path(d['exe'])
for source in (log,live):shutil.copy2(source,out/source.name)
shutil.copytree(launch,out/'launch');shutil.copytree(lab/(stem+'-evidence'),out/'strict-rejection')
record={'status':'ROOT_PRESERVED_COMPLETE_BUT_INACTIVE_LATTICE_ATTEMPT','pid':d['pid'],'args':args,'rejection':rejection,'files':{str(p.relative_to(out)):sha(p) for p in out.rglob('*')if p.is_file()},'performanceAccepted':False}
(out/'proof.json').write_text(json.dumps(record,indent=2)+'\n')
assert sha(live)==sha(out/'night-ablation.log');os.kill(d['pid'],signal.SIGTERM);live.unlink();print(record['status'])
