"""Source-only append-only public receiver fixtures. No editor/build/runtime calls."""
from pathlib import Path
import argparse,copy,hashlib,json,shutil,struct
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,default=Path('F:/Projects/tyra-editor'));a=p.parse_args();root=Path(__file__).parent
exclude={'bin','obj','logs','screenshots','preview','authoring','.vscode','.git','_backup','__pycache__'}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
newNames=['portals-players','showcase-players','animated-all','animated-players']
assert all(not(root/'fixtures'/n).exists()for n in newNames)
assert not(root/'additional-fixture-index.json').exists()
records=[]
for example,name,mode in [('portals','portals-players','players'),('showcase','showcase-players','players'),('two-players','animated-all','all'),('two-players','animated-players','players')]:
 source=a.repo/'examples'/example;dest=root/'fixtures'/name
 shutil.copytree(source,dest,ignore=lambda d,n:[x for x in n if x in exclude or x.endswith(('.history','.log','.elf','.sym'))])
 manifest=dest/(example+'.tyra');d=json.loads(manifest.read_text(encoding='utf8'));d['formatVersion']=96;d['startScene']=0
 d['settings'].update(dynamicLightReceivers=mode,buildProfile='debug',remotePad=True,liveDebug=True,framePipeline=True,showFps=True)
 for scene in d['scenes']:
  scene.setdefault('overrides',{}).pop('dynamicLightReceivers',None);scene.setdefault('settings',{})['dynamicLightReceivers']='all'
 witness={}
 if name.startswith('animated-'):
  scene=d['scenes'][0];objects=dest/'objects';players=[json.loads((objects/(oid+'.json')).read_text(encoding='utf8'))for oid in scene['objects']if json.loads((objects/(oid+'.json')).read_text(encoding='utf8')).get('type')=='player'];assert len(players)==2
  clip='Armature|Armature|ArmatureAction';b=(dest/'res/models/cat.glb').read_bytes();n,t=struct.unpack_from('<II',b,12);asset=json.loads(b[20:20+n]);assert any(x.get('name')==clip and len(x.get('channels',[]))>0 for x in asset['animations'])
  d['settings'].update(multiplayer='split',p2JoinOnStart=True,ambient=.08,diffuse=0,lightColor=[1,1,1],brightness=1,animLodDistance=0,meshLodDistance=0)
  scene['overrides']['lighting']=False
  for i,o in enumerate(players):
   o['color']=[1,1,1];o['player']['thirdPerson']['idleClip']=clip;o['player']['flashlight']['enabled']=False
   (objects/(o['id']+'.json')).write_text(json.dumps(o,indent=2)+'\n',encoding='utf8')
   world=copy.deepcopy(o);world.update(id='f00dcafe0000000'+str(i+1),name='ordinary-animated-cat-'+str(i+1),type='model',position=[o['position'][0],o['position'][1],o['position'][2]-3],collision='none',anim=dict(clip=clip,autoplay=True,loop=True,speed=1))
   world.pop('player',None);world.pop('flowGraph',None);(objects/(world['id']+'.json')).write_text(json.dumps(world,indent=2)+'\n',encoding='utf8');scene['objects'].append(world['id'])
  light=dict(id='f00dcafe00000003',name='receiver-witness-red-point-light',type='point-light',position=[0,3,4.5],rotation=[0,0,0],scale=[.4,.4,.4],color=[1,.08,.04],physics=False,light=dict(brightness=2,radius=30,dynamic=True,flicker=0,beam=0))
  (objects/(light['id']+'.json')).write_text(json.dumps(light,indent=2)+'\n',encoding='utf8');scene['objects'].append(light['id'])
  witness=dict(activePlayerIds=[o['id']for o in players],ordinaryModelIds=['f00dcafe00000001','f00dcafe00000002'],dynamicLightId=light['id'],sharedAsset='res/models/cat.glb',sharedClip=clip,clipChannels=next(len(x['channels'])for x in asset['animations']if x['name']==clip),samePoseNotRuntimeProven=True,p2JoinOnStart=True)
 manifest.write_text(json.dumps(d,indent=2)+'\n',encoding='utf8')
 assert not any('NightAblation'in f.read_text(encoding='utf8',errors='ignore')for f in(dest/'src').rglob('*.cpp'))
 record=dict(name=name,sourceExample=str(source),fixture=str(dest),mode=mode,sourceFiles={f.relative_to(dest).as_posix():sha(f)for f in dest.rglob('*')if f.is_file()},witnessPlan=witness,nativeBuilt=False,runtimeQualified=False)
 (root/(name+'-draft.json')).write_text(json.dumps(record,indent=2)+'\n',encoding='utf8');records.append(record)
(root/'additional-fixture-index.json').write_text(json.dumps(dict(status='SOURCE_ONLY_ADDITIONAL_PUBLIC_RECEIVER_FIXTURES',fixtures=[{k:v for k,v in r.items()if k!='sourceFiles'}for r in records]),indent=2)+'\n',encoding='utf8');print('SOURCE_ONLY_ADDITIONAL_PUBLIC_RECEIVER_FIXTURES',len(records))
