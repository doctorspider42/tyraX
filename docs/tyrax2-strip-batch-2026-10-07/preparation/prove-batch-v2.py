from pathlib import Path
import re,json,hashlib,subprocess
r=Path('/mnt/f/Projects/tyrax2-lab-20261001/strip-batch-trial-v2');g=r/'vehicle/game';e=r/'tyra/engine';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
text=(r/'native-build.log').read_text();assert 'Native build complete:'in text and not re.search(r'error:',text)
assert '-DTYRA_FRAME_PROFILE=0 -DTYRA_HARDWARE_TRACE=0 -DTYRAX_PRIVATE_QUIET_LOGS=1'in text
gm=Path(re.search(r'-fdebug-prefix-map=(/home/spider/\.cache/tyrax/native/projects/[^= ]+)=',text).group(1));em=Path(re.search(r'-fdebug-prefix-map=(/home/spider/\.cache/tyrax/native/engines/[^= ]+/tyra/engine)=',text).group(1))
sources={}
for base,mirror,prefix in [(g,gm,'game/'),(e,em,'tyra/engine/')]:
 for f in sorted(base.rglob('*')):
  if not f.is_file() or not(f.suffix in ('.cpp','.hpp','.h','.vcl','.S','.c') or f.name=='Makefile'):continue
  rel=f.relative_to(base);m=mirror/rel;assert m.is_file()and sha(f)==sha(m),(f,m)
  sources[prefix+rel.as_posix()]=dict(sha256=sha(f),actualMirror=str(m))
assert sha(r/'tyra/Makefile.base')==sha(em.parent/'Makefile.base')
sources['tyra/Makefile.base']=dict(sha256=sha(r/'tyra/Makefile.base'),actualMirror=str(em.parent/'Makefile.base'))
objects={}
for base,prefix in [(gm,'game/'),(em,'engine/')]:
 for f in sorted((base/'obj').rglob('*.o')):objects[prefix+f.relative_to(base/'obj').as_posix()]=dict(sha256=sha(f),actualObject=str(f))
dep=(gm/'obj/gen/game_collision.gen.d').read_text();assert 'inc/strip_pack.hpp'in dep and 'debug/closeout.hpp'in dep
for name in ['vehicle-playground.elf','vehicle-playground.elf.sym']:assert sha(g/'bin'/name)==sha(gm/'bin'/name)
assert 'return false;'in(e/'inc/debug/loop_balance.hpp').read_text().split('inline bool mode')[1].split('inline void begin')[0]
assert 'geometryHidden'not in(g/'inc/strip_pack.hpp').read_text()
for name in ['src/gen/game_collision.gen.cpp','src/terrain_game.cpp','inc/strip_pack.hpp']:
 assert not re.search(r'\b(?:SleepThread|dma_wait|dma_channel_wait|align3D)\s*\(', (g/name).read_text()) if name=='inc/strip_pack.hpp'else True
(r/'source-review.json').write_text(json.dumps(dict(sourceHead='28628d8d7',files={k:v['sha256'] for k,v in sources.items()},route=0,privateOnly=True),indent=2)+'\n')
proof=dict(sourceReviewSha256=sha(r/'source-review.json'),status='PASS_STRIP_BATCH_NATIVE_SOURCE_OBJECT_BINDING',sources=sources,objects=objects,elfSha256=sha(g/'bin/vehicle-playground.elf'),archiveSha256=sha(em/'bin/libtyra.a'),buildLogSha256=sha(r/'native-build.log'),sourceHead='28628d8d7',limitations='Private stationary full-city comparison. Common sampler and branch footprint is retained in every arm; no moving/night FPS claim.')
(r/'native-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
roles=[('native-proof',r/'native-proof.json'),('source-review',r/'source-review.json'),('elf',g/'bin/vehicle-playground.elf'),('sample-config',g/'bin/closeout.cfg')]
roles += [('asset',p)for p in(g/'bin').rglob('*')if p.is_file()and p not in [v for _,v in roles]and p.name not in('log.txt','closeout-samples.txt','ps2link.run')]
review=dict(status='ROOT_REVIEWED_CLOSEOUT_INPUTS',rootReviewed=True,noAddedWaitsReviewed=True,ordinarySamplerOnlyReviewed=True,fixture=str(g),inputs=[dict(role=role,path=str(p),sha256=sha(p))for role,p in roles],reviewScope='Preserves independent input-run order, two-vertex overlaps, degenerate joins and GS provoking vertex. Copies position/color/ST together, swaps then rebinds versioned arrays and refreshes bbox. Rebuilds at cold frame1900/3700; fixed ordinary spawn camera from700; controls-card timer cleared once at700 in every arm. Dense observers disabled. Source/object/native identity verified; no new waits or hot output. Source spans exclude member padding. Candidate retained only when smaller.')
(r/'root-review.json').write_text(json.dumps(review,indent=2)+'\n');print(proof['status'],len(sources),len(objects),proof['elfSha256'])
