"""Private constant-colour road trial; geometry and VU paint stay unchanged."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
out,source=[Path(p).resolve() for p in sys.argv[1:3]]
reverse='--reverse' in sys.argv
repack='--repack' in sys.argv
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-vu-paint.py'),str(out),str(source),'--roads-day']+
               (['--reverse'] if reverse else []),check=True)
def edit(path,old,new):
    text=path.read_text()
    assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())
engine=out/'tyra/engine'
edit(out/'game/src/gen/game_physics.gen.cpp',
     'part.envTexBag->paintVu = MotionProbe::variant() == 6;',
     'part.envTexBag->paintVu = true;')
proc=next(p for p in (out/'game/src').rglob('*.cpp') if '    c.colors.bind(c.colorBag);' in p.read_text())
edit(proc,'    c.colors.bind(c.colorBag);', '''    c.colors.bind(c.colorBag);
    c.colorBag->single=nullptr;
    if(c.owner==-3 && !c.roadBlend && !c.roadEdge && !c.colors.empty()) {
      static const Tyra::Color roadGrey(128.0F,128.0F,128.0F,128.0F);
      const auto& colors=static_cast<const decltype(c.colors)&>(c.colors);
      for(const auto& col:colors) for(int k=0;k<4;++k)
        TYRA_ASSERT(col.rgba[k]==roadGrey.rgba[k],"Nonuniform ordinary road colour");
      if(MotionProbe::variant()==6) {
        c.colorBag->single=&roadGrey;
        c.colorBag->many=nullptr;
        c.colorBag->contentVersion=nullptr;
      }
    }''')
edit(engine/'inc/debug/motion_probe.hpp','inline bool observing(){return false;}',
     'inline U roadSingleDraws=0;\ninline bool observing(){return false;}')
edit(out/'game/src/gen/game_vehicles.gen.cpp','    submitHeavy(c.bag.get());',
     '''    if(MotionProbe::offset>=1000 && MotionProbe::offset<1100 && c.colorBag->single)
      ++MotionProbe::roadSingleDraws;
    submitHeavy(c.bag.get());''')
edit(out/'game/src/terrain_game.cpp',
     '  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {',
     '''  if(MotionProbe::offset==1000) MotionProbe::roadSingleDraws=0;
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {
    unsigned eligible=0,selected=0,vertices=0;
    for(const auto& c:procChunks) if(c.owner==-3 && c.bag && c.bag->count) {
      if(!c.roadBlend && !c.roadEdge) { ++eligible; vertices+=c.bag->count; }
      if(c.colorBag->single) {
        ++selected;
        if(c.roadBlend || c.roadEdge || c.colorBag->many) MotionProbe::valid=false;
      }
    }
    const bool on=MotionProbe::variant()==6;
    if(!eligible || (on ? selected!=eligible || !MotionProbe::roadSingleDraws : selected || MotionProbe::roadSingleDraws)) MotionProbe::valid=false;
    printf("LOG: ROADCOLORS phase=%u eligible=%u selected=%u draws=%u vertices=%u\\n",
      MotionProbe::phase,eligible,selected,MotionProbe::roadSingleDraws,vertices);''')
if repack:
    edit(engine/'inc/debug/motion_probe.hpp','inline U roadSingleDraws=0;',
         'inline U roadSingleDraws=0,roadPackBefore=0,roadPackAfter=0,roadPackVerified=0,roadPackRun=0;')
    edit(out/'game/src/gen/game_vehicles.gen.cpp','  if (any) procFinishChunks();',r'''  if(scene==0) {
    MotionProbe::roadPackBefore=MotionProbe::roadPackAfter=MotionProbe::roadPackVerified=0;
    const unsigned target=stapip.core.getMaxVertCountByParams(true,false,true);
    MotionProbe::roadPackRun=MotionProbe::variant()==6?target:75;
    TYRA_ASSERT(target>=75 && target%3==0,"Single colour strip capacity");
    for(auto& chunk:procChunks) {
      if(chunk.owner!=-3 || chunk.stripRun<=0 || chunk.roadBlend || chunk.roadEdge) continue;
      const auto& oldV=static_cast<const decltype(chunk.vertices)&>(chunk.vertices);
      const auto& oldS=static_cast<const decltype(chunk.sts)&>(chunk.sts);
      const size_t oldRun=chunk.stripRun;
      MotionProbe::roadPackBefore+=(oldV.size()+oldRun-1)/oldRun;
      if(MotionProbe::variant()!=6) {
        MotionProbe::roadPackAfter+=(oldV.size()+oldRun-1)/oldRun;
        continue;
      }
      std::vector<Tyra::Vec4> v,s;
      size_t start=0;
      auto push=[&](const Tyra::Vec4& p,const Tyra::Vec4& uv) {
        if(v.size()-start==target) {
          const size_t n=v.size();
          const auto a=v[n-2],b=v[n-1],sa=s[n-2],sb=s[n-1];
          start=n;v.push_back(a);v.push_back(b);s.push_back(sa);s.push_back(sb);
        }
        v.push_back(p);s.push_back(uv);
      };
      for(size_t first=0;first<oldV.size();first+=oldRun) {
        size_t skip=0;
        if(v.size()>=2 && first+1<oldV.size() &&
           memcmp(&v[v.size()-2],&oldV[first],sizeof(Tyra::Vec4))==0 &&
           memcmp(&v[v.size()-1],&oldV[first+1],sizeof(Tyra::Vec4))==0 &&
           memcmp(&s[s.size()-2],&oldS[first],sizeof(Tyra::Vec4))==0 &&
           memcmp(&s[s.size()-1],&oldS[first+1],sizeof(Tyra::Vec4))==0) {
          skip=2;
        } else if(!v.empty()) {
          const auto last=v.back(),uv=s.back();
          push(last,uv);push(oldV[first],oldS[first]);
        }
        const size_t end=std::min(first+oldRun,oldV.size());
        for(size_t i=first+skip;i<end;++i) push(oldV[i],oldS[i]);
      }
      while((v.size()-start)%3) {const auto p=v.back(),uv=s.back();push(p,uv);}
      // Compare every nondegenerate triangle's actual position/ST bytes,
      // in order, before replacing any source storage. No backface culling.
      auto next=[](const auto& a,size_t run,size_t& i) {
        while(i+2<a.size()) {
          if(i/run!=(i+2)/run) {i=((i/run)+1)*run;continue;}
          auto eq=[](const auto& x,const auto& y){return x.x==y.x&&x.y==y.y&&x.z==y.z;};
          if(eq(a[i],a[i+1])||eq(a[i],a[i+2])||eq(a[i+1],a[i+2])) {++i;continue;}
          return true;
        }
        return false;
      };
      size_t a=0,b=0;
      while(next(oldV,oldRun,a)) {
        TYRA_ASSERT(next(v,target,b),"Repack lost triangle");
        for(unsigned j=0;j<3;++j) {
          TYRA_ASSERT(memcmp(&oldV[a+j],&v[b+j],sizeof(Tyra::Vec4))==0,"Repack position mismatch");
          TYRA_ASSERT(memcmp(&oldS[a+j],&s[b+j],sizeof(Tyra::Vec4))==0,"Repack ST mismatch");
        }
        ++MotionProbe::roadPackVerified;++a;++b;
      }
      TYRA_ASSERT(!next(v,target,b),"Repack extra triangle");
      chunk.vertices.clear();chunk.sts.clear();chunk.colors.clear();
      const Tyra::Color grey(128,128,128,128);
      for(size_t i=0;i<v.size();++i) {
        chunk.vertices.push_back(v[i]);chunk.sts.push_back(s[i]);chunk.colors.push_back(grey);
      }
      chunk.stripRun=target;
      MotionProbe::roadPackAfter+=(v.size()+target-1)/target;
    }
    roadIdxDirty=true;
  }
  if (any) procFinishChunks();''')
    edit(out/'game/src/terrain_game.cpp','    unsigned eligible=0,selected=0,vertices=0;',r'''    printf("LOG: ROADPACK phase=%u run=%u before=%u after=%u verified=%u\n",
      MotionProbe::phase,MotionProbe::roadPackRun,MotionProbe::roadPackBefore,MotionProbe::roadPackAfter,MotionProbe::roadPackVerified);
    unsigned eligible=0,selected=0,vertices=0;''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='road-single-colour',
    modes=[6,0,6] if reverse else [0,6,0],constantVuPaint=True,repack=repack,
    sourceProject=str(source),files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
