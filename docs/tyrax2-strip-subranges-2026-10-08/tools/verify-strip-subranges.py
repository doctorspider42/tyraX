"""Host sanitizer checks for the exact private bounds/cache/classifier helper."""
import subprocess
import sys
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
code=r'''
#include <cassert>
#include <random>
#include "subranges.hpp"
struct V {float x,y,z,w;};
struct P {V normal;float distance;};
int main(){
 using namespace StripSubranges;
 V v[128]{};P screen[6]={{{1,0,0,0},10},{{-1,0,0,0},10},{{0,1,0,0},10},{{0,-1,0,0},10},{{0,0,1,0},10},{{0,0,-1,0},10}};
 P guard[8];for(int i=0;i<8;++i)guard[i]=screen[i%6];
 bool built=false;auto* e=acquire(v,75,4,1,2,3,built);assert(e&&built);
 acquire(v,75,4,1,2,3,built);assert(!built);
 acquire(v,75,4,2,2,3,built);assert(built);
 acquire(v,75,4,2,3,3,built);assert(built);
 acquire(v,75,4,2,3,4,built);assert(built);
 assert(!acquire(v,128,1,0,0,0,built));assert(!acquire(v,2,4,0,0,0,built));
 auto b=bounds(v,0,6);assert(classify(b,screen,guard)==Cull);
 guard[7].distance=-1;assert(classify(b,screen,guard)==Clip);guard[7]=screen[1];
 guard[6].distance=0;assert(classify(b,screen,guard)==Clip);guard[6]=screen[0];
 v[0].x=NAN;assert(classify(bounds(v,0,6),screen,guard)==Clip);v[0].x=0;
 std::mt19937 rng(981);std::uniform_real_distribution<float>d(-30,30),small(-.3,.3);
 unsigned routes[3]{};
 for(unsigned it=0;it<10000;++it){
   const V centre{d(rng),d(rng),d(rng),1};
   for(auto& a:v)a={centre.x+small(rng),centre.y+small(rng),centre.z+small(rng),1};
   e=acquire(v,75,4,it,0,0,built);assert(e&&built);
   for(unsigned t=0;t<73;t+=4){unsigned n=std::min(4u,73-t);
     auto route=classify(e->boxes[t/4],screen,guard);++routes[route];
     assert(verify(v,t,n,e->boxes[t/4],route,screen,guard));
   }
 }
 assert(routes[Cull]>0&&routes[Outside]>0&&routes[Clip]>0&&failures==0);
 for(auto& a:v)a={100,100,100,1};b=bounds(v,0,6);
 assert(!verify(v,0,4,b,Cull,screen,guard));
 for(auto& a:v)a={0,0,0,1};b=bounds(v,0,6);
 assert(!verify(v,0,4,b,Outside,screen,guard));
 return 0;
}
'''
production='--production' in sys.argv
header=ROOT/'tools/tyrax2-strip-subranges.hpp'
if production:
    header=ROOT/'vendor/tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_strip_subranges.hpp'
    code=code.replace('using namespace StripSubranges;', 'using namespace Tyra::StaPipStripSubranges; Cache cache;')
    code=code.replace('acquire(', 'cache.acquire(')
with tempfile.TemporaryDirectory(prefix='strip-subranges-') as name:
    p=Path(name)
    (p/'subranges.hpp').write_bytes(header.read_bytes())
    (p/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('ASan/UBSan PASS: 190000 range witnesses, all routes, boundary/guard planes, tail overlap, versions/epoch, nonfinite fallback, negative witnesses')
