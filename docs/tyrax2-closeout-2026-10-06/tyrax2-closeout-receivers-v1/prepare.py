from pathlib import Path
import json,shutil,subprocess
root=Path(__file__).parent;repo=Path('F:/Projects/tyra-editor');assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip()=='5b6cd898b05fbbdcc11466bfc8a1911a9c285d4b'
exclude={'bin','obj','logs','screenshots','preview','authoring','.vscode','.git','_backup','__pycache__'}
game=root/'game';assert not game.exists();shutil.copytree(repo/'examples/vehicle-playground',game,ignore=lambda d,n:[x for x in n if x in exclude or x.endswith(('.history','.log','.elf','.sym'))])
manifest=game/'vehicle-playground.tyra';d=json.loads(manifest.read_text(encoding='utf8'));d['formatVersion']=96;d['startScene']=0
d['settings'].update(dynamicLightReceivers='players',buildProfile='debug',liveLink=False,liveLogic=False,liveDebug=False,timeMachine=False,remotePad=False,inputRecorder=False,keyboardMouse=False,keyboardMousePs2Link=False,eeCrashHandler=False,framePipeline=True,showFps=False,showMemory=False,showProfiler=False,showAreas=False,showCollision=False,multiplayer='off',p2JoinOnStart=False)
for scene in d['scenes']:scene.setdefault('overrides',{}).pop('dynamicLightReceivers',None)
for preset in d.get('ambience',[]):
 cycle=preset.setdefault('cycle',{});cycle.update(enabled=True,time=0,runtime=False)
for value in d.get('saveValues',[]):
 if value.get('name')=='district-night':value['default']=1
manifest.write_text(json.dumps(d,indent=2)+'\n',encoding='utf8')
for flag in ('--resave','--refresh-gen'):
 r=subprocess.run([str(repo/'build/tyrax-editor.exe'),flag,str(game)],capture_output=True);(root/(flag[2:]+'.stdout')).write_bytes(r.stdout);(root/(flag[2:]+'.stderr')).write_bytes(r.stderr);assert r.returncode==0,r.stderr.decode(errors='replace')
tyra=root/'tyra';tyra.mkdir();shutil.copytree(repo/'vendor/tyra/engine',tyra/'engine',ignore=lambda d,n:[x for x in n if x in exclude]);shutil.copytree(repo/'vendor/tyra/audsrv',tyra/'audsrv',ignore=lambda d,n:[x for x in n if x in exclude])
sampler=Path('F:/Projects/tyrax2-lab-20261001/night-main11-batch-physical-v2/tyra/engine/inc/debug/night_sampler.hpp');shutil.copyfile(sampler,tyra/'engine/inc/debug/night_sampler.hpp')
(game/'bin').mkdir(exist_ok=True);(game/'bin/closeout.cfg').write_bytes(b'0 0\n')
print('CURRENT_HEAD_SOURCE_BASELINE_PREPARED')
