"""A26: fixed two-qword header close, preserving original geometry and DMA."""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(repo/'tools/tyrax2-clip-consumer.py'),str(a.out),str(a.source)],check=True)
engine=a.out/'tyra/engine'
def edit(path,old,new):
 text=path.read_text();assert text.count(old)==1,(path,old,text.count(old));path.write_text(text.replace(old,new))
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
shutil.copy2(a.source/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp',renderer)
text=renderer.read_text();renderer.write_text('#include "debug/clip_consumer.hpp"\n'+text)
edit(renderer,'void StaPipQBufferRenderer::clip(StaPipQBuffer* buffer) {',
 'void StaPipQBufferRenderer::clip(StaPipQBuffer* buffer) {\n  ClipConsumer::input(buffer);')
header=engine/'inc/debug/clip_consumer.hpp'
edit(header,'inline bool observe=false;','inline bool observe=false;\ninline unsigned headerChecks=0;')
edit(header,'inline void reset(){hash=2166136261U;calls=verts=0;}',
 'inline void reset(){hash=2166136261U;calls=verts=headerChecks=0;}')
program=engine/'src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp'
text=program.read_text();program.write_text('#include "debug/clip_consumer.hpp"\n#include <cstring>\n'+text)
edit(program,'  packet2_utils_vu_close_unpack(packet);','''  // The standard header is exactly scale/count + primitive GIFtag: two qwords.
  const bool observe=ClipTrial::observe || ClipConsumer::observe;
  packet2_t reference;
  qword_t* start=nullptr;
  if(observe){reference=*packet;start=reinterpret_cast<qword_t*>(packet->tag_opened_at);}
  if(ClipTrial::mode==7){
    packet2_vif_pad128(packet);
    packet2_chain_close_tag(packet);
    packet2_vif_close_unpack_manual(packet,2);
  } else {
    packet2_utils_vu_close_unpack(packet);
  }
  if(ClipTrial::observe){++ClipTrial::current.clipCalls;if(ClipTrial::mode==7)++ClipTrial::current.eligible;}
  if(observe){
    qword_t result[3];memcpy(result,start,sizeof(result));
    if(ClipTrial::mode==7)packet2_utils_vu_close_unpack(&reference);
    else {packet2_vif_pad128(&reference);packet2_chain_close_tag(&reference);packet2_vif_close_unpack_manual(&reference,2);}
    TYRA_ASSERT(memcmp(result,start,sizeof(result))==0 && packet->next==reference.next &&
      packet->tag_opened_at==reference.tag_opened_at && packet->vif_code_opened_at==reference.vif_code_opened_at,
      "A26 fixed header differs from SDK auto close");
    if(ClipConsumer::observe)++ClipConsumer::headerChecks;
  }''')
physics=a.out/'game/src/gen/game_physics.gen.cpp'
edit(physics,'    printf("CONSUMERINPUT phase=',
 '    printf("HEADERORACLE phase=%u arm=%u checks=%u\\n",MotionProbe::phase,arm,ClipConsumer::headerChecks);fflush(stdout);\n    printf("CONSUMERINPUT phase=')
plan=json.loads((a.out/'clip-capacity-manifest.json').read_text())
plan.update(experiment='A26',fixedHeader=True,scope='7 fixed standard-header UNPACK length 2; normal clip programs, original EE geometry and DMA ownership',raster='0/7/0/0/0; all main/env images exact; actual SDK auto/manual bytes and packet state oracle')
plan['rendererReferenceSha256']=hashlib.sha256((a.source/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_bytes()).hexdigest()
(a.out/'clip-capacity-manifest.json').write_text(json.dumps(plan,indent=2)+'\n')
print('A26:',a.out)
