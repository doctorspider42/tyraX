"""Check collision sidecar lifecycle using the actual editor generator."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--editor', required=True)
    parser.add_argument('--scratch', required=True,
                        help="Primary checkout's ignored .tmp/tests directory")
    args = parser.parse_args()
    editor = str(Path(args.editor).resolve())
    scratch = Path(args.scratch).resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='collision-codegen-', dir=scratch) as work:
        def run(*parts):
            subprocess.run([editor, *parts], check=True, stdout=subprocess.DEVNULL)
        run('--new', 'collision_codegen', work, '12', '12', 'fpp')
        project = Path(work) / 'collision_codegen'
        manifest = project / 'collision_codegen.tyra'
        data = json.loads(manifest.read_text(encoding='utf-8'))
        object_id = 'testcollider'
        data['scenes'][0]['objects'].append(object_id)
        obj = dict(id=object_id, name='Mesh', type='model',
                   model='res/models/test.obj', collision='mesh',
                   position=[0, 0, 0], rotation=[0, 0, 0], scale=[1, 1, 1])
        model = project / 'res/models/test.obj'
        model.parent.mkdir(exist_ok=True)
        model.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n', encoding='utf-8')
        object_file = project / 'objects' / (object_id + '.json')
        object_file.write_text(json.dumps(obj), encoding='utf-8')
        manifest.write_text(json.dumps(data), encoding='utf-8')
        run('--refresh-gen', str(project))
        sidecar = model.with_suffix('.tmdl.tcol')
        assert sidecar.is_file(), 'mesh mode did not bake a collider'
        first = sidecar.read_bytes()
        timestamp = sidecar.stat().st_mtime_ns
        run('--refresh-gen', str(project))
        assert sidecar.read_bytes() == first
        assert sidecar.stat().st_mtime_ns == timestamp, 'no-op refresh rewrote the collider'
        assert '/models/*.tmdl.tcol' in (project / 'res/.gitignore').read_text()
        obj['collision'] = 'box'
        object_file.write_text(json.dumps(obj), encoding='utf-8')
        run('--refresh-gen', str(project))
        assert not sidecar.exists(), 'box mode left a stale collider staged'
    print('Collision codegen: create, stable bytes/mtime, ignore rule and stale removal passed')


if __name__ == '__main__':
    main()
