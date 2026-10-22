"""Compile the actual candidate; replay frozen PS2 clip inputs and fuzz safety."""
import csv
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
root=Path(sys.argv[1]); repo=Path(__file__).resolve().parents[1]
evidence=repo/'docs/tyrax2-scene-capture-2026-10-09/accepted-v4/physical'
header=root/'tyra/engine/inc/debug/sky_bounds.hpp'
bags={(r['phase'],r['row']):r for r in csv.DictReader((evidence/'scene-bags.csv').open())}
clips=[r for r in csv.DictReader((evidence/'scene-clips.csv').open()) if bags[r['phase'],r['row']]['owner']=='7']
source=r'''
#include "sky_bounds.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
#include <limits>
struct V{float x,y,z;};struct P{V normal;float distance;};
float bits(unsigned u){float f;memcpy(&f,&u,4);return f;}
int main(){
  P cube[8]={{{1,0,0},1},{{-1,0,0},1},{{0,1,0},1},{{0,-1,0},1},{{0,0,1},1},{{0,0,-1},1},{{1,0,0},1},{{-1,0,0},1}};
  V v[64]={{0,0,0},{.5f,0,0},{0,.5f,0}};
  assert(SkyBounds::classify(v,3,cube)==SkyBounds::Cull);
  v[0].x=v[1].x=v[2].x=2;assert(SkyBounds::classify(v,3,cube)==SkyBounds::Outside);
  v[0].x=0;assert(SkyBounds::classify(v,3,cube)==SkyBounds::Keep);
  v[0].x=v[1].x=v[2].x=1;assert(SkyBounds::classify(v,3,cube)==SkyBounds::Keep);
  v[0].x=std::numeric_limits<float>::quiet_NaN();assert(SkyBounds::classify(v,3,cube)==SkyBounds::Keep);
  assert(SkyBounds::classify(v,0,cube)==SkyBounds::Keep);
  V inside[3]={{0,0,0},{0,0,0},{0,0,0}};
  cube[6].distance=-1;assert(SkyBounds::classify(inside,3,cube)==SkyBounds::Keep);
  cube[6].distance=1;
  std::mt19937 rng(93);std::uniform_real_distribution<float> f(-1000,1000);
  for(int k=0;k<50000;++k){
    P ps[8];unsigned n=3*(1+rng()%20);
    for(unsigned i=0;i<n;++i)v[i]={f(rng),f(rng),f(rng)};
    for(auto& p:ps)p={{f(rng),f(rng),f(rng)},f(rng)};
    auto r=SkyBounds::classify(v,n,ps);assert(SkyBounds::verify(v,n,ps,r));
  }
  unsigned n,u,counts[3]={};
  while(std::cin>>n){
    assert(n && n<=64);P p[8];
    for(auto& q:p){std::cin>>u;q.normal.x=bits(u);std::cin>>u;q.normal.y=bits(u);std::cin>>u;q.normal.z=bits(u);std::cin>>u;q.distance=bits(u);}
    for(unsigned i=0;i<n;++i){std::cin>>u;v[i].x=bits(u);std::cin>>u;v[i].y=bits(u);std::cin>>u;v[i].z=bits(u);}
    const auto r=SkyBounds::classify(v,n,p);assert(SkyBounds::verify(v,n,p,r));++counts[r];
  }
  std::cout<<counts[0]<<" "<<counts[1]<<" "<<counts[2]<<"\n";
}
'''
data='\n'.join(' '.join([r['n']]+[r['p'+str(i)] for i in range(32)]+[r['v'+str(i)] for i in range(int(r['n'])*3)]) for r in clips)
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'sky_bounds.hpp').write_bytes(header.read_bytes());(p/'test.cpp').write_text(source)
    subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    run=subprocess.run([str(p/'test')],input=data,capture_output=True,text=True,check=True)
counts=list(map(int,run.stdout.split()));assert counts==[89,74,58],counts
result=dict(passed=True,skyClipInputs=len(clips),keep=counts[0],safe=counts[1],outside=counts[2],randomChecks=50000,
    controls=['inside','outside','straddle','plane boundary','nonfinite fallback','empty','EE-only plane cannot reject'],
    headerSha256=hashlib.sha256(header.read_bytes()).hexdigest(),sanitizers='ASan/UBSan')
(root/'sky-host.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
