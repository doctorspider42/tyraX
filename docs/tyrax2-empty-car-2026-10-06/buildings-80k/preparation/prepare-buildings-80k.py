from pathlib import Path
import shutil,json,subprocess,difflib,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');parent=lab/'tyrax2-closeout-road-two-cars-light-buildings-v1';root=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-v1';assert not root.exists();root.mkdir();game=root/'empty-car';base=root/'parent-generated-baseline';ignore=lambda d,n:[x for x in n if x in('bin','obj','__pycache__','.git')or x.endswith(('.elf','.log'))]
for dest in(game,base):shutil.copytree(parent/'empty-car',dest,ignore=ignore)
shutil.copytree(parent/'tyra',root/'tyra',ignore=lambda d,n:[x for x in n if x in('bin','obj','__pycache__')]);shutil.copytree(parent/'empty-car/bin',game/'bin',ignore=lambda d,n:[x for x in n if x.endswith(('.elf','.sym'))or x in('closeout-samples.txt','log.txt','livedbg.cmd','frame.tga')])
d=json.loads((game/'empty-car.tyra').read_text());d['scenes'][0]['objects']=[x for x in d['scenes'][0]['objects']if not x.startswith('onlybuilding')];placements=[]
for row in range(25):
 for side in(-1,1):
  i=len(placements);name='facade-north'if side<0 else'facade-west';o={'id':f'onlybuilding{i:03}','name':f'Only building {i} {name}','type':'model','position':[side*6.6,0,12+8*row],'rotation':[0,90,0],'scale':[1,1,1],'color':[1,1,1],'physics':False,'collision':'none','castShadow':False,'projShadow':False,'shadowMode':0,'model':'res/models/kenney/'+name+'.obj'};placements.append(o);(game/'objects'/(o['id']+'.json')).write_text(json.dumps(o,indent=2)+'\n');d['scenes'][0]['objects'].append(o['id'])
(game/'empty-car.tyra').write_text(json.dumps(d,indent=2)+'\n')
for label,path in [('parent',base),('80k',game)]:
 for flag in('--resave','--refresh-gen'):
  result=subprocess.run(['F:/Projects/tyra-editor/build/tyrax-editor.exe',flag,str(path)],capture_output=True);(root/(label+'-'+flag[2:]+'.stdout')).write_bytes(result.stdout);(root/(label+'-'+flag[2:]+'.stderr')).write_bytes(result.stderr);assert result.returncode==0
proof=[]
for name in('inc/scene_data.hpp','inc/terrain_heights.gen.hpp','src/main.cpp','src/terrain_game.cpp','src/gen/game_vehicles.gen.cpp','src/gen/game_collision.gen.cpp','src/gen/game_scene.gen.cpp'):
 before=(base/name).read_text(encoding='utf8');after=(parent/'empty-car'/name).read_text(encoding='utf8');target=(game/name).read_text(encoding='utf8');ba=before.splitlines(keepends=True);aa=after.splitlines(keepends=True);groups=list(difflib.SequenceMatcher(None,ba,aa,autojunk=False).get_grouped_opcodes(3))
 for group in reversed(groups):
  old=''.join(ba[group[0][1]:group[-1][2]]);new=''.join(aa[group[0][3]:group[-1][4]]);assert target.count(old)==1,(name,old[:100],target.count(old));target=target.replace(old,new,1)
 (game/name).write_bytes(target.encode('utf8'));proof.append({'file':name,'diagnosticGroups':len(groups)})
p=game/'src/gen/game_scene.gen.cpp';s=p.read_text(encoding='utf8');assert s.count('owner<=12')==1;s=s.replace('owner<=12','owner<=54');p.write_bytes(s.encode('utf8'))
p=game/'src/gen/game_collision.gen.cpp';s=p.read_text(encoding='utf8');assert s.count('unsigned mask=0;bool foreign')==1;s=s.replace('unsigned mask=0;bool foreign','unsigned long long mask=0;bool foreign').replace('owner<=12)mask|=1U<<unsigned(owner-5)','owner<=54)mask|=EmptyWitness::buildingBit(unsigned(owner-5))');p.write_bytes(s.encode('utf8'))
p=root/'tyra/engine/inc/debug/empty_witness.hpp';s=p.read_text(encoding='utf8');s=s.replace('buildingRefs[8]','buildingRefs[50]').replace('buildingPointers[256]','buildingPointers[512]').replace('inline unsigned buildingMasks[256]{};','inline unsigned long long buildingMasks[512]{};\ninline unsigned long long buildingBit(unsigned i){if(i>=50||i>=64){NightSampler::valid=false;return 0;}return 1ULL<<i;}')
s=s.replace('inline void buildingCandidate(const void* bag,unsigned mask){if(buildingCount<256)','inline void buildingCandidate(const void* bag,unsigned long long mask){if(!mask||(mask>>50)){NightSampler::valid=false;return;}if(buildingCount<512)')
s=s.replace('i<8','i<50').replace('unsigned bm=carrier>=6&&carrier<=13?1U<<(carrier-6):0','unsigned long long bm=carrier>=6&&carrier<=55?buildingBit(carrier-6):0').replace('bm&(1U<<i)','bm&buildingBit(i)')
start=s.index('printf("LOG: BUILDINGRENDER');end=s.index('\nprintf("LOG: MODELRENDER',start);fmt=' '.join('refs'+str(i)+'=%u'for i in range(50));args=','.join('buildingRefs['+str(i)+']'for i in range(50));s=s[:start]+'printf("LOG: BUILDINGRENDER phase=%u offset=%u acceptedBags=%u acceptedVertices=%u '+fmt+' valid=%u\\n",frame/1800,frame%1800,buildingBags,buildingVertices,'+args+',unsigned(NightSampler::valid));'+s[end:];p.write_bytes(s.encode('utf8'))
p=game/'src/terrain_game.cpp';s=p.read_text(encoding='utf8').replace('route=8','route=9');p.write_bytes(s.encode('utf8'))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();d=json.loads((parent/'source-review.json').read_text());d['files']={n:sha(game/n[5:]if n.startswith('game/')else root/n)for n in d['files']}
for part in('src','inc'):
 for p in(game/part).rglob('*'):
  if p.is_file():d['files']['game/'+part+'/'+p.relative_to(game/part).as_posix()]=sha(p)
d.update(status='PRIVATE_TWO_CARS_LIGHT_PINE_FIFTY_BUILDINGS_80K_TARGET_SOURCE_READY_NOT_BUILT',head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),game=str(game),engine=str(root/'tyra'),route=9,parentEightBuildingsNativeProofSha256=sha(parent/'native-proof.json'),parentEightBuildingsSourceReviewSha256=sha(parent/'source-review.json'),authoredObjectFiles={p.name:sha(p)for p in(game/'objects').glob('*.json')},authoredObjects={'playerNonrendering':1,'vehicle':2,'road':1,'modelPine':1,'modelBuildings':50,'other':0},buildings=placements,normalGeneratorDeltaReplay=proof,coldBuildingIdentity='50 owner refs owners5..54, checked64-bit mask indices0..49,512 bounded stable carrier slots, shown-members-only; accepted shared carrier counted once. All scans remain cold.',nominalAcceptedInputTarget=80133,nominalBuildingVertices=70800,targetNotRuntimeProven=True,runtimeReleased=False)
(root/'source-review.json').write_text(json.dumps(d,indent=2)+'\n');print('SOURCE_READY')
