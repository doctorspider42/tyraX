import hashlib, json, pathlib
REPO=pathlib.Path('F:/Projects/tyra-editor')
LAB=pathlib.Path('F:/Projects/tyrax2-lab-20261001')
OUT=REPO/'docs/dynamic-light-receivers-2026-10-06'
OUT.mkdir(parents=True,exist_ok=True)
payloads={}; external={}; refs={}
def digest(data): return hashlib.sha256(data).hexdigest()
def put(rel,data,origin):
    dst=OUT/rel; dst.parent.mkdir(parents=True,exist_ok=True)
    if dst.exists() and dst.read_bytes()!=data: raise RuntimeError('Changed archived bytes: '+rel)
    dst.write_bytes(data)
    payloads[rel]={'sha256':digest(data),'bytes':len(data),'origin':str(origin)}
def add(src,rel,force=False):
    data=src.read_bytes()
    if not force and (src.suffix.lower() in {'.exe','.obj','.o','.elf','.sym','.bin','.pyc','.ps2','.dat'} or len(data)>350000):
        external[str(src)]={'sha256':digest(data),'bytes':len(data),'reason':'binary or oversized inventory/source; metadata only'}
        if src.suffix=='.json':
            try:
                value=json.loads(data)
                if isinstance(value,dict):
                    small={k:v for k,v in value.items() if len(json.dumps(v))<18000}
                    put(rel+'.projection.json',json.dumps({'kind':'nonreconstructive top-level projection','original':str(src),'originalSha256':digest(data),'retained':small},indent=2).encode(),src)
            except (ValueError,UnicodeError): pass
    else: put(rel,data,src)
def postimage(src,key):
    data=src.read_bytes(); sha=digest(data); rel='postimages/'+sha+'.txt'
    if rel not in payloads: put(rel,data,src)
    refs[key]={'payload':rel,'sha256':sha,'bytes':len(data),'origin':str(src)}
sources=['src/project.hpp','src/project.cpp','src/version.hpp','src/migrations.cpp','src/app.cpp','src/viewport.hpp','src/viewport.cpp','src/templates.cpp','src/game_templates.inc','vendor/tyra/engine/inc/renderer/3d/pipeline/shared/bag/pipeline_info_bag.hpp','vendor/tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_core.hpp','vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp']
for name in sources: postimage(REPO/name,'production/'+name)
names=['player-light-public-model-root-v1','player-light-public-model-root-v2','player-light-public-model-root-v3','player-light-public-cli-root-v1','player-light-public-ui-root-v1','player-light-public-ui-root-v2','player-light-public-command-encoder-v1','player-light-public-review-v1','player-light-public-runtime-review-v1','public-receiver-example-refresh-v1']
names += [p.name for p in LAB.glob('player-light-public-native-*') if p.is_dir()]
names += [p.name for p in LAB.glob('player-light-public-emulator-*') if p.is_dir() and 'animated' not in p.name and 'showcase' not in p.name]
names += ['player-light-public-runtime-v1']
for name in sorted(set(names)):
    base=LAB/name
    if not base.exists(): continue
    for src in sorted(base.rglob('*')):
        if not src.is_file(): continue
        parts=src.relative_to(base).parts
        if name == 'public-receiver-example-refresh-v1' and len(parts)>1: continue
        if any(p in {'fixtures','fixture','profile','__pycache__','obj','bin','cache','res','.res-baked'} for p in parts): continue
        add(src,'evidence/'+name+'/'+src.relative_to(base).as_posix())
for fixture in ['vehicle-players','vehicle-all','animated-players','animated-all','showcase-players']:
    base=LAB/'player-light-public-runtime-v1/fixtures'/fixture
    for name in ['inc/terrain_game.hpp','src/gen/game_physics.gen.cpp','src/gen/game_lighting.gen.cpp','src/gen/game_scene.gen.cpp','src/gen/game_vehicles.gen.cpp']:
        src=base/name
        if src.exists(): postimage(src,'generated/'+fixture+'/'+name)
    for src in (base/'bin').glob('*') if (base/'bin').exists() else []:
        if src.is_file() and src.suffix.lower() in {'.elf','.sym'}: add(src,'unused-binary/'+fixture+'/'+src.name)
    config=base/'inc/scene_data.hpp'
    if config.exists():
        data=config.read_bytes(); external[str(config)]={'sha256':digest(data),'bytes':len(data),'reason':'full scene assets/constants omitted; policy excerpt stored'}
        lines=data.decode('utf-8').splitlines()
        excerpt='\n'.join(str(i+1)+': '+s for i,s in enumerate(lines) if 'PLAYER_ONLY_DYNAMIC_LIGHTS' in s)
        put('policy/'+fixture+'.txt',excerpt.encode(),config)
put('.gitattributes',b'* -text whitespace=-trailing-space,-space-before-tab,cr-at-eol\n','archive byte-preservation rule')
readme='''# Public dynamic light receivers: validation archive

This archive binds the public All/Players option to source, actual model/CLI/UI/native records and owned emulator records. It is distinct from the private receiver performance trial and main11 fallback experiment. No hardware timing or public60fps gain is established here.

Root reviewed normal driven-car and exited-player images. The vehicle run log records movement and VEHexit, establishing movement/exit exercise; normal images alone do not prove every lamp/recipient, scalar pickup or secondary view ownership case. Animated/showcase records are included only when supplied. Evidence files retain their own status and scope; this archive does not upgrade incomplete or build-only records.

Excluded attempts: stale historical object model link v2; namespace-unaware native audit v1; capture attempts v1 mutable-log, v2 missing attached session and v3 auxiliary-window selection. Corrected model v3, namespace-aware native v2 and vehicle capture v4 are separate records. Initial PS2 launcher preflight failed ping before creating a run/device output or sending ELF; it supplies no physical test.

manifest.json lists stored payload hashes and content-addressed source postimages. externalArtifacts lists metadata-only binaries and oversized inventories. JSON projections and policy excerpts explicitly cannot reconstruct omitted originals. Assets, build objects/caches/profile files and complete fixture inventories are excluded. SHA256 metadata is provenance, not independent replay/execution proof.

Use the external LAB verify-public-receiver-archive-v1.py for filesystem and staged blob verification. Root owns final native ABI, runtime and release qualification; no stage/commit was performed by the archivist.
'''
put('README.md',readme.encode(),'archivist qualification boundaries')
for helper in ['archive-public-receivers-v1.py','verify-public-receiver-archive-v1.py']:
    add(LAB/helper,'tools/'+helper,True)
manifest={'schema':1,'scope':'public source/build/model/UI/emulator evidence; no hardware pricing','payloads':payloads,'postimages':refs,'externalArtifacts':external}
expected=set(payloads)|{'manifest.json'}
for stale in OUT.rglob('*'):
    if stale.is_file() and stale.relative_to(OUT).as_posix() not in expected:
        assert stale.resolve().is_relative_to(OUT.resolve())
        stale.unlink()  # Only this newly created archive's superseded payloads.
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'payloads':len(payloads),'postimages':len(refs),'external':len(external),'storedBytes':sum(v['bytes'] for v in payloads.values())}))
