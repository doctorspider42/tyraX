from pathlib import Path
import shutil,json,subprocess,difflib,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');parent=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-v1';root=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1';assert not root.exists();root.mkdir();game=root/'empty-car';base=root/'parent-generated-baseline'
ignore=lambda d,n:[x for x in n if x in('bin','obj','__pycache__','.git')or x.endswith(('.elf','.log'))]
for dest in(game,base):shutil.copytree(parent/'empty-car',dest,ignore=ignore)
shutil.copytree(parent/'tyra',root/'tyra',ignore=lambda d,n:[x for x in n if x in('bin','obj','__pycache__')])
shutil.copytree(parent/'empty-car/bin',game/'bin',ignore=lambda d,n:[x for x in n if x.endswith(('.elf','.sym'))or x in('closeout-samples.txt','log.txt','livedbg.cmd','frame.tga')])
p=game/'objects/emptyravager0001.json';d=json.loads(p.read_text(encoding='utf8'));d['shadowMode']=2;p.write_bytes((json.dumps(d,indent=2)+'\n').encode())
for label,path in [('parent',base),('shadow',game)]:
 for flag in('--resave','--refresh-gen'):
  r=subprocess.run(['F:/Projects/tyra-editor/build/tyrax-editor.exe',flag,str(path)],capture_output=True);(root/(label+'-'+flag[2:]+'.stdout')).write_bytes(r.stdout);(root/(label+'-'+flag[2:]+'.stderr')).write_bytes(r.stderr);assert r.returncode==0
proof=[]
for name in('inc/scene_data.hpp','inc/terrain_heights.gen.hpp','src/main.cpp','src/terrain_game.cpp','src/gen/game_vehicles.gen.cpp','src/gen/game_collision.gen.cpp','src/gen/game_scene.gen.cpp'):
 before=(base/name).read_text(encoding='utf8');after=(parent/'empty-car'/name).read_text(encoding='utf8');target=(game/name).read_text(encoding='utf8');ba=before.splitlines(keepends=True);aa=after.splitlines(keepends=True);groups=list(difflib.SequenceMatcher(None,ba,aa,autojunk=False).get_grouped_opcodes(3))
 for group in reversed(groups):
  old=''.join(ba[group[0][1]:group[-1][2]]);new=''.join(aa[group[0][3]:group[-1][4]]);assert target.count(old)==1,(name,old[:100],target.count(old));target=target.replace(old,new,1)
 (game/name).write_bytes(target.encode());proof.append({'file':name,'diagnosticGroups':len(groups)})
p=game/'src/terrain_game.cpp';s=p.read_text(encoding='utf8').replace('route=9','route=10');p.write_bytes(s.encode())
p=game/'src/gen/game_lighting.gen.cpp';s=p.read_text(encoding='utf8');start=s.index('void TerrainGame::updateAndRenderBlobShadows()');end=s.index('\nvoid TerrainGame::',start+10);chunk=s[start:end];assert chunk.count('renderWorldBag(b.bag.get());')==2
scope='if(EmptyWitness::active){if(b.objIndex!=1||b.bag->count!=54||!b.bag->texture||b.info->dynLightPick||b.info->spotLit||b.info->zTestType!=PipelineZTest_TestOnly)NightSampler::valid=false;EmptyWitness::carrier=56;} renderWorldBag(b.bag.get()); if(EmptyWitness::active)EmptyWitness::carrier=0;'
s=s[:start]+chunk.replace('renderWorldBag(b.bag.get());',scope)+s[end:];p.write_bytes(s.encode())
p=root/'tyra/engine/inc/debug/empty_witness.hpp';s=p.read_text(encoding='utf8');s=s.replace('inline unsigned modelBags=0,modelVertices=0;','inline unsigned modelBags=0,modelVertices=0,blobBags=0,blobVertices=0;').replace('buildingCount=0;for(unsigned','buildingCount=blobBags=blobVertices=0;for(unsigned')
s=s.replace('if(carrier==5){','if(carrier==56){++blobBags;blobVertices+=n;}if(carrier==5){')
s=s.replace('if(!bags||!vertices||billboards','if(blobBags!=1||blobVertices!=54||!bags||!vertices||billboards')
s=s.replace('roadBags+modelBags+buildingBags','roadBags+modelBags+buildingBags+blobBags').replace('roadVertices+modelVertices+buildingVertices','roadVertices+modelVertices+buildingVertices+blobVertices')
s=s.replace('bags-roadBags-modelBags-buildingBags','bags-roadBags-modelBags-buildingBags-blobBags').replace('vertices-roadVertices-modelVertices-buildingVertices','vertices-roadVertices-modelVertices-buildingVertices-blobVertices')
s=s.replace('printf("LOG: MODELRENDER','printf("LOG: BLOBRENDER phase=%u offset=%u owner=1 acceptedBags=%u acceptedVertices=%u valid=%u\\n",frame/1800,frame%1800,blobBags,blobVertices,unsigned(NightSampler::valid));\nprintf("LOG: MODELRENDER');p.write_bytes(s.encode())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();d=json.loads((parent/'source-review.json').read_text());d['files']={n:sha(game/n[5:]if n.startswith('game/')else root/n)for n in d['files']}
for part in('src','inc'):
 for p in(game/part).rglob('*'):
  if p.is_file():d['files']['game/'+part+'/'+p.relative_to(game/part).as_posix()]=sha(p)
d.update(status='PRIVATE_NORMAL_RAVAGER_BLOB_SOURCE_READY_NOT_BUILT',head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),game=str(game),engine=str(root/'tyra'),route=10,parent80kNativeProofSha256=sha(parent/'native-proof.json'),parent80kSourceReviewSha256=sha(parent/'source-review.json'),authoredObjectFiles={p.name:sha(p)for p in(game/'objects').glob('*.json')},normalGeneratorDeltaReplay=proof,nominalAcceptedInputTarget=80187,targetNotRuntimeProven=True,runtimeReleased=False,blob={'owner':1,'shadowMode':2,'castShadow':False,'globalBlobShadowsUnchanged':False,'normalFunctionsUnstubbed':True,'acceptedCarrier':56,'acceptedVerticesRequired':54,'unrelatedEightFxStubsRetained':True})
(root/'source-review.json').write_bytes((json.dumps(d,indent=2)+'\n').encode())
s=(parent/'native-proof.py').read_text(encoding='utf8').replace('tyrax2-closeout-road-two-cars-light-buildings-80k-v1','tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1')
needle="obj=gm/'obj/gen/game_collision.gen.o';"
s=s.replace(needle,"obj=gm/'obj/gen/game_lighting.gen.o';dep=gm/'obj/gen/game_lighting.gen.d';assert obj.is_file() and 'inc/debug/empty_witness.hpp' in dep.read_text();objects['game/normal-blob-cached-and-rebuilt-scope']={'actualObject':str(obj),'sha256':sha(obj),'actualDependency':str(dep),'dependencySha256':sha(dep)}\n"+needle)
(root/'native-proof.py').write_bytes(s.encode());print(str(root))
