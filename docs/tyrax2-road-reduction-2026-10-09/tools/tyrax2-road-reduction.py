"""A06 private draw-only road streams, bound to both A05 target inputs."""
import hashlib
import importlib.util
import json
import struct
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out,source=map(lambda p:Path(p).resolve(),sys.argv[1:3])
audit=ROOT/'docs/tyrax2-road-geometry-2026-10-09/accepted-v1'
spec=importlib.util.spec_from_file_location('geometry',ROOT/'tools/tyrax2-road-geometry-analyze.py')
geo=importlib.util.module_from_spec(spec);spec.loader.exec_module(geo)
def load(path):
    raw=path.read_bytes();magic,n=struct.unpack_from('<2I',raw);assert magic==0x31414452 and n==66
    at=8;out={}
    for _ in range(n):
        h=struct.unpack_from('<8I',raw,at);at+=32
        rows=[struct.unpack_from('<12f',raw,at+i*48) for i in range(h[2])];at+=h[2]*48
        out[h[0]]=(h,rows)
    assert at==len(raw)
    return out
paths=[audit/'game/bin/road-geometry.bin',audit/'physical/bin/road-geometry.bin']
data=[load(p) for p in paths]
reports=[json.loads(p.with_suffix('.json').read_text()) for p in paths]
assert all(hashlib.sha256(p.read_bytes()).hexdigest()==r['sourceSha256'] for p,r in zip(paths,reports))
plans=[];source_code=['#pragma once','#include <cstring>','namespace RoadReduction {',
    'struct Plan { unsigned id,count,size; const unsigned *pick,*a,*b; };']
