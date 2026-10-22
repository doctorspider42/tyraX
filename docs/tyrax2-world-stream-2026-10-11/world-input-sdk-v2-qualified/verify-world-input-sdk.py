"""Source-bind actual SDK dumps and replay cold compaction, not compact GPU/FPS."""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture', type=Path)
p.add_argument('out', type=Path)
a = p.parse_args()
root = a.fixture.resolve()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
binding = json.loads((root / 'native-binding.json').read_text())
for rel, digest in binding['files'].items():
    assert sha(root / rel) == digest, rel
assert sha(root / 'game/bin/vehicle-playground.elf') == binding['elfSha256']
assert sha(root / 'tyra/engine/bin/libtyra.a') == binding['engineArchiveSha256']
launch = json.loads((root / 'emulator/launch.json').read_text())
assert launch['elfSha256'] == binding['elfSha256']
log = (root / 'emulator/emulator.log').read_text(errors='replace')
quiet = (root / 'game/bin/log.txt').read_text(errors='replace')
assert 'Assertion failed!' not in log + quiet
assert 'A44SDK checks=12 passes=60 parts=270 controls=54' in log
match = re.findall(r'INPUTSDKDONE records=60 words=(\d+) gpuExecuted=0 componentOnly=1', log)
assert len(match) == 1
dump = root / 'game/bin/world-input-sdk.bin'
raw = dump.read_bytes()
assert len(raw) == int(match[0]) * 4
assert not a.out.exists()
a.out.mkdir(parents=True)
repo = Path(__file__).resolve().parent
for name in ('tyrax2-world-input-compact.hpp', 'verify-world-input-compact.cpp',
             'verify-world-input-sdk.py', 'tyrax2-world-input-sdk-fixture.py'):
    shutil.copy2(repo / name, a.out / name)
shutil.copy2(dump, a.out / dump.name)
names = set(binding['files'])
names.update(('native-binding.json', 'world-input-sdk-manifest.json', 'emulator/launch.json',
              'emulator/emulator.log', 'game/bin/log.txt', 'game/bin/world-input-sdk.bin',
              'game/bin/vehicle-playground.elf', 'tyra/engine/bin/libtyra.a', 'build.log'))
for rel in sorted(names):
    target = a.out / rel
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / rel, target)
results = {}
for mode, flags in [('optimized', ['-O2']), ('sanitized', ['-O1', '-g',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
    binary = a.out / f'verify-{mode}'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                    str(a.out / 'verify-world-input-compact.cpp'), '-o', str(binary)], check=True)
    r = subprocess.run([str(binary), str(a.out / dump.name)], capture_output=True, text=True)
    (a.out / f'{mode}.log').write_text(r.stdout + r.stderr)
    assert r.returncode == 0, r.stderr
    actual = re.findall(r'PASS actualSdkRecords=60 oldWords=(\d+) compactWords=(\d+) compactedStreams=(\d+) fallbackStreams=(\d+) exactVuAtEveryKick=1 observedStateStable=1 gpuExecuted=0', r.stdout)
    assert len(actual) == 1
    results[mode] = list(map(int, actual[0]))
assert results['optimized'] == results['sanitized']
controls = []
for fault in range(8):
    bad = bytearray(raw)
    if fault == 0:
        bad = bad[:-1]
    elif fault == 1:
        bad += b'\0'
    else:
        index = {2: 0, 3: 2, 4: 3, 5: 9, 6: 9 + 5, 7: len(raw) // 4 - 7}[fault]
        old = struct.unpack_from('<I', bad, index * 4)[0]
        struct.pack_into('<I', bad, index * 4, old ^ 1)
    file = a.out / f'reject-{fault}.bin'
    file.write_bytes(bad)
    r = subprocess.run([str(a.out / 'verify-optimized'), str(file)], capture_output=True, text=True)
    assert r.returncode != 0, fault
    controls.append(dict(fault=fault, returncode=r.returncode))
report = dict(valid=True, componentOnly=True, gpuExecuted=False,
              elfSha256=binding['elfSha256'], dumpSha256=sha(dump),
              logSha256=sha(root / 'emulator/emulator.log'), results=results,
              rejectingControls=controls,
              limits=['Actual SDK emission, modeled compact decode only; no compact native VIF submission.',
                      'VIF state snapshots are not queue-wide lifetime/state proof.',
                      'No moving/FPS/default qualification.'])
(a.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
pins = {f.relative_to(a.out).as_posix(): sha(f) for f in a.out.rglob('*') if f.is_file()}
(a.out / 'sha256.json').write_text(json.dumps(pins, indent=2) + '\n')
assert all(sha(a.out / rel) == digest for rel, digest in pins.items())
print(json.dumps(dict(valid=True, results=results, controls=len(controls), files=len(pins), gpuExecuted=False)))
