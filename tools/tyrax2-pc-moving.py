"""A37 moving raw-PC distribution: unchanged V7 baseline, A35 OFF, no -pg."""
import argparse,hashlib,importlib.util,json,re,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();source=a.source.resolve();out=a.out.resolve();assert not out.exists()
repo=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
b=json.loads((source/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(source/rel)==digest,rel
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
for part in ('game','tyra/engine'):shutil.copytree(source/part,out/part,ignore=q.ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(source/rel,out/rel)
engine=out/'tyra/engine'
for original,name in [('tyrax2-pc-sample-store.hpp','pc_sample_store.hpp'),('tyrax2-pc-moving.hpp','pc_moving.hpp')]:
    shutil.copy2(repo/'tools'/original,engine/'inc/debug'/name)
def edit(path,old,new):
    t=path.read_text(encoding='utf-8-sig');path.write_text(q.once(t,old,new),encoding='utf-8')
motion=engine/'inc/debug/motion_probe.hpp'
edit(motion,'constexpr U modes[]={0,7,0,0,7,0};','constexpr U modes[]={0,0,0,0,0,0};')
run=engine/'src/engine.cpp'
edit(run,'#include "debug/motion_probe.hpp"','#include "debug/motion_probe.hpp"\n#include "debug/pc_moving.hpp"')
edit(run,'  MotionProbe::begin(renderer.core.getStallTotal());','''  MotionProbe::begin(renderer.core.getStallTotal());
  if(MotionProbe::phase>=MotionProbe::phaseCount){SleepThread();return;}
  if(MotionProbe::offset==MotionProbe::gasStart){
    const unsigned us=MotionProbe::phase%3==0?997:MotionProbe::phase%3==1?1733:0;
    PcMoving::start(MotionProbe::phase,0,us);
  }
  PcMoving::mark(MotionProbe::offset,1);
  if(MotionProbe::finishPending){PcMoving::stop();PcMoving::exportRows();}''')
edit(run,'  MotionProbe::end(renderer.core.getStallTotal());','  MotionProbe::end(renderer.core.getStallTotal());\n  PcMoving::mark(MotionProbe::offset,0);')
game=out/'game/src/terrain_game.cpp'
edit(game,'  MotionProbe::renderBegin();','  PcMoving::mark(MotionProbe::offset,2);\n  MotionProbe::renderBegin();')
game.write_text('#include "debug/pc_moving.hpp"\n'+game.read_text(),encoding='utf-8')
physics=out/'game/src/gen/game_physics.gen.cpp'
t=physics.read_text(encoding='utf-8-sig')
begin=t.index('void TerrainGame::renderJoinCheckpoint() {');end=t.index('\nvoid TerrainGame::renderScene() {',begin)
c=t[begin:end]
admit=c.index('    if(arm==1){');admit_end=c.index('    for(unsigned frame=0;frame<32;++frame){',admit)
c=c[:admit]+c[admit_end:]
c=q.once(c,'for(unsigned arm=0;arm<5;++arm)','for(unsigned arm=0;arm<3;++arm)')
c=q.once(c,'ClipTrial::mode=arm==1?7:0;','ClipTrial::mode=0;')
c=q.once(c,'WorldRuntime::active=arm==1;WorldFront::active=arm==1;','WorldRuntime::active=true;WorldFront::active=false;')
c=q.once(c,'    for(unsigned frame=0;frame<32;++frame){','''    if(arm==1)PcMoving::start(MotionProbe::phase,1,MotionProbe::phase%2?1733:997);
    const unsigned stopped=PcMoving::callbacks;
    for(unsigned frame=0;frame<32;++frame){
      PcMoving::mark(frame,8);''')
c=q.once(c,'    ClipConsumer::observe=false;','''    if(arm==1){PcMoving::stop();PcMoving::exportRows();}
    else TYRA_ASSERT(PcMoving::callbacks==stopped,"A37 callbacks in off return");
    ClipConsumer::observe=false;''')
c=q.once(c,'(arm==1?WorldRuntime::draws==WorldRuntime::sources:WorldRuntime::draws==0)','WorldRuntime::draws==WorldRuntime::sources')
c=q.once(c,'MotionProbe::phase*5+arm','MotionProbe::phase*3+arm')
c=q.once(c,'arms=5','arms=3')
c=q.once(c,'CoarseStrip::active=active==7;','CoarseStrip::active=true;WorldRuntime::active=true;WorldFront::active=false;')
t=t[:begin]+c+t[end:]
physics.write_text('#include "debug/pc_moving.hpp"\n'+t,encoding='utf-8')
sdk=Path('C:/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2sdk')
pins={str(f.relative_to(sdk)).replace('\\','/'):sha(f) for f in
 [sdk/'ee/lib/libkernel.a',sdk/'ee/include/kernel.h',sdk/'ee/include/timer.h',sdk/'ee/include/timer_alarm.h']}
(out/'pc-moving-manifest.json').write_text(json.dumps(dict(parentElf=b['elfSha256'],source=str(source),
 scope='Instruction residency, not exclusive cycles or FPS proof',periods=[997,1733,0,997,1733,0],
 heldArms=['off','on','off return'],sdkPins=pins,calibrationElf='a87be09eeee5f91ee90f75876a62be9daca35d3ca368ce07ba739fefc0caf2c3'),indent=2)+'\n')
print(out)
