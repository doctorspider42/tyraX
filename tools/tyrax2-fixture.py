"""Create an isolated current Motor District/engine pair for TyraX2 gates."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--project', type=Path)
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--pose', choices=['day', 'night'], default='day')
    parser.add_argument('--mode', choices=['plain', 'timing', 'check', 'hold'],
                        default='timing')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    project = (args.project or repo/'examples/vehicle-playground').resolve()
    engine = (args.engine or repo/'vendor/tyra').resolve()
    destination = args.destination.resolve()
    if destination.exists():
        parser.error('Destination already exists; choose a new directory')
    if any(destination.is_relative_to(x) for x in (project, engine, repo)):
        parser.error('Destination must be outside the repository and sources')
    manifests = list(project.glob('*.tyra'))
    if len(manifests) != 1 or not (engine/'engine/Makefile').is_file():
        parser.error('Expected one project manifest and a complete Tyra engine')
    model = json.loads(manifests[0].read_text(encoding='utf-8-sig'))
    mood = next((v for v in model.get('saveValues', [])
                 if v['name'] == 'district-night'), None)
    if mood is None:
        parser.error('Expected the Motor District district-night save value')
    for key in ('remotePad', 'liveDebug', 'liveLink', 'liveLogic',
                'timeMachine', 'inputRecorder', 'showMemory'):
        model['settings'][key] = False
    mood['default'] = int(args.pose == 'night')
    destination.mkdir(parents=True)
    game = destination/'game'
    shutil.copytree(project, game, ignore=shutil.ignore_patterns(
        'obj', 'logs', 'authoring', '.vscode', 'screenshots', '*.history',
        '*.elf', 'livedbg.bin', 'livedbg.cmd', 'livepad.bin', 'frame.tga',
        'hardware-trace.cfg', 'hardware-trace.csv', 'ps2link.run'))
    (game/manifests[0].name).write_text(json.dumps(model, indent=2)+'\n',
                                      encoding='utf-8')
    target = destination/'tyra'
    shutil.copytree(engine/'engine', target/'engine',
                    ignore=shutil.ignore_patterns('obj', 'bin'))
    shutil.copy2(engine/'Makefile.base', target/'Makefile.base')
    switches = {}
    if args.mode == 'timing': switches['TYRA_FRAME_PROFILE'] = 2
    if args.mode == 'check': switches['TYRA_VIF1_CHAIN_CHECK'] = 1
    if args.mode == 'hold':
        switches.update(TYRA_VIF1_QUEUE_HOLD=1, TYRA_VIF1_QUEUE_DEPTH=80)
    headers = {
        'TYRA_FRAME_PROFILE': 'debug/frame_profile.hpp',
        'TYRA_VIF1_CHAIN_CHECK': 'renderer/core/paths/path1/vif1_chain_check.hpp',
        'TYRA_VIF1_QUEUE_HOLD': 'renderer/core/paths/path1/vif1_queue.hpp',
        'TYRA_VIF1_QUEUE_DEPTH': 'renderer/core/paths/path1/vif1_queue.hpp',
    }
    defaults = {'TYRA_FRAME_PROFILE': 0, 'TYRA_VIF1_CHAIN_CHECK': 0,
                'TYRA_VIF1_QUEUE_HOLD': 0, 'TYRA_VIF1_QUEUE_DEPTH': 4}
    for key, value in switches.items():
        header = target/'engine/inc'/headers[key]
        source = header.read_text(encoding='utf-8-sig')
        needle = f'#define {key} {defaults[key]}'
        if source.count(needle) != 1:
            raise RuntimeError(f'Unexpected switch definition: {key}')
        header.write_bytes(source.replace(needle, f'#define {key} {value}').encode())
    revision = subprocess.run(['git', '-C', str(repo), 'rev-parse', 'HEAD'],
                              capture_output=True, text=True, check=True).stdout.strip()
    record = {'revision': revision, 'project': str(project), 'engine': str(engine),
              'pose': args.pose, 'mode': args.mode, 'switches': switches,
              'settings': model['settings'],
              'source_assets': {str(f.relative_to(project)): hashlib.sha256(
                  f.read_bytes()).hexdigest() for f in (project/'res').rglob('*')
                  if f.is_file()},
              'engine_files': {str(f.relative_to(target)): hashlib.sha256(
                  f.read_bytes()).hexdigest() for f in target.rglob('*')
                  if f.is_file()}}
    (destination/'fixture.json').write_text(json.dumps(record, indent=2)+'\n',
                                           encoding='utf-8')
    print(f'Game: {game}\nEngine: {target}\nRefresh generated files before building.')


if __name__ == '__main__':
    main()
