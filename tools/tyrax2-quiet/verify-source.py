"""Host-only generated fixture/anchor checks; never builds games or devices."""
import argparse,hashlib,importlib.util,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--engine',type=Path,required=True);p.add_argument('--showcase',type=Path,required=True);p.add_argument('--vehicle',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists();module=Path(__file__).parent.parent/'tyrax2-quiet-fixture.py';s=importlib.util.spec_from_file_location('quietfixture',module);q=importlib.util.module_from_spec(s);s.loader.exec_module(q);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();runs=[]
for label,fixture in [('showcase',a.showcase),('vehicle',a.vehicle)]:
 record=json.loads((fixture/'fixture.json').read_text());assert record['status']=='ordinary_quiet_fixture_prepared' and record['ordinary_clocks'] and record['authored_save_values_unchanged'] and not record['build_or_device_actions']
 for path,digest in json.loads((fixture/'target-source-manifest.json').read_text())['files'].items():assert sha(fixture/path)==digest,path
 original=Path(record['project']);old=json.loads(next(original.glob('*.tyra')).read_text(encoding='utf-8-sig'));new=json.loads(next((fixture/'game').glob('*.tyra')).read_text(encoding='utf8'));assert old.get('saveValues')==new.get('saveValues')
 for key in ('videoSystem','displayMode','colorDepth','tripleBuffering','framePipeline'):assert old['settings'].get(key)==new['settings'].get(key)
 engine=(a.engine/'engine/src/engine.cpp').read_text(encoding='utf8');renderer=(a.engine/'engine/src/renderer/core/renderer_core.cpp').read_text(encoding='utf8')
 assert (fixture/'tyra/engine/src/engine.cpp').read_text(encoding='utf8')==q.engine_hooks(engine)
 assert (fixture/'tyra/engine/src/renderer/core/renderer_core.cpp').read_text(encoding='utf8')==q.present_hooks(renderer)
 text=(fixture/'game/src/terrain_game.cpp').read_text(encoding='utf8');assert text.count('updateFrameClock();')==1 and 'saveValues[0]=' not in text and text.count('QuietRuntime::config(')==1 and text.count('QuietRuntime::init(')==1
 assert text.count('QUIETCONTEXT')==1 and text.count('QUIETWORKLOAD')==1
 runs.append({'fixture':label,'actual_editor_generation_pass':True,'quality_save_clock_preservation':True,'engine_hook_output_parity':True,'source_manifest_sha256':sha(fixture/'target-source-manifest.json')})
def rejection(label,operation):
 try:operation()
 except ValueError:runs.append({'negative':label,'rejected':True});return
 raise AssertionError('did not reject '+label)
renderer=(a.engine/'engine/src/renderer/core/renderer_core.cpp').read_text(encoding='utf8');engine=(a.engine/'engine/src/engine.cpp').read_text(encoding='utf8')
rejection('missing flip',lambda:q.present_hooks(renderer.replace('gs.flipBuffers(isFrameLimitOn);','missingFlip();',1)))
rejection('duplicate clock',lambda:q.present_hooks(renderer.replace('__asm__ volatile("mfc0 %0, $9" : "=r"(t1));','__asm__ volatile("mfc0 %0, $9" : "=r"(t1));\n__asm__ volatile("mfc0 %0, $9" : "=r"(t1));',1)))
rejection('missing engine end',lambda:q.engine_hooks(engine.replace('HardwareTrace::endFrame();','missingEnd();')))
rejection('ambiguous realLoop',lambda:q.engine_hooks(engine+'\nvoid Engine::realLoop() {}\n'))
rejection('duplicate game init',lambda:q.game_hooks('void TerrainGame::init() {}\nvoid TerrainGame::init() {}'))
rejection('missing game clock',lambda:q.game_hooks('#include "game_runtime.gen.hpp"\nvoid TerrainGame::init() {\n  buildScene();\n}\n'))
rejection('repeat application',lambda:q.present_hooks(q.present_hooks(renderer)))
rejection('reuse output directory',lambda:q.prepare(Path(json.loads((a.showcase/'fixture.json').read_text())['project']),a.engine,Path(json.loads((a.showcase/'fixture.json').read_text())['editor']),a.showcase,0))
rejection('missing editor',lambda:q.prepare(Path(json.loads((a.showcase/'fixture.json').read_text())['project']),a.engine,a.out.parent/'missing-editor-file',a.out.parent/'unused-no-editor-output',0))
unsupported=a.out.with_name(a.out.stem+'-unsupported-input');unsupported.mkdir(exist_ok=False);(unsupported/'unsupported.tyra').write_text('{"template":"thirdPerson"}',encoding='utf8')
rejection('unsupported generated model',lambda:q.prepare(unsupported,a.engine,Path(json.loads((a.showcase/'fixture.json').read_text())['editor']),a.out.parent/'unused-unsupported-output',0))
record={'status':'portable_quiet_source_controls_pass','runs':runs,'helper_sha256':sha(module),'no_build_or_device_actions':True,'render_state_host_control':'generated source/config preservation; no rendered pixels or target timing tested'};a.out.write_text(json.dumps(record,indent=2)+'\n',encoding='utf8');print(record['status']);print(sha(a.out))
