"""Builds resources/chargen-kit.bin from scratch.

    python make_kit.py <work_dir> [--blender <blender.exe>] [--skip-fetch]

Fetch the reference data, bake the five UV layouts in Blender, retarget the
motion library, and compile the studio sculpts and wardrobe into the kit.
Rebuild the editor and export the example recipes after changing the kit.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def find_blender(explicit):
    if explicit:
        return explicit
    if os.environ.get('BLENDER'):
        return os.environ['BLENDER']
    on_path = shutil.which('blender')
    if on_path:
        return on_path
    roots = [r'C:\Program Files\Blender Foundation', '/Applications/Blender.app/Contents/MacOS']
    for root in roots:
        if os.path.isdir(root):
            for d in sorted(os.listdir(root), reverse=True):
                for exe in ('blender.exe', 'blender', 'Blender'):
                    p = os.path.join(root, d, exe) if 'Foundation' in root else os.path.join(root, exe)
                    if os.path.exists(p):
                        return p
    sys.exit('Blender not found: pass --blender or set BLENDER')


def run(cmd):
    print('>>', ' '.join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def main():
    args = sys.argv[1:]
    work = os.path.abspath(args[0])
    blender = find_blender(args[args.index('--blender') + 1] if '--blender' in args else None)
    src = os.path.join(work, 'sources')
    stage = os.path.join(work, 'stage')
    if '--skip-fetch' not in args:
        run([sys.executable, os.path.join(HERE, 'fetch_sources.py'), src])
    bl = [blender, '-b', '--factory-startup', '--python']
    mh = os.path.join(src, 'mh-data')
    # Each proxy supplies its own target projections and UV layout.
    bodies = [(stage, 'female1605'), (os.path.join(work, 'stage_m'), 'male1591'),
              (os.path.join(work, 'stage_hf'), 'female1605@sub'),
              (os.path.join(work, 'stage_hm'), 'male1591@sub'),
              (os.path.join(work, 'stage_c'), 'proxy741')]
    for st_, proxy in bodies:
        run(bl + [os.path.join(HERE, 'kit_body.py'), '--', os.path.join(src, 'sys'), mh, st_, '1024',
                  os.path.join(src, 'targets'), proxy])
    ual = os.path.join(src, 'ual')
    anims = os.path.join(work, 'anims.json')
    run(bl + [os.path.join(HERE, 'anim_retarget.py'), '--', anims,
              '--ual1', os.path.join(ual, 'universal_animation_librarystandard', 'Animation Library[Standard]',
                                     'Godot', 'AnimationLibrary_Godot_Standard.glb'),
              '--ual2', os.path.join(ual, 'universal_animation_library_2standard',
                                     'Universal Animation Library 2 [Standard]', 'Unreal-Godot',
                                     'UAL2_Standard.glb'),
              '--mh', mh])
    run([sys.executable, os.path.join(HERE, 'build_kit.py'), ','.join(b[0] for b in bodies), mh,
         os.path.join(src, 'targets'),
         os.path.join(REPO, 'resources', 'chargen-kit.bin'), anims])


if __name__ == '__main__':
    main()
