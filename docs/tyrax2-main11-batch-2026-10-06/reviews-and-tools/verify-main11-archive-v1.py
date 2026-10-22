from pathlib import Path
import argparse, hashlib, json, subprocess

ap = argparse.ArgumentParser()
ap.add_argument('--repo', type=Path, default=Path('F:/Projects/tyra-editor'))
ap.add_argument('--staged', action='store_true')
a = ap.parse_args()
prefix = 'docs/tyrax2-main11-batch-2026-10-06'

def read(rel):
    rel = prefix + '/' + rel
    if a.staged:
        q = subprocess.run(['git', '-C', str(a.repo), 'show', ':' + rel], capture_output=True)
        assert q.returncode == 0, rel
        return q.stdout
    return (a.repo / rel).read_bytes()

sha = lambda data: hashlib.sha256(data).hexdigest()
manifest = json.loads(read('payload-manifest.json'))
for name, spec in manifest.items():
    data = read(name)
    assert len(data) == spec['bytes'] and sha(data) == spec['sha256'], name
blobs = set()
for version in ('v1', 'v2'):
    m = json.loads(read('fixtures/night-main11-batch-physical-' + version + '/target-source-manifest.json'))
    assert m['frozen'] and len(m['files']) == 501
    for name, digest in m['files'].items():
        assert sha(read('source-blobs/' + digest)) == digest, (version, name)
        blobs.add(digest)
assert {name.split('/')[-1] for name in manifest if name.startswith('source-blobs/')} == blobs
binary = json.loads(read('binary-hashes.json'))
assert all(len(v['sha256']) == 64 and v['bytes'] > 0 for v in binary.values())
print(json.dumps({'status':'PASS_MAIN11_ARCHIVE_INDEX' if a.staged else 'PASS_MAIN11_ARCHIVE_FILES',
                  'payloads':len(manifest), 'sourcePostimages':1002, 'uniqueSourceBlobs':len(blobs),
                  'hashOnlyBinaries':len(binary), 'runtimeOrPerformanceQualified':False}))
