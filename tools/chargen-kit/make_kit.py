"""Builds resources/chargen-kit.bin from scratch.

    python make_kit.py <work_dir> [--blender <blender.exe>] [--skip-fetch]

  1. fetch_sources.py  -> <work_dir>/sources      (~1.3 GB of CC0 downloads)
  2. kit_body.py       -> <work_dir>/stage        (Blender: atlas + layers)
  3. kit_wear.py       -> <work_dir>/stage/wear   (Blender: 85 items)
  4. anim_retarget.py  -> <work_dir>/anims.json   (Blender: 87 clips)
  5. build_kit.py      -> resources/chargen-kit.bin

Takes ~30 minutes on a desktop CPU, most of it in kit_wear.py. Then rebuild
the editor (chargen_kit.cpp re-links the kit) and regenerate any example
characters with `tyrax-editor --chargen <recipe> <glb>`.
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
    run(bl + [os.path.join(HERE, 'kit_body.py'), '--', os.path.join(src, 'sys'), mh, stage, '1024',
              os.path.join(src, 'targets')])
    run(bl + [os.path.join(HERE, 'kit_wear.py'), '--', os.path.join(src, 'sys'),
              os.path.join(src, 'packs'), mh, stage])
    ual = os.path.join(src, 'ual')
    anims = os.path.join(work, 'anims.json')
    run(bl + [os.path.join(HERE, 'anim_retarget.py'), '--', anims,
              '--ual1', os.path.join(ual, 'universal_animation_librarystandard', 'Animation Library[Standard]',
                                     'Godot', 'AnimationLibrary_Godot_Standard.glb'),
              '--ual2', os.path.join(ual, 'universal_animation_library_2standard',
                                     'Universal Animation Library 2 [Standard]', 'Unreal-Godot',
                                     'UAL2_Standard.glb'),
              '--mh', mh])
    run([sys.executable, os.path.join(HERE, 'build_kit.py'), stage, mh, os.path.join(src, 'targets'),
         os.path.join(REPO, 'resources', 'chargen-kit.bin'), anims])


if __name__ == '__main__':
    main()
