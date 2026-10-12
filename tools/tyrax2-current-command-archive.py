"""Preserve both closed current-capture qualifications without modifying inputs."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('fixture', type=Path)
parser.add_argument('out', type=Path)
args = parser.parse_args()
root, out = args.fixture.resolve(), args.out.resolve()
assert not out.exists()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
paths = set()
for target in ('emulator', 'physical'):
    name = f'current-command-{target}'
    report = json.loads((root / (name + '.json')).read_text())
    assert report['valid'] and report['qualificationOnly'] and report['target'] == target
    inventory = json.loads((root / (name + '.inventory.json')).read_text())
    for rel, digest in inventory.items():
        assert sha(root / rel) == digest, rel
        paths.add(rel)
    paths.add(name + '.inventory.json')
    paths.add(f'current-lineage-{target}-v2.json')
    decoded = json.loads((root / f'current-lineage-{target}-v2.json').read_text())
    assert decoded['valid'] and decoded['elfSha256'] == report['elfSha256']
    assert decoded['closureSha256'] == report['closure']['sha256']
paths.update(('bag-tape-manifest.json', 'authored-manifest.json', 'build-repair.log',
              'physical/editor-deploy.json', 'physical/launch-inputs.json', 'emulator/launch.json'))
for rel in sorted(paths):
    dest = out / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / rel, dest)
repo = Path(__file__).resolve().parents[1]
for name in ('tyrax2-bag-tape.py', 'tyrax2-bag-tape.hpp', 'tyrax2-command-tape.hpp',
             'tyrax2-command-size.hpp', 'tyrax2-quiet-fixture.py',
             'tyrax2-current-command-check.py', 'tyrax2-command-lineage.py',
             'verify-command-lineage.cpp', 'tyrax2-current-command-archive.py'):
    dest = out / 'tools' / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(repo / 'tools' / name, dest)
inventory = {str(p.relative_to(out)).replace('\\', '/'): sha(p)
             for p in sorted(out.rglob('*')) if p.is_file()}
(out / 'archive-sha256.json').write_text(json.dumps(inventory, indent=2) + '\n')
print('Preserved', len(inventory), 'files:', out)
