"""A25: retain EE preparation; change only the final clip consumer selection."""
import argparse, hashlib, json, shutil, subprocess, sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(repo/'tools/tyrax2-clip-capacity.py'),str(a.out),str(a.source),'--group-only'],check=True)
engine=a.out/'tyra/engine'
for name in ('stapip_core.cpp','stapip_qbuffer_renderer.cpp'):
 rel=Path('src/renderer/3d/pipeline/static/core')/name
 shutil.copy2(a.source/'tyra/engine'/rel,engine/rel)
def edit(path,old,new):
 text=path.read_text();assert text.count(old)==1,(path,old,text.count(old))
 path.write_text(text.replace(old,new))
header=engine/'inc/debug/clip_consumer.hpp'
header.write_text('''#pragma once
#include "debug/clip_capacity.hpp"
namespace ClipConsumer {
inline bool observe=false;
inline unsigned hash=2166136261U,calls=0,verts=0;
inline void reset(){hash=2166136261U;calls=verts=0;}
inline void bytes(const void* p,unsigned n){const auto* b=static_cast<const unsigned char*>(p);while(n--)hash=(hash^*b++)*16777619U;}
inline void word(unsigned n){bytes(&n,sizeof(n));}
template<class Buffer> inline void input(Buffer* b){
 if(!observe)return;
 ++calls;verts+=b->size;word(b->size);word(b->clipPlaneMask);word(b->stripped);
 const unsigned streams=(b->bag->texture?1:0)|(b->bag->color->many?2:0)|(b->bag->lighting?4:0);
 word(streams);bytes(b->vertices,b->size*sizeof(*b->vertices));
 if(streams&1)bytes(b->sts,b->size*sizeof(*b->sts));
 if(streams&2)bytes(b->colors,b->size*sizeof(*b->colors));
 if(streams&4)bytes(b->normals,b->size*sizeof(*b->normals));
}
}
''')
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(renderer,'void StaPipQBufferRenderer::clip(StaPipQBuffer* buffer) {','''void StaPipQBufferRenderer::clip(StaPipQBuffer* buffer) {
  ClipConsumer::input(buffer);
  if(ClipTrial::observe){++ClipTrial::current.clipCalls;if(ClipTrial::mode==7)++ClipTrial::current.eligible;}''')
edit(renderer,'    dBufferPrograms[getQBufferIndex(buffer)] = getClipProgramByBag(buffer->bag);','''    TYRA_ASSERT(!hasProgramOverrides() && !buffer->bag->billboard,"A25 unsupported clip consumer");
    dBufferPrograms[getQBufferIndex(buffer)] = ClipTrial::mode==7
        ? getCullProgramByBag(buffer->bag) : getClipProgramByBag(buffer->bag);''')
text=renderer.read_text();renderer.write_text('#include "debug/clip_consumer.hpp"\n'+text)
physics=a.out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'#include "debug/clip_capacity.hpp"','#include "debug/clip_capacity.hpp"\n#include "debug/clip_consumer.hpp"')
edit(physics,'ClipTrial::mode=arm==1?7:arm==3?8:0;','ClipTrial::mode=arm==1?7:0;')
edit(physics,'      core.synchronizeFrame();\n      TYRA_ASSERT(core.gs.selectDiagnosticTarget','''      ClipConsumer::reset();ClipConsumer::observe=frame==7;
      core.synchronizeFrame();
      TYRA_ASSERT(core.gs.selectDiagnosticTarget''')
edit(physics,'    for(unsigned env=0;env<2;++env){','''    ClipConsumer::observe=false;
    printf("CONSUMERINPUT phase=%u arm=%u calls=%u verts=%u hash=%u\\n",MotionProbe::phase,arm,ClipConsumer::calls,ClipConsumer::verts,ClipConsumer::hash);fflush(stdout);
    for(unsigned env=0;env<2;++env){''')
plan=json.loads((a.out/'clip-capacity-manifest.json').read_text())
plan.update(experiment='A25',consumerOnly=True,unrefined=False,scope='7 final cull instead of clip consumer; EE Core and qbuffer fills unchanged; destructive output contrast',raster='0/7/0/0/0; cold input hashes equal, return controls exact')
plan['preparationSha256']=hashlib.sha256((a.source/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_bytes()).hexdigest()
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
# Byte-identical preparation TU, not merely a textual assertion about its route.
assert hashlib.sha256((engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_bytes()).digest()==hashlib.sha256((a.source/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_bytes()).digest()
print('A25 fixture:',a.out)
