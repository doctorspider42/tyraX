"""Qualify the private A36 current V7 ordinary capture. No timing or replay."""
import argparse
import collections
import csv
import hashlib
import json
import re
import struct
from pathlib import Path


def validate(log, images, rows):
    assert 'TAPEDONE stages=4 qualificationOnly=1' in log
    assert len(re.findall(r'TAPEFREEZE phase=0 offset=1380 ', log)) == 1
    assert not re.search(r'COMMANDSNAP busy|Assertion failed|bad_alloc|DMA Busy', log)
    cap = re.findall(r'COMMANDCAP valid=(\d+) flushes=(\d+) submissions=(\d+) qwords=(\d+) bytes=(\d+) mpg=(\d+) calls=(\d+) first=([0-9a-f]+) programEqual=(\d+)', log)
    assert len(cap) == 1
    valid, flushes, submissions, qw, size, mpg, calls, first, equal = cap[0]
    assert (valid, flushes, submissions, mpg, equal) == ('1', '1', '1', '0', '1')
    checks = re.findall(r'TAPECHECK stage=(\d+) mismatches=(\d+) mask=(\d+) first=(\d+)', log)
    assert [tuple(map(int, r[:3])) for r in checks] == [(i, 0, 0) for i in range(4)]
    stages = re.findall(r'TAPESTAGE stage=(\d+) bags=(\d+) events=(\d+) bytes=(\d+) replay=(\d+)', log)
    assert len(stages) == 4 and [int(r[0]) for r in stages] == list(range(4))
    assert len({tuple(r[1:]) for r in stages}) == 1 and all(r[4] == '0' for r in stages)
    assert len(rows) == int(stages[0][1]) > 0
    assert len({int(r['event']) for r in rows}) == len(rows)
    assert all(0 <= int(r['event']) < int(stages[0][2]) and int(r['count']) > 0 for r in rows)
    assert all(r['view'] in ('main', 'env') for r in rows)
    assert {r['view'] for r in rows} == {'main', 'env'}
    for view in ('main', 'env'):
        reference = None
        for stage in range(4):
            raw = images[stage, view]
            width, height, psm, recorded_stage = struct.unpack_from('<4I', raw)
            assert recorded_stage == stage and psm in (0, 2)
            assert 0 < width <= 1024 and 0 < height <= 1024
            assert len(raw) == 16 + width * height * (2 if psm == 2 else 4)
            if view == 'env': assert (width, height) == (128, 128)
            pixels = raw[16:]
            assert len(set(pixels)) > 8, 'Blank/flat readback'
            image = (width, height, psm, pixels)
            if reference is None: reference = image
            else: assert image == reference, (stage, view)
    return dict(qwords=int(qw), bytes=int(size), vuCalls=int(calls))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('--target', choices=('emulator', 'physical'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.fixture.resolve()
    folder = root / ('physical/bin' if args.target == 'physical' else 'game/bin')
    logpath = root / ('physical/console.log' if args.target == 'physical' else 'emulator/emulator.log')
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    binding = json.loads((root / 'native-binding.json').read_text())
    for rel, digest in binding['files'].items(): assert sha(root / rel) == digest, rel
    assert sha(folder / 'vehicle-playground.elf') == binding['elfSha256']
    manifest = json.loads((root / 'bag-tape-manifest.json').read_text())
    assert manifest['experiment'] == 'A36 current V7 ordinary baseline capture'
    source = root / 'game/src/terrain_game.cpp'
    assert 'WorldFront::active=false;ClipTrial::mode=0;' in source.read_text()
    physics = (root / 'game/src/gen/game_physics.gen.cpp').read_text()
    assert 'core.synchronizeFrame();core.sync.align2D();' in physics
    assert '  renderScene();\n  recording=false;' in physics
    assert 'if (BagTape::frozen) sharedEnvBasisValid=false;' in physics
    probe = (root / 'tyra/engine/inc/debug/motion_probe.hpp').read_text()
    assert probe.count('if(suspended){sample=enabled=clocks=false;return;}') == 2
    images = {(i, v): (folder / f'tape-{i}-{v}.bin').read_bytes() for i in range(4) for v in ('main', 'env')}
    rows = list(csv.DictReader((folder / 'bag-identity.csv').open()))
    log = logpath.read_text(errors='replace')
    closure = validate(log, images, rows)
    assert (folder / 'command-closure.bin').stat().st_size == closure['bytes']
    assert sha(folder / 'command-program-before.bin') == sha(folder / 'command-program-after.bin')
    negatives = []
    for name, altered in [('missing-finish', log.replace('TAPEDONE', 'REMOVED')),
                          ('extra-submission', log.replace('submissions=1', 'submissions=2')),
                          ('descriptor-mismatch', log.replace('TAPECHECK stage=3 mismatches=0', 'TAPECHECK stage=3 mismatches=1'))]:
        try: validate(altered, images, rows)
        except AssertionError: negatives.append(name)
        else: raise AssertionError('Control escaped: ' + name)
    broken = dict(images)
    altered = bytearray(broken[3, 'env']);altered[-1] ^= 1;broken[3, 'env'] = bytes(altered)
    try: validate(log, broken, rows)
    except AssertionError: negatives.append('changed-return-env')
    else: raise AssertionError('Raster control escaped')
    groups = collections.defaultdict(lambda: dict(bags=0, sourceVectors=0))
    for r in rows:
        key = ':'.join(r[k] for k in ('producer', 'pass', 'view'))
        groups[key]['bags'] += 1;groups[key]['sourceVectors'] += int(r['count'])
    closure['sha256'] = sha(folder / 'command-closure.bin')
    result = dict(valid=True, qualificationOnly=True, target=args.target,
                  rasterEqual=True, presentationFence=True, legacySamplerSuspended=True,
                  elfSha256=binding['elfSha256'], logSha256=sha(logpath), closure=closure,
                  descriptorGroups=dict(groups), negativeControls=negatives,
                  limitations=['one frozen daytime pose, not moving-frame pricing',
                               'descriptor source vectors include rejected bags, not emitted VU vectors',
                               'descriptor ownership is not command ownership or EE cost'])
    serialized = json.dumps(result, indent=2) + '\n'
    if args.output.exists(): assert args.output.read_text() == serialized
    else: args.output.write_text(serialized)
    inventory_paths = [root / rel for rel in binding['files']]
    inventory_paths += [root / 'native-binding.json', root / 'symbols.txt', args.output.resolve(), logpath,
                        folder / 'vehicle-playground.elf', folder / 'bag-identity.csv']
    inventory_paths += list(folder.glob('command-*.bin')) + list(folder.glob('tape-*.bin'))
    inventory = {str(p.relative_to(root)).replace('\\', '/'): sha(p) for p in inventory_paths}
    inventory_path = args.output.with_suffix('.inventory.json')
    serialized = json.dumps(inventory, indent=2) + '\n'
    if inventory_path.exists(): assert inventory_path.read_text() == serialized
    else: inventory_path.write_text(serialized)
    print(json.dumps(result, indent=2))


if __name__ == '__main__': main()
