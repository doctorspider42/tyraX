"""Capture actual native prefixes using the qualified A36 parser, at A44 source stages."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
report=json.loads((a.source/'world-source-emulator.json').read_text())
assert report['valid'] and report['exactBaselineImages']==108
assert report['elfSha256']==binding['elfSha256']
assert report['logSha256']==sha(a.source/'emulator/emulator.log')
for rel,digest in report['files'].items():assert sha(a.source/rel)==digest,rel
switch=json.loads((a.source/'world-source-switch-manifest.json').read_text())
assert switch['mode']=='ON' and switch['fixedSceneStep']
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (n.startswith(('tape-','command-','cell-','source-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','world-cell-terrain-manifest.json',
            'world-cell-view-manifest.json','world-source-manifest.json',
            'world-source-switch-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
def once(text,old,new):
    assert text.count(old)==1,old
    return text.replace(old,new)
engine=a.out/'tyra/engine'
t=(repo/'tools/tyrax2-command-tape.hpp').read_text()
t=once(t,'constexpr U capacity=4*1024*1024;',
         'constexpr U capacity=2*1024*1024;\ninline U stageNumber=0;')
start=t.index('inline void arm(){');end=t.index('inline void capture(',start)
t=t[:start]+'''inline void prepare(){
  if(!memory)memory=memalign(64,capacity);
  TYRA_ASSERT(memory,"A44 command reserve failed");
}
inline void arm(U stage){
  TYRA_ASSERT(!armed,"A44 command capture still armed");prepare();
  stageNumber=stage;valid=true;
  flushes=submissions=qwords=bytes=mpg=calls=allocated=0;firstCall=~0u;
  armed=true;
}
'''+t[end:]
t=once(t,'  memory=memalign(64,allocated);','  prepare();')
t=once(t,'  FILE* f=fopen(path,"wb");',
         '''  char output[96];snprintf(output,sizeof(output),"host:command-%u-%s",stageNumber,path);
  FILE* f=fopen(output,"wb");''')
for name in ('closure','program-before','program-after','data-before','data-after',
             'state-before','state-after'):
    t=once(t,f'"host:command-{name}.bin"',f'"{name}.bin"')
t=once(t,'COMMANDCAP valid=%u','COMMANDCAP stage=%u valid=%u')
t=once(t,'    U(valid),flushes,submissions,qwords,bytes,mpg,calls,firstCall,',
       '    stageNumber,U(valid),flushes,submissions,qwords,bytes,mpg,calls,firstCall,')
t=once(t,'COMMANDMEM allocated=%u used=%u limit=%u',
       'COMMANDMEM stage=%u allocated=%u used=%u limit=%u base=%u')
t=once(t,'allocated,bytes,capacity);fflush(stdout);',
       'stageNumber,allocated,bytes,capacity,U(reinterpret_cast<uintptr_t>(memory)));fflush(stdout);')
(engine/'inc/debug/command_tape.hpp').write_text(t)
shutil.copy2(repo/'tools/tyrax2-command-size.hpp',engine/'inc/debug/command_size.hpp')
queue=engine/'src/renderer/core/paths/path1/vif1_queue.cpp'
t=queue.read_text();t=once(t,'namespace Tyra {','#include "debug/command_tape.hpp"\nnamespace Tyra {')
t=once(t,'  // Logical source sequence numbers were reserved while recording.',
       '  CommandTape::capture(nativeWriter->data(),qwords);\n  // Logical source sequence numbers were reserved while recording.')
t=once(t,'void Vif1Queue::enqueue(const void* chain, u32 sequence) {',
       'void Vif1Queue::enqueue(const void* chain, u32 sequence) {\n  if(CommandTape::armed)++CommandTape::submissions;')
queue.write_text(t)
adapter=engine/'inc/renderer/core/paths/path1/frame_submission.hpp'
t=adapter.read_text();t=once(t,'#include "vif1_queue.hpp"',
       '#include "vif1_queue.hpp"\n#include "debug/command_tape.hpp"')
t=once(t,'    // Order an unsupported direct transfer after all recorded VU/GIF work.',
       '    TYRA_ASSERT(!CommandTape::armed,"A44 uncaptured direct VIF transfer");\n    // Order an unsupported direct transfer after all recorded VU/GIF work.')
adapter.write_text(t)
physics=a.out/'game/src/gen/game_physics.gen.cpp'
t=physics.read_text();t=once(t,'#include "world_source_capture.hpp"',
       '#include "world_source_capture.hpp"\n#include "debug/command_tape.hpp"')
t=once(t,'      if(angle==3 && frame==31)WorldSourceCapture::begin(MotionProbe::phase*3+arm);',
       '''      if(angle==3 && frame==31){
        WorldSourceCapture::begin(MotionProbe::phase*3+arm);CommandTape::prepare();
        if(WorldSourceCapture::enabled)CommandTape::arm(MotionProbe::phase*3+arm);
      }''')
t=once(t,'      if(angle==3 && frame==31)WorldSourceCapture::finish();',
       '''      if(angle==3 && frame==31){
        if(WorldSourceCapture::enabled)CommandTape::finish();WorldSourceCapture::finish();
      }''')
physics.write_text(t)
(a.out/'world-command-source-manifest.json').write_text(json.dumps({
    'baseElfSha256':binding['elfSha256'],'generatorSha256':sha(Path(__file__)),
    'commandTemplateSha256':sha(repo/'tools/tyrax2-command-tape.hpp'),
    'sourceTemplateSha256':sha(a.source/'game/inc/world_source_capture.hpp'),
    'capacityBytes':2*1024*1024,'stages':list(range(9)),'status':'UNQUALIFIED',
    'limits':['Actual native prefix; requires ONE captured prefix/submission per stage.',
              'Private cold diagnostic, not a command replay or performance price.']},indent=2)+'\n')
print(a.out,'UNQUALIFIED actual native-prefix capture; existing A36 parser')
