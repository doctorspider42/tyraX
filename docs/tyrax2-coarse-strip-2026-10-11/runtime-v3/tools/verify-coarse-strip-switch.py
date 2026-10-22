"""Compile actual A30 main-loop switching snippet; reject per-frame drains."""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path)
a=p.parse_args()
game=(a.fixture/'game/src/terrain_game.cpp').read_text()
renderer=(a.fixture/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_text()
assert 'if(ClipRing::active==enabled)return;' in renderer
assert game.count('setDiagnosticClipRing(false)')==1
match=re.search(r'^  if\(CoarseStrip::active!=\(ClipTrial::mode==7\)\)stapip.core.setDiagnosticClipRing\(false\);\n  stapip.core.setDiagnosticClipRing\(true\);CoarseStrip::active=ClipTrial::mode==7;',game,re.M)
assert match,'Missing guarded main-loop switch'
snippet=match.group()
prefix='''namespace CoarseStrip { bool active=false; }
namespace ClipTrial { unsigned mode=0; }
struct Renderer {
  bool enabled=false;unsigned changes=0,falseCalls=0;
  void setDiagnosticClipRing(bool e){
    if(!e)++falseCalls;
    if(enabled==e)return;
    enabled=e;++changes;
  }
};
struct Pipeline { Renderer core; } stapip;
void frame(){
'''
suffix='''
}
int main(){
  for(unsigned mode: {0U,7U,7U,0U}){
    ClipTrial::mode=mode;
    for(unsigned i=0;i<64;++i)frame();
  }
  return stapip.core.enabled && !CoarseStrip::active &&
    stapip.core.falseCalls==2 && stapip.core.changes==5?0:77;
}
'''
variants=(snippet,
          snippet.replace('if(CoarseStrip::active!=(ClipTrial::mode==7))',''),
          snippet.split('\n',1)[1])
with tempfile.TemporaryDirectory(prefix='tyrax2-coarse-switch-') as tmp:
    root=Path(tmp)
    for index,body in enumerate(variants):
        source=root/f'case-{index}.cpp';binary=root/f'case-{index}'
        source.write_text('#include <initializer_list>\n'+prefix+body+suffix)
        subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)])
        assert result.returncode==(0 if index==0 else 77),(index,result.returncode)
print('PASS actual main-loop switch frames=256 positive=1 controls=2')
