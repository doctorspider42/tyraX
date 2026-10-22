"""Archive accepted BOTH-target A38 reports, event streams and actual source/raster gates."""
import argparse,hashlib,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();source=a.source;out=a.out;assert not out.exists()
repo=Path(__file__).resolve().parents[1];sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((source/'native-binding.json').read_text())
def copy(rel):
 target=out/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source/rel,target)
for rel,digest in binding['files'].items():assert sha(source/rel)==digest;copy(rel)
for target in ('emulator','physical'):
 report=json.loads((source/f'triple-{target}.json').read_text());assert report['valid'] and report['elfSha256']==binding['elfSha256']
 assert report['parentReaderSha256']==sha(repo/'tools/tyrax2-pc-moving-check.py')
 for rel,digest in report['eventPins'].items():assert sha(source/rel)==digest;copy(rel)
 folder='physical/bin' if target=='physical' else 'game/bin'
 for image in report['images']:
  rel=f'{folder}/tape-{image["phase"]*3+image["arm"]}-{image["view"]}.bin';assert sha(source/rel)==image['sha256'];copy(rel)
 log='physical/console.log' if target=='physical' else 'emulator/emulator.log';assert sha(source/log)==report['logSha256'];copy(log)
 for rel in (f'{folder}/drive-probe.csv',f'{folder}/vehicle-playground.elf',f'triple-{target}.json'):copy(rel)
for rel in ('native-binding.json','parent-native-binding.json','symbols.txt','triple-manifest.json','build.log','authored-manifest.json','tyra/Makefile.base',
 'emulator/launch.json','physical/editor-deploy.json','physical/launch-inputs.json','physical/reset.log','physical/reset-listen.log'):copy(rel)
omitted={rel:digest for rel,digest in json.loads((source/'physical/launch-inputs.json').read_text(encoding='utf-8-sig')).items()
 if not (out/'physical/bin'/rel).exists()}
(out/'omitted-physical-inputs.json').write_text(json.dumps(omitted,indent=2)+'\n')
for name in ('tyrax2-triple-fixture.py','tyrax2-triple-trial.hpp','tyrax2-triple-check.py','tyrax2-triple-archive.py',
 'tyrax2-pc-moving-check.py','tyrax2-motion-run.py','tyrax2-deploy-ps2.ps1'):
 dest=out/'tools'/name;dest.parent.mkdir(exist_ok=True);shutil.copy2(repo/'tools'/name,dest)
inventory={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'archive-sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
print('Archived',len(inventory),'files; source/data evidence, not complete runnable asset mirror')
