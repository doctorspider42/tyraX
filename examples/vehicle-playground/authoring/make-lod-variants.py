"""Create isolated Motor District LOD candidates for a measured A/B run.

The checked-in district deliberately keeps LOD disabled until a candidate has
been built, driven and compared in PCSX2.  This copies the complete example so
generated sources, assets and object paths remain self-contained.
"""
from pathlib import Path
import argparse
import json
import shutil

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('destination', type=Path,
                    help='new empty directory for the copied candidates')
args = parser.parse_args()
if args.destination.exists():
    raise SystemExit(f'{args.destination} already exists; refusing to overwrite it')

presets = {
    # Vehicle/model silhouettes stay at full quality through the usual chase
    # camera distance. Terrain gets its own candidate because a coarse tile
    # can intersect a terrain-projected road even though collision stays full.
    'model-64': (64, 0),
    'model-64-terrain-96': (64, 96),
}
for name, (mesh, terrain) in presets.items():
    dst = args.destination / name
    shutil.copytree(root, dst, ignore=shutil.ignore_patterns('bin', 'obj', 'build',
                    '.res-baked', '__pycache__'))
    manifest = dst / 'vehicle-playground.tyra'
    project = json.loads(manifest.read_text(encoding='utf-8'))
    project['settings']['meshLodDistance'] = mesh
    project['settings']['terrainLodDistance'] = terrain
    manifest.write_text(json.dumps(project, indent=2) + '\n', encoding='utf-8')
    (dst / 'LOD-CANDIDATE.md').write_text(
        f'# Motor District LOD candidate: {name}\n\n'
        f'Mesh LOD distance: {mesh} units. Terrain LOD distance: {terrain} units.\n\n'
        'Refresh generated sources, build, then compare this candidate against '
        'the unmodified district at the same camera. Drive through the LOD '
        'boundaries; check each vehicle silhouette, lights and reflected scenery, '
        'and inspect every road/terrain crossing before accepting a result.\n',
        encoding='utf-8')
    print(dst)
