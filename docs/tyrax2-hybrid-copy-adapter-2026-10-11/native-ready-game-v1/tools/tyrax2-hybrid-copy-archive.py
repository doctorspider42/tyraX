"""Preserve a qualified A41 run and its exact readers, including launch inputs."""
import argparse,hashlib,json,shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path);p.add_argument('out',type=Path)
p.add_argument('--physical-dir',default='physical')
p.add_argument('--physical-report',default='hybrid-copy-physical.json')
p.add_argument('--emulator-report',default='hybrid-copy-emulator.json')
a=p.parse_args();source=a.source.resolve();out=a.out.resolve()
assert not out.exists()
repo=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()

def copy(rel):
 target=out/rel;target.parent.mkdir(parents=True,exist_ok=True)
 shutil.copy2(source/rel,target)

binding=json.loads((source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():
 assert sha(source/rel)==digest,rel;copy(rel)
for target,reportName,folder,log in (
 ('emulator',a.emulator_report,'game/bin','emulator/emulator.log'),
 ('physical',a.physical_report,a.physical_dir+'/bin',a.physical_dir+'/console.log')):
 report=json.loads((source/reportName).read_text())
 assert report['valid'] and report['platform']==target and report['experiment'] in ('A41','A42')
 assert report['elfSha256']==binding['elfSha256']==sha(source/folder/'vehicle-playground.elf')
 assert report['readerSha256']==sha(repo/'tools/tyrax2-hybrid-copy-check.py')
 assert report['baselineReaderSha256']==sha(repo/'tools/tyrax2-pc-moving-check.py')
 assert len(report['images'])==54 and len(report['negativeControls'])==(18 if report['experiment']=='A42' else 16)
 for rel,digest in report['eventPins'].items():
  assert sha(source/rel)==digest,rel;copy(rel)
 for image in report['images']:
  rel=f'{folder}/tape-{image["phase"]*3+image["arm"]}-{image["view"]}.bin'
  assert sha(source/rel)==image['sha256'],rel;copy(rel)
 assert sha(source/log)==report['logSha256'];copy(log)
 for rel in (reportName,f'{folder}/drive-probe.csv',f'{folder}/vehicle-playground.elf'):copy(rel)
physical=source/a.physical_dir
for rel,digest in json.loads((physical/'launch-inputs.json').read_text(encoding='utf-8-sig')).items():
 assert sha(physical/'bin'/rel)==digest,rel;copy(f'{a.physical_dir}/bin/{rel}')
for rel in ('native-binding.json','a38-native-binding.json','parent-native-binding.json',
 'symbols.txt','linked-disassembly.txt','irq-closure.json','hybrid-copy-manifest.json',
 'triple-manifest.json','authored-manifest.json','build.log','tyra/Makefile.base',
 'emulator/launch.json',f'{a.physical_dir}/editor-deploy.json',f'{a.physical_dir}/launch-inputs.json'):
 copy(rel)
for rel in ('reset.log','reset-listen.log','console.stderr.log'):
 if (physical/rel).exists():copy(f'{a.physical_dir}/{rel}')
for name in ('tyrax2-hybrid-copy-fixture.py','tyrax2-hybrid-copy-check.py',
 'tyrax2-hybrid-copy-archive.py','tyrax2-hybrid-copy-irq-audit.py',
 'tyrax2-pc-moving-check.py','tyrax2-motion-run.py','tyrax2-deploy-ps2.ps1'):
 target=out/'tools'/name;target.parent.mkdir(exist_ok=True);shutil.copy2(repo/'tools'/name,target)
inventory={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'archive-sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
print('Archived',len(inventory),'files; includes immutable physical launch inputs for strict reader replay')
