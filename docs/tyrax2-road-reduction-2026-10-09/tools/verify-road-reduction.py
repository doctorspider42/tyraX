"""A06 generated target source matcher and plan bounds under ASan/UBSan."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
root=Path(sys.argv[1]).resolve();header=root/'game/inc/road_reduction.hpp'
source=r'''
#include "road_reduction.hpp"
#include <vector>
#include <cassert>
#include <iostream>
struct V{float x,y,z,w;};struct C{float r,g,b,a;};
int main(){
 unsigned checks=0;
 for(const auto& p:RoadReduction::plans){
  assert(p.count<=75 && p.size<p.count && p.size%3==0);
  assert(RoadReduction::find(p.id)==&p);
  for(unsigned i=0;i<p.size;++i)assert(p.pick[i]<p.count);
  for(const unsigned* bits:{p.a,p.b}){
   std::vector<V> v(p.count),s(p.count);std::vector<C> c(p.count);
   for(unsigned i=0;i<p.count;++i){
    std::memcpy(&v[i],bits+i*12,16);std::memcpy(&s[i],bits+i*12+4,16);std::memcpy(&c[i],bits+i*12+8,16);
   }
   assert(RoadReduction::matches(p,p.count,v.data(),s.data(),c.data()));++checks;
   assert(!RoadReduction::matches(p,p.count-1,v.data(),s.data(),c.data()));
   assert(!RoadReduction::matches(p,p.count,(V*)nullptr,s.data(),c.data()));
   for(unsigned i=0;i<p.count;++i){
    float saved=v[i].x;v[i].x+=1;
    assert(!RoadReduction::matches(p,p.count,v.data(),s.data(),c.data()));v[i].x=saved;
    saved=s[i].y;s[i].y+=1;
    assert(!RoadReduction::matches(p,p.count,v.data(),s.data(),c.data()));s[i].y=saved;
    saved=c[i].a;c[i].a+=1;
    assert(!RoadReduction::matches(p,p.count,v.data(),s.data(),c.data()));c[i].a=saved;
   }
  }
 }
 assert(!RoadReduction::find(999));std::cout<<checks<<" target input sets verified\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(source)
    subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined','-I'+str(header.parent),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    run=subprocess.run([str(p/'test')],capture_output=True,text=True,check=True)
assert run.stdout.strip()=='8 target input sets verified',run.stdout
result=dict(passed=True,targetInputSets=8,headerSha256=hashlib.sha256(header.read_bytes()).hexdigest(),
            controls=['plan indices','both platform payloads','null','count','every vertex position','every UV','every alpha','unknown ID'],sanitizers='ASan/UBSan')
(root/'road-reduction-host.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
