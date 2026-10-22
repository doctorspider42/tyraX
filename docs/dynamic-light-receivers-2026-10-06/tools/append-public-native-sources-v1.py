import argparse,hashlib,json,pathlib
p=argparse.ArgumentParser();p.add_argument('--fixture',required=True);p.add_argument('directories',nargs='*');a=p.parse_args()
lab=pathlib.Path('F:/Projects/tyrax2-lab-20261001');o=pathlib.Path('F:/Projects/tyra-editor/docs/dynamic-light-receivers-2026-10-06');m=json.loads((o/'manifest.json').read_text())
def sha(d):return hashlib.sha256(d).hexdigest()
def put(rel,d,origin):
    t=o/rel;assert t.resolve().is_relative_to(o.resolve())
    if t.exists():assert t.read_bytes()==d,rel
    t.parent.mkdir(parents=True,exist_ok=True);t.write_bytes(d)
    m['payloads'][rel]={'sha256':sha(d),'bytes':len(d),'origin':str(origin)}
for name in a.directories:
    b=lab/name;assert b.is_dir()
    for s in sorted(b.iterdir()):
        if not s.is_file():continue
        d=s.read_bytes();rel='evidence/'+name+'/'+s.name
        if len(d)>350000:
            m['externalArtifacts'][str(s)]={'sha256':sha(d),'bytes':len(d),'reason':'oversized native inventory or full symbol text; metadata only'}
            if s.suffix=='.json':
                v=json.loads(d);small={k:x for k,x in v.items() if len(json.dumps(x))<18000}
                put(rel+'.projection.json',json.dumps({'kind':'nonreconstructive top-level projection','original':str(s),'originalSha256':sha(d),'retained':small},indent=2).encode(),s)
        else:put(rel,d,s)
b=lab/'player-light-public-runtime-v1/fixtures'/a.fixture
for name in ['inc/terrain_game.hpp','src/gen/game_physics.gen.cpp','src/gen/game_lighting.gen.cpp','src/gen/game_scene.gen.cpp','src/gen/game_vehicles.gen.cpp']:
    s=b/name
    if not s.exists():continue
    d=s.read_bytes();rel='postimages/'+sha(d)+'.txt';put(rel,d,s)
    m['postimages']['generated/'+a.fixture+'/'+name]={'payload':rel,'sha256':sha(d),'bytes':len(d),'origin':str(s)}
for s in (b/'bin').glob('*'):
    if s.is_file() and s.suffix in {'.elf','.sym'}:
        d=s.read_bytes();m['externalArtifacts'][str(s)]={'sha256':sha(d),'bytes':len(d),'reason':'binary only provenance'}
s=pathlib.Path(__file__);put('tools/'+s.name,s.read_bytes(),s)
(o/'manifest.json').write_text(json.dumps(m,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'payloads':len(m['payloads']),'postimages':len(m['postimages']),'external':len(m['externalArtifacts'])}))
