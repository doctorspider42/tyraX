import argparse,hashlib,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--staged',action='store_true');a=p.parse_args()
repo=pathlib.Path('F:/Projects/tyra-editor'); root='docs/dynamic-light-receivers-2026-10-06'
def read(rel):
    if a.staged:return subprocess.check_output(['git','show',':'+root+'/'+rel],cwd=repo)
    return (repo/root/rel).read_bytes()
m=json.loads(read('manifest.json'))
for rel,record in m['payloads'].items():
    assert not pathlib.PurePosixPath(rel).is_absolute() and '..' not in pathlib.PurePosixPath(rel).parts
    data=read(rel);assert len(data)==record['bytes'],rel
    assert hashlib.sha256(data).hexdigest()==record['sha256'],rel
for key,ref in m['postimages'].items():
    stored=m['payloads'][ref['payload']];assert ref['sha256']==stored['sha256'] and ref['bytes']==stored['bytes'],key
expected=set(m['payloads'])|{'manifest.json'}
if a.staged:
    actual={s[len(root)+1:] for s in subprocess.check_output(['git','ls-files',root],cwd=repo,text=True).splitlines()}
else:actual={f.relative_to(repo/root).as_posix() for f in (repo/root).rglob('*') if f.is_file()}
assert actual==expected,{'extra':sorted(actual-expected),'missing':sorted(expected-actual)}
for record in m['externalArtifacts'].values():assert len(record['sha256'])==64 and record['bytes']>=0
print(json.dumps({'status':'PASS_STORED_BYTES_AND_POSTIMAGE_CLOSURE','staged':a.staged,'payloads':len(m['payloads']),'postimages':len(m['postimages']),'metadataOnly':len(m['externalArtifacts']),'omittedArtifactsReconstructed':False}))
