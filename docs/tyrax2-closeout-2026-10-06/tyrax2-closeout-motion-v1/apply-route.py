import argparse,hashlib,json,pathlib,re,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--fixture',required=True);p.add_argument('--route',choices=['vehicle','gate'],required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parent;repo=pathlib.Path('F:/Projects/tyra-editor');fixture=pathlib.Path(a.fixture).resolve();lab=root.parent.resolve()
assert fixture.is_relative_to(lab) and fixture != lab
source=fixture/'src/terrain_game.cpp';s=source.read_text(encoding='utf-8');assert 'CloseoutMotion' not in s
assert 'inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);' in s
manifest=next(fixture.glob('*.tyra'));settings=json.loads(manifest.read_text(encoding='utf-8-sig'))['settings']
for flag in ['remotePad','liveDebug','liveLogic','liveLink','timeMachine','inputRecorder','keyboardMouse','showFps','showMemory','showProfiler','showAreas','showCollision']:
    assert settings.get(flag,False) is False,(flag,'must be disabled before ordinary generation')
assert settings.get('disableVsync',False) is False
old=hashlib.sha256(source.read_bytes()).hexdigest()
anchor='  inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);'
assert s.count(anchor)==1
route=1 if a.route=='vehicle' else 2
s=s.replace(anchor,anchor+f'\n  CloseoutMotion::apply(engine->pad, NightSampler::frame, {route}U);',1)
s='#include "motion_route.hpp"\n'+s
shutil.copyfile(root/'motion_route.hpp',fixture/'inc/motion_route.hpp');source.write_bytes(s.encode('utf-8'))
record={'status':'PRIVATE_INPUT_ROUTE_PATCH_ONLY_NO_BUILD_OR_RUNTIME','HEAD':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),'fixture':str(fixture),'source':str(source),'preSha256':old,'postSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'headerSha256':hashlib.sha256((fixture/'inc/motion_route.hpp').read_bytes()).hexdigest(),'route':a.route,'samplerFrameContract':'beginLoop assigns absolute frame before pad update; verify namespace before build','regenerationWarning':'editor --build refresh replaces this private patch; native build private postimage directly after sampler hooks','trajectoryEqualityClaim':False}
proof=fixture.parent/'input-route-proof.json';assert not proof.exists();proof.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print(json.dumps(record))
