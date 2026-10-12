"""A40 known integrated paint EE/VU/EE with actual Hybrid3 in every arm."""
import argparse,hashlib,importlib.util,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();source=a.source.resolve();out=a.out.resolve();assert not out.exists()
repo=Path(__file__).resolve().parents[1];sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
b=json.loads((source/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(source/rel)==digest,rel
assert json.loads((source/'triple-physical.json').read_text())['firstOrderGate']
assert json.loads((source/'triple-manifest.json').read_text())['phases']==[3,2,3,3,2,3]
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
def ignore(path,names):return q.ignore(path,names)+[n for n in names if n.startswith(('pc-drive-','pc-held-','triple-present-')) and n.endswith('.csv')]
for part in ('game','tyra/engine'):shutil.copytree(source/part,out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','parent-native-binding.json'):shutil.copy2(source/rel,out/rel)
shutil.copy2(source/'native-binding.json',out/'a38-native-binding.json')
engine=out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-paint-triple.hpp',engine/'inc/debug/paint_triple.hpp')
writer='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
shutil.copy2(source/writer,out/'parent-head-writer.cpp')
def edit(path,old,new):
 t=path.read_text(encoding='utf-8-sig');path.write_text(q.once(t,old,new),encoding='utf-8')
run=engine/'src/engine.cpp'
edit(run,'#include "debug/triple_trial.hpp"','#include "debug/triple_trial.hpp"\n#include "debug/paint_triple.hpp"')
edit(run,'renderer.core.setDiagnosticTriple(MotionProbe::phase%3!=1);',
 'renderer.core.setDiagnosticTriple(true);PaintTriple::enabled=MotionProbe::phase%3==1;')
edit(run,'unsigned(MotionProbe::phase%3!=1),renderer.core.gs.getFrameBufferCount()','1u,renderer.core.gs.getFrameBufferCount()')
edit(run,'  MotionProbe::begin(renderer.core.getStallTotal());',
 '  if(MotionProbe::offset==0){printf("PAINTMODE phase=%u mode=%u\\n",MotionProbe::phase,unsigned(PaintTriple::enabled));fflush(stdout);}\n  MotionProbe::begin(renderer.core.getStallTotal());')
physics=out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'#include "debug/triple_trial.hpp"','#include "debug/triple_trial.hpp"\n#include "debug/paint_triple.hpp"')
edit(physics,'part.envTexBag->paintVu = false && stapip.core.supportsPaintVu(part.envBag.get());',
 'part.envTexBag->paintVu = PaintTriple::enabled && stapip.core.supportsPaintVu(part.envBag.get());')
edit(physics,'core.setDiagnosticTriple(arm==1,true);setupProjShadows();',
 'core.setDiagnosticTriple(true,true);setupProjShadows();PaintTriple::enabled=arm==1;PaintTriple::coldHeads=0;')
edit(physics,'  core.setDiagnosticTriple(MotionProbe::phase%3!=1,true);setupProjShadows();',
 '  PaintTriple::enabled=MotionProbe::phase%3==1;core.setDiagnosticTriple(true,true);setupProjShadows();')
edit(physics,'    printf("TERRAINFOCUSCHECK phase=',
 '    PaintTriple::witness(MotionProbe::phase,arm);\n    printf("TERRAINFOCUSCHECK phase=')
head=out/writer
edit(head,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/paint_triple.hpp"')
edit(head,'    ++WorldFront::headChecks;WorldFront::headWords+=n;',
 '    if(paintVu)++PaintTriple::coldHeads;\n    ++WorldFront::headChecks;WorldFront::headWords+=n;')
(out/'triple-manifest.json').write_text(json.dumps(dict(experiment='A38',parentElf=json.loads((out/'parent-native-binding.json').read_text())['elfSha256'],phases=[3]*6,held=[3]*3),indent=2)+'\n')
(out/'paint-triple-manifest.json').write_text(json.dumps(dict(experiment='A40',parentElf=b['elfSha256'],modes=[0,1,0,0,1,0],held=[0,1,0],source=str(source),scope='Known A13 paint offload under Hybrid3/current native-strip ABI; no A39'),indent=2)+'\n')
print(out)
