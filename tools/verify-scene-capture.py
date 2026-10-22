"""Host-check A02b's actual recorder with bounded storage and fake clock."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
root=Path(sys.argv[1]);path=root/'tyra/engine/inc/debug/motion_probe.hpp'
header=path.read_text()
clock='inline U ticks() { U t; asm volatile("mfc0 %0,$9":"=r"(t)::"memory"); return t; }'
assert header.count(clock)==1
header=header.replace(clock,'inline U fake=0; inline U ticks(){return ++fake;}')
test=r'''
#include "probe.hpp"
#include <cassert>
struct V{float x,y,z;};struct P{V normal;float distance;};
struct T{void*texture;};
struct B{V*vertices;unsigned count=3,auditOwner=0,auditId=0,auditPass=0,bboxVersion=1;bool stripped=false;T*texture=nullptr;};
struct Q{V*vertices;unsigned size=3,clipPlaneMask=255;};
int main(){using namespace MotionProbe;namespace S=SceneCapture;
V v[3]={{1,2,3},{4,5,6},{7,8,9}};P p[8]{};B b{v};Q q{v};
offset=1336;phase=0;{S::Bag r(&b,0);S::dispatch(&q,true);}assert(S::count==0);
phase=1;enabled=true;S::tag(&b,S::Terrain,9,S::GroundShadow);
{S::Bag r(&b,1);add(Bags);S::planes(p);S::dispatch(&q,false);S::dispatch(&q,true);}
S::save();assert(S::errors==0 && S::count==1 && S::clipCount==1);
assert(S::records[0].owner==S::Terrain && S::records[0].pass==S::GroundShadow);
assert(S::records[0].callsCull==1 && S::records[0].callsClip==1);
assert(S::clips[0].row==0 && S::clips[0].vertices[8]==S::bits(9));
{S::OwnerScope owner(S::Effects,S::Effect);assert(S::owner==S::Effects && S::pass==S::Effect);}
assert(S::owner==0 && S::pass==0);
offset=1400;{S::Bag r(&b,0);}assert(S::count==1);
offset=1576;{S::Bag r(&b,0);S::dispatch(&q,true);}assert(S::errors==1 && !valid);
valid=true;S::clipCount=1024;{S::Bag r(&b,0);S::planes(p);S::dispatch(&q,true);}assert(S::errors==2);
S::count=4096;{S::Bag r(&b,0);}assert(S::count==4096 && S::errors==3);
S::count=0;S::clipCount=0;q.size=65;{S::Bag r(&b,0);S::planes(p);S::dispatch(&q,true);}assert(S::errors==4);
phase=4;offset=1380;assert(S::active());phase=5;assert(!S::active());
assert(fake==0); // recorder itself reads no clocks
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'probe.hpp').write_text(header);(p/'test.cpp').write_text(test)
    subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
result=dict(passed=True,headerSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
    checks=['off gates','owner/pass/view','bag reconciliation','exact float bits','actual vs logical chunks',
            'missing planes rejected','bag/clip overflow rejected','oversize rejected','drive gates','no clocks','ASan/UBSan'])
(root/'scene-capture-host.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
