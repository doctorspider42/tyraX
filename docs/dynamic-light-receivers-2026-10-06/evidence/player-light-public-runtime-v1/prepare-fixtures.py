"""Source-only private public-feature fixtures. No editor/build/device calls."""
from pathlib import Path
import copy,hashlib,json,shutil,argparse
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,default=Path('F:/Projects/tyra-editor'));p.add_argument('--extras',action='store_true');a=p.parse_args();root=Path(__file__).parent;assert not(root/'fixtures').exists();(root/'fixtures').mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();records=[]
exclude={'bin','obj','logs','screenshots','preview','authoring','.vscode','.git','_backup','__pycache__'}
def clone(example,name,mode,vehicle=True):
 source=a.repo/'examples'/example;dest=root/'fixtures'/name;shutil.copytree(source,dest,ignore=lambda d,n:[x for x in n if x in exclude or x.endswith(('.history','.log','.elf','.sym'))]);manifest=dest/(example+'.tyra');d=json.loads(manifest.read_text(encoding='utf8'));d['formatVersion']=96;d['startScene']=0;d['settings'].update(dynamicLightReceivers=mode,buildProfile='debug',remotePad=True,liveDebug=True,framePipeline=True);d['settings']['showFps']=True
 for scene in d['scenes']:scene.setdefault('overrides',{}).pop('dynamicLightReceivers',None);scene.setdefault('settings',{})['dynamicLightReceivers']='all'
 if vehicle:
  # Authored frozen midnight; no private cfg or per-frame night forcing.
  for preset in d.get('ambience',[]):
   cycle=preset.get('cycle',{});cycle.update(enabled=True,time=0,runtime=False);preset['cycle']=cycle
  for value in d.get('saveValues',[]):
   if value.get('name')=='district-night':value['default']=1
 manifest.write_text(json.dumps(d,indent=2)+'\n',encoding='utf8')
 if vehicle:
  scene=d['scenes'][0];objs={oid:json.loads((dest/'objects'/(oid+'.json')).read_text(encoding='utf8'))for oid in scene['objects']}
  driven=next((o for o in objs.values()if o.get('type')=='vehicle'and any(n.get('type')=='EnterVehicle'for n in o.get('flowGraph',{}).get('nodes',[]))),None)
  if driven is None:
   driven=next(o for o in objs.values()if o.get('type')=='vehicle');driven['flowGraph']=dict(nextId=4,nodes=[dict(id=1,type='OnStart',pos=[64,130],str='',num=[0,0,0,0]),dict(id=2,type='EnterVehicle',pos=[400,142],str='',num=[0,0,0,0])],links=[dict(id=3,**{'from':1,'to':2})]);(dest/'objects'/(driven['id']+'.json')).write_text(json.dumps(driven,indent=2)+'\n',encoding='utf8')
  driverId=driven['id']
 else:driverId=None
 files={f.relative_to(dest).as_posix():sha(f)for f in dest.rglob('*')if f.is_file()};assert not any('NightAblation' in f.read_text(encoding='utf8',errors='ignore')for f in(dest/'src').rglob('*.cpp')),'private observer source imported'
 record=dict(name=name,sourceExample=str(source),fixture=str(dest),mode=mode,driverObjectId=driverId,startScene=0,authoredNightFrozen=vehicle,sourceFiles=files,nativeBuilt=False,runtimeQualified=False)
 (root/(name+'-draft.json')).write_text(json.dumps(record,indent=2)+'\n',encoding='utf8');records.append(record)
for mode in('all','players'):clone('vehicle-playground','vehicle-'+mode,mode)
if a.extras:
 for example in('portals','showcase'):
  for mode in('all','players'):clone(example,example+'-'+mode,mode,False)
(root/'fixture-index.json').write_text(json.dumps(dict(status='SOURCE_ONLY_PUBLIC_RECEIVER_FIXTURES',fixtures=[{k:v for k,v in r.items()if k!='sourceFiles'}for r in records],noPrivateObserver=True),indent=2)+'\n',encoding='utf8');print('SOURCE_ONLY_PUBLIC_RECEIVER_FIXTURES',len(records))
