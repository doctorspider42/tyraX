"""Host-check the injected GIF register substitution, not GS timing."""
import subprocess
import sys
import tempfile
from pathlib import Path

root=Path(sys.argv[1])/'tyra/engine'
source=(root/'src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp').read_text()
start=source.index('    u32 outputRegs=reglist;')
end=source.index('    packet2_utils_gs_add_prim_giftag',start)
body=source[start:end]
q=(root/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_text()
assert q.count('(static_cast<u32>(bag->diagnosticNoKick) << 12)')==2
prefix='''#include <cassert>
using u32=unsigned;
#define GIF_REG_XYZF2 4u
#define GIF_REG_XYZF3 12u
#define TYRA_ASSERT(c,m) assert(c)
struct Bag { bool diagnosticNoKick; };
struct Buffer { Bag* bag; };
unsigned rewrite(unsigned reglist,unsigned reglistCount,bool on) {
Bag b{on}; Buffer storage{&b}; auto* buffer=&storage;
'''
suffix='''return outputRegs;
}
int main(int argc,char**) {
if(argc>1){rewrite(0x111,3,true);return 0;}
for(unsigned count=1;count<=8;++count)for(unsigned lane=0;lane<count;++lane){
unsigned input=0x11111111u;
input=(input&~(15u<<(4*lane)))|(4u<<(4*lane));
assert(rewrite(input,count,false)==input);
assert(rewrite(input,count,true)==(input|(8u<<(4*lane))));
}
assert(rewrite(0x412,3,true)==0xc12);
assert(rewrite(0x41,2,true)==0xc1);
}
'''
with tempfile.TemporaryDirectory(prefix='road-nokick-') as name:
    p=Path(name)
    (p/'test.cpp').write_text(prefix+body+suffix)
    subprocess.run(['g++','-std=c++17','-O1','-fsanitize=undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
    bad=subprocess.run([str(p/'test'),'invalid'],capture_output=True)
    assert bad.returncode!=0
print('PASS: actual register substitution, both cache keys, invalid-register rejection')
