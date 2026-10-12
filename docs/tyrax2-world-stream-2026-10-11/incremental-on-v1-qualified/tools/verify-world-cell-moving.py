"""Require same-ELF moving source poses, actual cell delivery and exact rasters."""
import argparse
import csv
import hashlib
import json
import re
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('off', type=Path)
p.add_argument('on', type=Path)
a = p.parse_args()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()

def read(root, mode):
    binding = json.loads((root / 'native-binding.json').read_text())
    for rel, digest in binding['files'].items():
        assert sha(root / rel) == digest, rel
    assert sha(root / 'game/bin/vehicle-playground.elf') == binding['elfSha256']
    launch = json.loads((root / 'emulator/launch.json').read_text())
    assert launch['elfSha256'] == binding['elfSha256']
    manifest = json.loads((root / 'world-cell-moving-manifest.json').read_text())
    assert manifest['mode'] == mode and manifest['movingOrder'] == 2
    assert manifest['bufferCount'] == 2
    incremental=bool(manifest.get('incremental',False))
    assert manifest['fixedSceneStep'] and manifest['commonReservation']
    assert manifest['deferredPreparation']==(not incremental)
    control = root / 'game/bin/world-moving-control.txt'
    assert control.read_bytes() == (b'1' if mode == 'ON' else b'0')
    assert sha(control) == manifest['controlSha256']
    log = (root / 'emulator/emulator.log').read_text(errors='replace')
    enabled = int(mode == 'ON')
    config=rf'A44MOVINGCONFIG enabled={enabled} fixedStep=1 order=2 deferred={int(not incremental)}'
    if incremental:config+=' incremental=1'
    assert len(re.findall(config,log))==1
    assert 'DRIVEDONE valid=1' in log
    assert re.findall(r'A44BUFFER phase=(\d+) requested=2 actual=(\d+)',log) == [(str(i),'2') for i in range(6)]
    summaries = [tuple(map(int, row)) for row in re.findall(
        r'A44MOVING phase=(\d+) enabled=(\d+) frames=(\d+) gasFrames=(\d+) requested=(\d+) submitted=(\d+) fallback=(\d+) prepares=(\d+) prepareTicks=(\d+) dirtyFrames=(\d+) captures=(\d+)', log)]
    assert [r[0] for r in summaries] == [0, 1, 2]
    local=[tuple(map(int,row)) for row in re.findall(r'A44LOCALPHASE phase=(\d+) rebuilds=(\d+) ticks=(\d+) heldReaders=(\d+)',log)]
    if incremental:
        assert [r[0] for r in local]==[0,1,2]
        assert all(r[1]>=49 and r[2]>0 for r in local),'Missing active local replacement'
        if enabled:assert all(r[3]>0 for r in local),'No replacement with old bank readers'
        assert all(r[7]==1 and r[9]==0 for r in summaries),'Whole-set amplification or deferred dirty cells remain'
    frames = [tuple(map(int, row)) for row in re.findall(
        r'A44MOVINGFRAME stage=(\d+) enabled=(\d+) requested=(\d+) submitted=(\d+) fallback=(\d+) dirty=(\d+)', log)]
    assert len({r[0] for r in frames}) == len(frames)
    for row in summaries:
        phase, actual, count, gas, requested, submitted, fallback, prepares, ticks, dirty, captures = row
        assert actual == enabled and count >= gas > 300 and prepares > 0 and ticks > 0
        selected = [f for f in frames if f[0] // 4096 == phase]
        assert len(selected) == captures >= 4
        assert all(f[1] == enabled and f[0] % 4096 >= 1320 and f[0] % 128 == 0 for f in selected)
        if not enabled:
            assert requested == submitted == fallback == 0
            assert all(f[2:5] == (0, 0, 0) for f in selected)
    rows = list(csv.DictReader((root / 'game/bin/drive-probe.csv').open()))
    pose = ('phase', 'offset', 'gas', 'x', 'y', 'z', 'speed', 'yaw', 'dt', 'damage',
            'eyeX', 'eyeY', 'eyeZ', 'lookX', 'lookY', 'lookZ', 'driver', 'vehicles')
    poses = [tuple(r[k] for k in pose) for r in rows if int(r['phase']) < 3]
    assert len(poses) > 1500
    return binding, summaries, frames, poses, local, incremental

def image(path, stage, kind):
    raw = path.read_bytes()
    assert len(raw) >= 16
    w, h, psm, identity = struct.unpack_from('<4I', raw)
    assert identity == stage and (w, h) == ((128, 128) if kind == 'env' else (448, 448))
    assert psm == (2 if kind == 'display' else 0)
    stride = 2 if psm == 2 else 4
    assert len(raw) == 16 + w * h * stride
    assert len(set(raw[16:])) > 16, 'Blank raster'
    return raw[16:]

off, off_summary, off_frames, off_pose, off_local, off_incremental = read(a.off, 'OFF')
on, on_summary, on_frames, on_pose, on_local, on_incremental = read(a.on, 'ON')
assert off_incremental==on_incremental,'Unmatched producer contracts'
assert off['elfSha256'] == on['elfSha256'] and off['files'] == on['files'], 'Not same ELF/sources'
assert off_pose == on_pose, 'Moving simulation/source poses differ'
assert [r[0] for r in off_frames] == [r[0] for r in on_frames], 'Unmatched capture schedule'
images = []
for frame in on_frames:
    stage = frame[0]
    for kind in ('main', 'env', 'display'):
        rel = f'game/bin/moving-{stage}-{kind}.bin'
        left, right = image(a.off / rel, stage, kind), image(a.on / rel, stage, kind)
        images.append(dict(stage=stage, kind=kind, file=rel,
                           offSha256=sha(a.off / rel), onSha256=sha(a.on / rel),
                           differentBytes=sum(x != y for x, y in zip(left, right))))
delivery = all(r[4] > 0 and r[4] == r[5] and r[6] == 0 for r in on_summary)
delivery = delivery and all(r[2] > 0 and r[2] == r[3] and r[4] == 0 for r in on_frames)
valid = delivery and all(row['differentBytes'] == 0 for row in images)
report = dict(valid=valid, experiment='A44-moving-cell-correctness', elfSha256=on['elfSha256'],
              sourceFiles=len(on['files']), poseRows=len(on_pose), deliveryValid=delivery,
              offSummary=off_summary, onSummary=on_summary, images=images,
              offLogSha256=sha(a.off / 'emulator/emulator.log'),
              onLogSha256=sha(a.on / 'emulator/emulator.log'), promotionAllowed=False,
              limits=['Forced moving order2, common slots and synchronous readbacks: no performance proof.',
                      'Daytime sampled driving only; not all poses, rebuild pressure or nighttime coverage.'])
if on_incremental:
    report.update(incremental=True,offLocalRebuilds=off_local,onLocalRebuilds=on_local)
(a.on / 'world-cell-moving-emulator.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(dict(valid=valid, deliveryValid=delivery, images=len(images),
                     exactImages=sum(row['differentBytes'] == 0 for row in images))))
assert valid, 'Moving complete-cell delivery or image mismatch'
