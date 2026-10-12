"""Freeze actual input-VIF component evidence and replay both exact readers."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('fixture', type=Path)
p.add_argument('attempt', type=Path)
a = p.parse_args()
root, out, attempt = a.fixture.resolve(), a.out.resolve(), a.attempt.resolve()
assert not out.exists()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
b = json.loads((root / 'native-binding.json').read_text())
for rel, digest in b['files'].items():
    assert sha(root / rel) == digest, rel
names = set(b['files'])
names.update(('native-binding.json', 'authored-manifest.json', 'world-input-native-manifest.json',
              'world-input-native-emulator.json', 'symbols.txt', 'build.log',
              'game/bin/vehicle-playground.elf', 'game/bin/world-input-sdk.bin', 'game/bin/log.txt',
              'tyra/engine/bin/libtyra.a', 'emulator/launch.json', 'emulator/emulator.log'))
for rel in sorted(names):
    target = out / rel
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / rel, target)
shutil.copytree(attempt, out / 'physical')
repo = Path(__file__).resolve().parent
for name in ('tyrax2-world-input-native.hpp', 'tyrax2-world-input-compact.hpp',
             'tyrax2-world-input-native-fixture.py', 'verify-world-input-native.py',
             'tyrax2-world-input-native-archive.py', 'tyrax2-motion-run.py', 'tyrax2-deploy-ps2.ps1'):
    (out / 'tools').mkdir(exist_ok=True)
    shutil.copy2(repo / name, out / 'tools' / name)
for platform in ('emulator', 'ps2'):
    report = out / ('world-input-native-emulator.json' if platform == 'emulator'
                    else 'physical/world-input-native-ps2.json')
    original = report.read_bytes()
    r = json.loads(original)
    assert r['valid'] and r['componentOnly'] and r['compactVifExecuted'] and not r['shaderExecuted']
    assert r['elfSha256'] == b['elfSha256']
    command = [sys.executable, str(out / 'tools/verify-world-input-native.py'), str(out), '--platform', platform]
    if platform == 'ps2':
        command += ['--attempt', str(out / 'physical')]
    result = subprocess.run(command, capture_output=True, text=True)
    (out / f'{platform}-reader.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stderr
    assert original == report.read_bytes(), 'Archived component reader changed report'
pins = {f.relative_to(out).as_posix(): sha(f) for f in out.rglob('*') if f.is_file()}
(out / 'sha256.json').write_text(json.dumps(pins, indent=2) + '\n')
assert all(sha(out / rel) == digest for rel, digest in pins.items())
print(json.dumps(dict(valid=True, files=len(pins), exactEmulatorReplay=True,
                     exactPhysicalReplay=True, physicalVifExecuted=True, shaderExecuted=False,
                     promotionAllowed=False)))
