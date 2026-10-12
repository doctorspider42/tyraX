"""Test the actual P08 command memo; this is not an emulation of PS2 writers."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
root=Path(sys.argv[1])
header=root/'tyra/engine/inc/debug/clip_command_memo.hpp'
program=(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp').read_text()
wrapper=program[program.index('void StaPipVU1Program::addBufferDataToPacket'):program.index('bool StaPipVU1Program::emitsStateFlag')]
assert wrapper.index('addStandardBufferDataToPacket') < wrapper.index('cache.emit')
assert 'name == StaPipClipTextureColor' in wrapper
assert 'diagnosticRoadId == 24' in wrapper and 'diagnosticRoadId == 34' in wrapper
assert 'packet->next = start + result.qw;' in wrapper
body=(root/'tyra/engine/src/renderer/3d/pipeline/static/core/programs/clip/stapip_clip_tc_vu1_program.cpp').read_text()
body=body[body.index('void StaPipClipTCVU1Program::addProgramQBufferDataToPacket'):]
assert body.count('packet2_utils_vu_add_unpack_data(')==3
assert 'qbuffer->normals' not in body
test=r'''
#include <cassert>
#include <algorithm>
#include "clip_command_memo.hpp"
int main() {
  ClipCommandMemo::Cache cache;
  alignas(16) uint32_t out[16]{};
  ClipCommandMemo::Key key{0x1000,0x2000,0x3000,12,0};
  unsigned writes=0;
  auto writer=[&](){++writes;for(unsigned i=0;i<12;++i)out[i]=i%4==0?0x3000000cu:(i+77);return 3u;};
  auto a=cache.emit(key,out,false,writer);assert(!a.hit&&a.equal&&writes==1);
  std::fill(out,out+16,0u);
  auto b=cache.emit(key,out,false,writer);assert(b.hit&&b.equal&&writes==1&&out[11]==88&&out[12]==0);
  auto c=cache.emit(key,out,true,writer);assert(c.hit&&c.equal&&writes==2);
  auto padded=cache.emit(key,out,true,[&](){writer();out[0]|=0x03FF0000u;return 3u;});
  assert(padded.hit&&padded.equal&&padded.paddingDifferent&&out[0]==0x33FF000Cu);
  cache.emit(key,out,false,writer);assert(out[0]==0x33FF000Cu);
  auto bad=cache.emit(key,out,true,[&](){writer();out[7]^=1;return 3u;});assert(bad.hit&&!bad.equal);
  for(unsigned field=0;field<5;++field){
    auto changed=key;
    switch(field){case 0:changed.vertices+=16;break;case 1:changed.sts+=16;break;
      case 2:changed.colors+=16;break;case 3:changed.count+=3;break;case 4:changed.single=1;break;}
    auto r=cache.emit(changed,out,false,writer);assert(!r.hit&&r.equal);
  }
  for(unsigned i=0;i<4096;++i){
    key.vertices=i*16;auto r=cache.emit(key,out,true,writer);assert(r.equal);
  }
  key.vertices=0x123456;
  auto invalid=cache.emit(key,out,false,[](){return 4u;});assert(!invalid.equal);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(test)
    subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined','-I',str(header.parent),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
result=dict(passed=True,headerSha256=hashlib.sha256(header.read_bytes()).hexdigest(),
    checks=['miss/hit','no writer on hit','fresh-byte oracle','corrupted body rejected',
        'all five key fields','4096 replacements','invalid body length','output canary',
        'DMA PAD preservation','original dynamic header before memo','TC body source checks'],
    scope='actual bounded memo helper, not PS2 packet writer or raster')
(root/'command-memo-host.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
