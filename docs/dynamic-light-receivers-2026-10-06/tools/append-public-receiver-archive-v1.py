import argparse,hashlib,json,pathlib
p=argparse.ArgumentParser();p.add_argument('directories',nargs='*');a=p.parse_args()
repo=pathlib.Path('F:/Projects/tyra-editor');lab=pathlib.Path('F:/Projects/tyrax2-lab-20261001');out=repo/'docs/dynamic-light-receivers-2026-10-06'
m=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
def store(src,rel):
    data=src.read_bytes(); sha=hashlib.sha256(data).hexdigest(); dst=out/rel
    assert dst.resolve().is_relative_to(out.resolve())
    if dst.exists(): assert dst.read_bytes()==data,rel
    dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(data)
    m['payloads'][rel]={'sha256':sha,'bytes':len(data),'origin':str(src)}
    if str(src) in m['externalArtifacts']:del m['externalArtifacts'][str(src)]
# Screenshots are visual evidence even when larger than inventory cutoff.
for origin in list(m['externalArtifacts']):
    src=pathlib.Path(origin)
    if src.suffix.lower()=='.png':
        assert hashlib.sha256(src.read_bytes()).hexdigest()==m['externalArtifacts'][origin]['sha256']
        store(src,'evidence/'+src.relative_to(lab).as_posix())
for name in a.directories:
    base=lab/name;assert base.is_dir() and base.resolve().is_relative_to(lab.resolve())
    for src in sorted(base.rglob('*')):
        if not src.is_file():continue
        rel=src.relative_to(base)
        if any(q in {'profile','cache','memcards','__pycache__','obj','bin','fixtures','fixture','res'} for q in rel.parts):continue
        if src.suffix.lower() in {'.exe','.obj','.o','.elf','.sym','.bin','.pyc','.ps2','.dat'}:continue
        store(src,'evidence/'+name+'/'+rel.as_posix())
store(pathlib.Path(__file__),'tools/append-public-receiver-archive-v1.py')
(out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'payloads':len(m['payloads']),'postimages':len(m['postimages']),'external':len(m['externalArtifacts']),'storedBytes':sum(r['bytes'] for r in m['payloads'].values())}))
