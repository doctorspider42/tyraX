"""Component-only actual VIF memory comparison using qualified native SDK input."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('source', type=Path)
p.add_argument('sdk_evidence', type=Path)
a = p.parse_args()
repo = Path(__file__).resolve().parent
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
sdk = a.sdk_evidence.resolve()
for rel, digest in json.loads((sdk / 'sha256.json').read_text()).items():
    assert sha(sdk / rel) == digest, rel
r = json.loads((sdk / 'report.json').read_text())
assert r['valid'] and not r['gpuExecuted']
dump = sdk / 'world-input-sdk.bin'
assert sha(dump) == r['dumpSha256']
assert not a.out.exists()
b = json.loads((a.source / 'native-binding.json').read_text())
for rel, digest in b['files'].items():
    assert sha(a.source / rel) == digest, rel
assert sha(a.source / 'game/bin/vehicle-playground.elf') == b['elfSha256']
assert sha(a.source / 'tyra/engine/bin/libtyra.a') == b['engineArchiveSha256']
for rel in ('world-cell-price-emulator.json', 'physical-v1/world-cell-price-ps2.json'):
    report = json.loads((a.source / rel).read_text())
    assert report['valid'] and report['variant'] == 'audio-event' and report['elfSha256'] == b['elfSha256']

def ignore(path, names):
    return [n for n in names if n in ('obj', '.git') or
            (Path(path).name == 'bin' and (n in ('log.txt', 'drive-probe.csv') or
                                         n.startswith('triple-present-'))) or
            (n.startswith(('tape-', 'command-', 'cell-', 'source-', 'moving-', 'world-price-')) and n.endswith('.bin'))]

for part in ('game', 'tyra/engine'):
    shutil.copytree(a.source / part, a.out / part, ignore=ignore)
for rel in ('tyra/Makefile.base', 'authored-manifest.json'):
    shutil.copy2(a.source / rel, a.out / rel)
for name, target in [('tyrax2-world-input-native.hpp', 'world_input_native.hpp'),
                     ('tyrax2-world-input-compact.hpp', 'world_input_compact.hpp')]:
    shutil.copy2(repo / name, a.out / 'game/inc' / target)
shutil.copy2(dump, a.out / 'game/bin/world-input-sdk.bin')
f = a.out / 'game/src/terrain_game.cpp'
t = f.read_text()
old = 'void TerrainGame::init() {\n  MotionProbe::init();'
assert t.count(old) == 1
t = '#include "world_input_native.hpp"\n' + t.replace(old, '''void TerrainGame::init() {
  WorldInputNative::run();SleepThread();
  MotionProbe::init();''')
f.write_text(t)
(a.out / 'world-input-native-manifest.json').write_text(json.dumps(dict(
    generatorSha256=sha(Path(__file__)), sdkDumpSha256=sha(dump), sdkElfSha256=r['elfSha256'],
    status='UNQUALIFIED', componentOnly=True,
    limits=['Actual VIF decode only; program kicks replaced with FLUSHE/NOP.',
            'Run before world init/recording, drain queue; save/restore all VU data and ROW/MASK/CYCLE.',
            'Two incoming ROW/MASK states; no compact shader/image/lease/FPS qualification.']), indent=2) + '\n')
print(a.out, 'UNQUALIFIED actual VIF input component')
