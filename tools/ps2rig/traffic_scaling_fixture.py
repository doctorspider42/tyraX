"""Prepare the authored five-HQ/eight-civilian Motor District fixture."""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import subprocess
import zipfile


def prepare(repo, project):
    common = Path(subprocess.check_output([
        'git', '-C', str(repo), 'rev-parse', '--path-format=absolute',
        '--git-common-dir'], text=True).strip())
    allowed = (common.parent/'.tmp').resolve()
    assert project != allowed and project.is_relative_to(allowed)
    manifest = project/'vehicle-playground.tyra'
    d = json.loads(manifest.read_text(encoding='utf8'))
    assert len(d['vehicles']) == 3 and d['scenes'][0]['name'] == 'main'
    backup = project.parent/'before-traffic-scaling.zip'
    if backup.exists():
        # A failed pre-write validation may be retried only on exact inputs.
        with zipfile.ZipFile(backup) as z:
            for name in z.namelist():
                assert (project/name).read_bytes() == z.read(name), name
    else:
        with zipfile.ZipFile(backup, 'x', zipfile.ZIP_DEFLATED) as z:
            z.write(manifest, manifest.name)
            for directory in ['objects', 'src', 'inc']:
                for p in (project/directory).rglob('*'):
                    if p.is_file():
                        z.write(p, p.relative_to(project))
            z.write(project/'bin/vehicle-playground.elf', 'bin/vehicle-playground.elf')
    settings = d['settings']
    settings['buildProfile'] = 'release'
    for key in ['remotePad', 'liveDebug', 'liveLink', 'liveLogic',
                'timeMachine', 'inputRecorder', 'keyboardMouse',
                'keyboardMousePs2Link', 'showMemory']:
        settings[key] = False
    # Rendered HQ means the actual near model, not merely an HQ definition
    # that has already switched to its authored far model at12 units.
    for definition in d['vehicles']:
        definition['trafficDistance'] = 48
    low = copy.deepcopy(d['vehicles'][1])
    # Imported GLBs keep authored near geometry; budget knobs do not decimate
    # them. The existing far asset has four named wheels and is a genuinely
    # smaller complete model, usable as a civilian's near model.
    low.update(id='benchcivilian001', name='Civilian Pica',
               model='res/models/pica-far.glb', fastWheel='', trafficDistance=48)
    d['vehicles'].append(low)
    settings['traffic'] = dict(cars=8, vehicles=['Civilian Pica'],
                               density=100, radius=110)
    main = d['scenes'][0]
    hq = []
    for oid in main['objects']:
        path = project/'objects'/(oid+'.json')
        obj = json.loads(path.read_text(encoding='utf8'))
        if obj.get('type') == 'vehicle':
            hq.append(obj)
    assert len(hq) == 3
    # Keep the original driver's OnStart/EnterVehicle graph. The other four
    # use normal waypoint AI and therefore cannot enter the parked sleep path.
    driver = next(o for o in hq if any(n['type'] == 'EnterVehicle'
                  for n in o.get('flowGraph', {}).get('nodes', [])))
    hq = [driver] + [o for o in hq if o is not driver]
    for index in [3, 4]:
        obj = copy.deepcopy(driver)
        obj.update(id=f'bench-racer-{index}', name=f'bench-racer-{index}')
        obj.pop('flowGraph', None)
        obj['vehicle']['driveable'] = False
        main['objects'].append(obj['id'])
        hq.append(obj)
    placements = [(0, -74.3035), (-4.875, -64), (-1.625, -64),
                  (-4.875, -54), (-1.625, -54)]
    for index, (obj, (x, z)) in enumerate(zip(hq, placements)):
        obj['position'] = [x, .5, z]
        obj['rotation'] = [0, 0, 0]
        if index:
            obj['vehicle']['route'] = 'bench-route-left-' if x < -3 else 'bench-route-right-'
        (project/'objects'/(obj['id']+'.json')).write_text(
            json.dumps(obj, indent=2)+'\n', encoding='utf8')
    for side, x in [('left', -4.875), ('right', -1.625)]:
        for index, z in enumerate([80, -95]):
            oid = f'bench-route-{side}-{index}'
            obj = dict(id=oid, name=oid, type='area', position=[x, 0, z],
                       rotation=[0, 0, 0], scale=[1, 1, 1], physics=False)
            (project/'objects'/(oid+'.json')).write_text(
                json.dumps(obj, indent=2)+'\n', encoding='utf8')
            main['objects'].append(oid)
    manifest.write_text(json.dumps(d, indent=2)+'\n', encoding='utf8')
    metadata = dict(source_commit=subprocess.check_output(
        ['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip(),
        phase_populations=[1, 3, 5, 10, 13, 13, 10, 5, 3, 1],
        hq_total=5, civilian_total=8,
        hq_ids=[o['id'] for o in hq], hq_positions=placements,
        civilian_definition=low,
        main_model_distance=48,
        collision_policy='Existing full vehicle model and collision handling. Civilians use the authored lower-poly asset; the ordinary baker measures its wheel anchors.',
        status='Authored input only; fresh bake, instrumentation and hardware admission pending.',
        manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest())
    target = project.parent/'traffic-scaling-metadata.json'
    target.write_text(json.dumps(metadata, indent=2)+'\n', encoding='utf8')
    print('Prepared five HQ TOTAL and eight authored lower-poly civilian slots; full bake required')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project', required=True, type=Path)
    p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    a = p.parse_args()
    prepare(a.repo.resolve(), a.project.resolve())
