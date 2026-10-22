"""Linux compile-only source/object/archive/ELF binding. Never a runtime release."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--label',choices=['vehicle'],default='vehicle');p.add_argument('--log',default='native-build.log');a=p.parse_args()
root=Path('/mnt/f/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-two-cars-light-model-v1');pkg=root;game=pkg/'empty-car';engine=root/'tyra/engine';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
review=pkg/'source-review.json';d=json.loads(review.read_text());log=pkg/a.log;text=log.read_text(errors='replace');assert not re.search(r'error:',text) and 'Native build complete:' in text
gm=re.search(r'-fdebug-prefix-map=(/home/spider/\.cache/tyrax/native/projects/[^= ]+)=',text);em=re.search(r'-fdebug-prefix-map=(/home/spider/\.cache/tyrax/native/engines/[^= ]+/tyra/engine)=',text);assert gm and em;gm=Path(gm.group(1));em=Path(em.group(1))
files=d['files'];mirrors={}
for name,digest in files.items():
 source=game/name[5:] if name.startswith('game/') else root/name
 assert sha(source)==digest,name
 if name=='game/bin/closeout.cfg':
  mirrors[name]={'actualPath':str(source),'sha256':digest,'role':'runtime config pin only, not compiled source'}
  continue
 mirror=gm/name[5:] if name.startswith('game/') else em/name[len('tyra/engine/'):] if name.startswith('tyra/engine/') else em.parent/'Makefile.base'
 assert mirror.is_file() and sha(mirror)==digest,(name,str(mirror));mirrors[name]={'actualPath':str(mirror),'sha256':digest}
objects={}
requiredGame=['inc/scene_data.hpp','inc/empty_route.hpp',str(em/'inc/debug/closeout.hpp'),str(em/'inc/debug/night_sampler.hpp')]
if a.label=='showcase':requiredGame+=['inc/gate_witness.hpp']
for name,base,required in [('game/terrain_game',gm,requiredGame),('engine/engine',em,['inc/debug/closeout.hpp','inc/debug/night_sampler.hpp']),('engine/renderer/core/renderer_core',em,['inc/debug/night_sampler.hpp'])]:
 rel=name.split('/',1)[1];obj=base/('obj/'+rel+'.o');dep=base/('obj/'+rel+'.d');t=dep.read_text();assert obj.is_file() and all(x in t for x in required),(name,required)
 objects[name]={'actualObject':str(obj),'sha256':sha(obj),'actualDependency':str(dep),'dependencySha256':sha(dep),'requiredDependencies':required}
if a.label=='showcase':
 obj=gm/'obj/gen/game_physics.gen.o';dep=gm/'obj/gen/game_physics.gen.d';assert obj.is_file() and 'inc/gate_witness.hpp' in dep.read_text();objects['game/gate-cross-branch']={'actualObject':str(obj),'sha256':sha(obj),'actualDependency':str(dep),'dependencySha256':sha(dep)}
obj=gm/'obj/gen/game_vehicles.gen.o';dep=gm/'obj/gen/game_vehicles.gen.d';assert obj.is_file() and 'inc/debug/empty_witness.hpp' in dep.read_text();objects['game/road-accepted-candidate']={'actualObject':str(obj),'sha256':sha(obj),'actualDependency':str(dep),'dependencySha256':sha(dep)}
obj=gm/'obj/gen/game_scene.gen.o';dep=gm/'obj/gen/game_scene.gen.d';assert obj.is_file() and 'inc/debug/empty_witness.hpp' in dep.read_text();objects['game/actual-object-carrier-scope']={'actualObject':str(obj),'sha256':sha(obj),'actualDependency':str(dep),'dependencySha256':sha(dep)}
obj=em/'obj/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.o';dep=em/'obj/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.d';assert obj.is_file() and 'inc/debug/empty_witness.hpp' in dep.read_text();objects['engine/actual-local-filter-VU-spot-lane']={'actualObject':str(obj),'sha256':sha(obj),'actualDependency':str(dep),'dependencySha256':sha(dep)}
elfs=list((game/'bin').glob('*.elf'));assert len(elfs)==1;elf=elfs[0];sym=Path(str(elf)+'.sym');assert sha(elf)==sha(gm/'bin'/elf.name) and sha(sym)==sha(gm/'bin'/sym.name)
tool='/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f/ee/bin';nm=subprocess.check_output([tool+'/mips64r5900el-ps2-elf-nm','-S','-C',str(sym)],text=True);(pkg/'actual-symbols.txt').write_text(nm);buffers={}
for name,size in [('NightSampler::samples',6144),('NightSampler::chunks',420)]:
 rows=[r for r in nm.splitlines() if r.endswith(' '+name)];assert len(rows)==1;tokens=rows[0].split();assert int(tokens[1],16)==size;buffers[name]={'address':int(tokens[0],16),'bytes':size,'type':tokens[2]}
if a.label=='showcase':
 for name,size in [('GateWitness::crossings',8),('GateWitness::other',4)]:
  rows=[r for r in nm.splitlines() if r.endswith(' '+name)];assert len(rows)==1 and int(rows[0].split()[1],16)==size;buffers[name]={'address':int(rows[0].split()[0],16),'bytes':size}
for name in ('EmptyWitness::bags','EmptyWitness::vertices','EmptyWitness::billboards','EmptyWitness::roadBags','EmptyWitness::roadVertices','EmptyWitness::roadCount','EmptyWitness::modelBags','EmptyWitness::modelVertices'):
 rows=[r for r in nm.splitlines()if r.endswith(' '+name)];assert len(rows)==1 and int(rows[0].split()[1],16)==4;buffers[name]={'address':int(rows[0].split()[0],16),'bytes':4}
for name,size in [('EmptyWitness::carrier',4),('EmptyWitness::carrierBags',16),('EmptyWitness::carrierVertices',16),('EmptyWitness::selectedLights',16),('EmptyWitness::effectiveLights',16)]:
 rows=[r for r in nm.splitlines()if r.endswith(' '+name)];assert len(rows)==1 and int(rows[0].split()[1],16)==size;buffers[name]={'address':int(rows[0].split()[0],16),'bytes':size}
proof={'status':'PASS_PRIVATE_TWO_CARS_LIGHT_STATIC_MODEL_NATIVE_SOURCE_OBJECT_LINK_BINDING','sourceReviewSha256':sha(review),'nativeBuildLogSha256':sha(log),'actualELF':str(elf),'actualELFSha256':sha(elf),'actualSymbolSha256':sha(sym),'actualSourceMirrors':mirrors,'objects':objects,'engineArchiveSha256':sha(em/'bin/libtyra.a'),'linkedBuffers':buffers,'warningLines':[r for r in text.splitlines() if 'warning:' in r],'kind':4,'route':d['route'],'quietDefine':True,'rootIndependentReviewPending':True,'runtimeReleased':False,'devicesExecuted':False,'hardwarePerformanceQualified':False,'commonScaffoldingCostUnpriced':True}
(pkg/'native-proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(proof['status']);print(proof['actualELFSha256'])
