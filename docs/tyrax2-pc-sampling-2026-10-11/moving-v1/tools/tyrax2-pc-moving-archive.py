"""Archive complete A37 moving qualification and exact-debug instruction attribution."""
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
 report=json.loads((source/f'pc-moving-{target}.json').read_text());profile=json.loads((source/f'pc-symbols-{target}.json').read_text())
 assert report['valid'] and profile['valid'] and report['elfSha256']==binding['elfSha256']==profile['elfSha256']
 assert profile['qualificationSha256']==sha(source/f'pc-moving-{target}.json')
 for rel,digest in report['samplePins'].items():assert sha(source/rel)==digest;copy(rel)
 folder='physical/bin' if target=='physical' else 'game/bin'
 for image in report['images']:
  rel=f'{folder}/tape-{image["phase"]*3+image["arm"]}-{image["view"]}.bin';assert sha(source/rel)==image['sha256'];copy(rel)
 log='physical/console.log' if target=='physical' else 'emulator/emulator.log';assert sha(source/log)==report['logSha256'];copy(log)
 for rel in (f'{folder}/drive-probe.csv',f'{folder}/vehicle-playground.elf',f'pc-moving-{target}.json',f'pc-symbols-{target}.json'):copy(rel)
for rel in ('native-binding.json','symbols.txt','pc-moving-manifest.json','vehicle-playground.elf.sym','build.log',
 'authored-manifest.json','tyra/Makefile.base','emulator/launch.json','physical/editor-deploy.json','physical/launch-inputs.json',
 'physical/reset.log','physical/reset-listen.log'):copy(rel)
omitted={rel:digest for rel,digest in json.loads((source/'physical/launch-inputs.json').read_text(encoding='utf-8-sig')).items()
 if not (out/'physical/bin'/rel).exists()}
(out/'omitted-physical-inputs.json').write_text(json.dumps(omitted,indent=2)+'\n')
manifest=json.loads((source/'pc-moving-manifest.json').read_text());sdk=Path('C:/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2sdk')
for rel,digest in manifest['sdkPins'].items():
 assert sha(sdk/rel)==digest;dest=out/'sdk'/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(sdk/rel,dest)
cal=repo/'docs/tyrax2-pc-sampling-2026-10-11/calibration-v1'
for name in ('pc-calibration-physical.json','pc-calibration-manifest.json'):
 dest=out/'calibration-evidence'/name;dest.parent.mkdir(exist_ok=True);shutil.copy2(cal/name,dest)
for name in ('tyrax2-pc-moving.py','tyrax2-pc-moving.hpp','tyrax2-pc-moving-check.py','tyrax2-pc-moving-symbolize.py',
 'tyrax2-pc-moving-archive.py','tyrax2-pc-sample-store.hpp','tyrax2-motion-run.py','tyrax2-deploy-ps2.ps1'):
 dest=out/'tools'/name;dest.parent.mkdir(exist_ok=True);shutil.copy2(repo/'tools'/name,dest)
inventory={f.relative_to(out).as_posix():sha(f) for f in out.rglob('*') if f.is_file()}
(out/'archive-sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
print('Archived',len(inventory),'files; source/data/debug ELF evidence, not a complete runnable asset mirror')
