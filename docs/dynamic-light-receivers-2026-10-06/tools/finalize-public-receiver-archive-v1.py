import pathlib,json,hashlib
r=pathlib.Path('F:/Projects/tyra-editor');o=r/'docs/dynamic-light-receivers-2026-10-06';m=json.loads((o/'manifest.json').read_text())
def put(rel,d,origin):
    p=o/rel;assert p.resolve().is_relative_to(o.resolve())
    if p.exists():assert p.read_bytes()==d,rel
    p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(d)
    m['payloads'][rel]={'sha256':hashlib.sha256(d).hexdigest(),'bytes':len(d),'origin':str(origin)}
# Canonical authored/generated baseline tree is not the compact evidence;
# source-proof/manifest-delta/pins retain its exact preparation provenance.
prefix='evidence/player-light-public-runtime-v1/showcase-gate-players-prep-v1/canonical-baseline/'
removed=[]
for rel in list(m['payloads']):
    if rel.startswith(prefix):
        p=o/rel;assert p.resolve().is_relative_to(o.resolve());p.unlink()
        removed.append(rel);del m['payloads'][rel]
for name in ['docs/dynamic-light-receivers.md','docs/backlog.md','.agents/skills/tyra-engine-dev/SKILL.md','.claude/skills/tyra-engine-dev/SKILL.md']:
    p=r/name;d=p.read_bytes();sha=hashlib.sha256(d).hexdigest();rel='postimages/'+sha+'.txt';put(rel,d,p)
    m['postimages']['final-docs/'+name]={'payload':rel,'sha256':sha,'bytes':len(d),'origin':str(p)}
p=o/'final-validation-summary.md';put(p.name,p.read_bytes(),'root completed public qualification boundaries')
p=pathlib.Path(__file__);put('tools/'+p.name,p.read_bytes(),p)
(o/'manifest.json').write_text(json.dumps(m,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'payloads':len(m['payloads']),'postimages':len(m['postimages']),'metadataOnly':len(m['externalArtifacts']),'removedDuplicateBaselineFiles':len(removed),'storedBytes':sum(v['bytes'] for v in m['payloads'].values())}))