for index in range(23,27):
    cs=[next(c for c in r['chunks'] if c['index']==index) for r in reports]
    assert all(c['eligible'] for c in cs)
    stations=sorted({i//2 for c in cs for i in c['selected']})
    picks=[i for s in stations for i in (2*s,2*s+1)]
    while len(picks)%3:picks.append(picks[-1])
    proofs=[]
    for dataset in data:
        h,rows=dataset[index]
        assert h[3]==75 and not h[4] and not h[5] and len(picks)<len(rows)
        proof=geo.surface_error(rows,[rows[i] for i in picks]);assert proof['maxSurfaceReconstructionError']<=1e-5
        proofs.append(proof)
    n=data[0][index][0][2];assert n==data[1][index][0][2]
    source_code.append('inline constexpr unsigned pick%d[]={%s};'%(index,','.join(map(str,picks))))
    for platform,dataset in zip(('a','b'),data):
        bits=[struct.unpack('<I',struct.pack('<f',v))[0] for row in dataset[index][1] for v in row]
        source_code.append('inline constexpr unsigned %s%d[]={%s};'%(platform,index,','.join('0x%08xu'%b for b in bits)))
    plans.append(dict(id=index,count=n,size=len(picks),picks=picks,proofs=proofs))
source_code.append('inline constexpr Plan plans[]={'+','.join('{%d,%d,%d,pick%d,a%d,b%d}'%(p['id'],p['count'],p['size'],p['id'],p['id'],p['id']) for p in plans)+'};')
source_code.append(r'''
inline const Plan* find(unsigned id){for(const auto& p:plans)if(p.id==id)return &p;return nullptr;}
template<class V,class C> bool matches(const Plan& p,unsigned count,const V* v,const V* s,const C* c){
  if(count!=p.count || !v || !s || !c)return false;
  bool a=true,b=true;
  for(unsigned i=0;i<count;++i){
    const float row[12]={v[i].x,v[i].y,v[i].z,v[i].w,s[i].x,s[i].y,s[i].z,s[i].w,c[i].r,c[i].g,c[i].b,c[i].a};
    unsigned bits[12];static_assert(sizeof(bits)==sizeof(row),"float payload size");std::memcpy(bits,row,sizeof(bits));
    for(unsigned j=0;j<12;++j){a=a && bits[j]==p.a[i*12+j];b=b && bits[j]==p.b[i*12+j];}
  }
  return a||b;
}
}
''')
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-sky-bounds.py'),str(out),str(source)],check=True)
engine,game=out/'tyra/engine',out/'game'
def edit(p,old,new):
    s=p.read_text(encoding='utf-8-sig');assert s.count(old)==1,(p,old,s.count(old))
    p.write_bytes(s.replace(old,new).encode())
(game/'inc/road_reduction.hpp').write_text('\n'.join(source_code)+'\n')
h=engine/'inc/debug/motion_probe.hpp'
extra='RoadCalls, RoadSourceVerts, RoadDrawVerts, RoadReducedCalls, RoadCullCalls, RoadClipCalls'
edit(h,'SkyClipCalls, Count','SkyClipCalls, '+extra+', Count')
edit(h,'skyClipCalls,captureFrozen','skyClipCalls,roadCalls,roadSourceVerts,roadDrawVerts,roadReducedCalls,roadCullCalls,roadClipCalls,captureFrozen')
edit(h,'inline bool skyBag=false;','''inline bool roadSelected=false;
struct RoadScope { bool old; explicit RoadScope(bool b):old(roadSelected){roadSelected=b;} ~RoadScope(){roadSelected=old;} };
inline bool skyBag=false;''')
pack=engine/'src/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.cpp'
edit(pack,'if(MotionProbe::variant()==6 || cold){','if(cold){')
edit(pack,'      if(MotionProbe::variant()==6){','      if(false){')
edit(engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp',
     '  bool diagnosticSky = false;','  unsigned diagnosticRoadSource = 0;\n  bool diagnosticSky = false;')
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(core,'  MotionProbe::SkyScope skyScope(bag->diagnosticSky);','''  MotionProbe::RoadScope roadScope(bag->diagnosticRoadSource!=0);
  if(bag->diagnosticRoadSource){
    MotionProbe::add(MotionProbe::RoadCalls);
    MotionProbe::add(MotionProbe::RoadSourceVerts,bag->diagnosticRoadSource);
    MotionProbe::add(MotionProbe::RoadDrawVerts,bag->count);
    if(bag->count<bag->diagnosticRoadSource)MotionProbe::add(MotionProbe::RoadReducedCalls);
  }
  MotionProbe::SkyScope skyScope(bag->diagnosticSky);''')
q=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
for a,b in [('SkyCullCalls','RoadCullCalls'),('SkyClipCalls','RoadClipCalls')]:
    old='  if(MotionProbe::skyBag)MotionProbe::add(MotionProbe::'+a+');'
    edit(q,old,old+'\n  if(MotionProbe::roadSelected)MotionProbe::add(MotionProbe::'+b+');')
edit(game/'inc/terrain_game.hpp','  struct ProcChunk {','''  struct ProcChunk {
    BagArray<Tyra::Vec4> roadDrawVertices, roadDrawSts;
    BagArray<Tyra::Color> roadDrawColors;''')
collision=game/'src/gen/game_collision.gen.cpp'
collision.write_bytes(b'#include "road_reduction.hpp"\n'+collision.read_bytes())
edit(collision,'      c.centre[a] = 0.5F * (c.aabbMin[a] + c.aabbMax[a]);','''      c.centre[a] = 0.5F * (c.aabbMin[a] + c.aabbMax[a]);
    c.bag->diagnosticRoadSource=0;
    const auto* plan=RoadReduction::find(unsigned(&c-procChunks.data()));
    if(c.owner==-3 && plan){
      const auto& original=static_cast<const ProcChunk&>(c);
      const bool matched=c.stripRun==75 && !c.roadBlend && !c.roadEdge && c.texBag &&
        c.sts.size()==c.vertices.size() && c.colors.size()==c.vertices.size() &&
        RoadReduction::matches(*plan,unsigned(c.vertices.size()),original.vertices.data(),original.sts.data(),original.colors.data());
      if(!matched){MotionProbe::valid=false;TYRA_ASSERT(false,"Road reduction source mismatch");}
      c.roadDrawVertices.clear();c.roadDrawSts.clear();c.roadDrawColors.clear();
      for(unsigned i=0;i<plan->size;++i){
        const unsigned at=plan->pick[i];
        c.roadDrawVertices.push_back(original.vertices[at]);
        c.roadDrawSts.push_back(original.sts[at]);
        c.roadDrawColors.push_back(original.colors[at]);
      }
      c.bag->diagnosticRoadSource=plan->count;
      const bool apply=MotionProbe::variant()==6;
      if(apply){
        c.roadDrawVertices.bind(c.bag);c.roadDrawSts.bind(c.texBag);c.roadDrawColors.bind(c.colorBag);
        c.bag->count=plan->size;
      }
      printf("ROADREDUCE phase=%u offset=%u id=%u source=%u reduced=%u applied=%u matched=%u\\n",
        MotionProbe::phase,MotionProbe::offset,plan->id,plan->count,plan->size,unsigned(apply),unsigned(matched));
    }''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'road-reduction-manifest.json').write_text(json.dumps(dict(experiment='A06-draw-only-road-reduction',
    plans=plans,inputHashes=[hashlib.sha256(p.read_bytes()).hexdigest() for p in paths],
    note='Private fixed union certified on BOTH target streams; runtime exact source match required; original receiver/contact arrays retained',
    files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(json.dumps(plans,indent=2))
