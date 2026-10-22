"""Clone bound A18 sources/binary into a fresh replay run, dropping only outputs."""
import argparse,hashlib,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert hashlib.sha256((a.source/rel).read_bytes()).hexdigest()==h
assert hashlib.sha256((a.source/'game/bin/vehicle-playground.elf').read_bytes()).hexdigest()==binding['elfSha256']
for folder in ('game','tyra'):
    shutil.copytree(a.source/folder,a.out/folder,ignore=shutil.ignore_patterns('obj','*.log','*.csv','tape-*.bin','command-*.bin','bag-tape-control.cfg','three-path.cfg','*.a'))
for name in ('native-binding.json','bag-tape-manifest.json','authored-manifest.json','symbols.txt'):
    shutil.copy2(a.source/name,a.out/name)
for name in ('command-tape-manifest.json','command-pricing-manifest.json','list-join-manifest.json'):
    if (a.source/name).exists():shutil.copy2(a.source/name,a.out/name)
(a.out/'clone.json').write_text(json.dumps(dict(source=str(a.source),elfSha256=binding['elfSha256'],mode='replay'),indent=2)+'\n')
print(a.out)
