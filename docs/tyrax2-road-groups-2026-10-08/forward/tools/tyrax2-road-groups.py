"""Private draw-only road groups; preserve original acceptance and receivers."""
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
out,source=[Path(p).resolve() for p in sys.argv[1:3]]
reverse='--reverse' in sys.argv
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-vu-paint.py'),str(out),str(source),'--roads-day']+
               (['--reverse'] if reverse else []),
               check=True,env={**os.environ,'PYTHONUTF8':'1'})


def edit(path,old,new):
    text=path.read_text(encoding='utf-8-sig')
    assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())


game=out/'game'
edit(game/'inc/terrain_config.hpp','constexpr int INTERLEAVE_PASSES = 1;','constexpr int INTERLEAVE_PASSES = 2;')
edit(game/'src/gen/game_physics.gen.cpp','part.envTexBag->paintVu = MotionProbe::variant() == 6;',
     'part.envTexBag->paintVu = true;')
edit(game/'inc/terrain_game.hpp','  std::vector<ProcChunk> procChunks;', '''  std::vector<ProcChunk> procChunks;
  std::vector<ProcChunk> roadDrawGroups;
  std::vector<size_t> roadGroupFirst,roadGroupLast,roadGroupAt;
  std::vector<unsigned> roadGroupInput;
  std::vector<unsigned char> roadDrawVisible;''')
vehicles=game/'src/gen/game_vehicles.gen.cpp'
edit(vehicles,'void TerrainGame::buildRoads(int scene) {', '''void TerrainGame::buildRoads(int scene) {
  roadDrawGroups.clear();roadGroupFirst.clear();roadGroupLast.clear();roadGroupAt.clear();roadDrawVisible.clear();roadGroupInput.clear();''')
edit(vehicles,'  if (any) procFinishChunks();', r'''  if (any) procFinishChunks();
  roadGroupAt.assign(procChunks.size(),size_t(-1));
  roadDrawVisible.resize(procChunks.size());
  roadDrawGroups.reserve(procChunks.size());
  auto eligible=[](const ProcChunk& c) {
    for(const auto& color:c.colors) if(color.rgba[3]!=128.0F)return false;
    return c.owner==-3 && !c.roadBlend && !c.roadEdge && c.bag && c.texBag &&
      c.stripRun>0 && c.bag->count>=3 && c.bag->count%3==0 &&
      c.bag->stripped && !c.bag->lighting && !c.bag->billboard &&
      c.colorBag && c.colorBag->many && !c.colorBag->single &&
      !c.texBag->coordinatesAreNormals && c.vertices.size()==c.colors.size() &&
      c.vertices.size()==c.sts.size();
  };
  for(size_t first=0;first<procChunks.size();) {
    const auto& base=procChunks[first];
    if(!eligible(base)){++first;continue;}
    size_t last=first+1;
    while(last<procChunks.size() && last-first<4) {
      const auto& c=procChunks[last];
      if(!eligible(c) || c.bag->info!=base.bag->info || c.stripRun!=base.stripRun ||
         c.bag->packageSize!=base.bag->packageSize || c.bag->refineStrips!=base.bag->refineStrips ||
         c.texBag->texture!=base.texBag->texture || c.texBag->textureFunction!=base.texBag->textureFunction) break;
      ++last;
    }
    if(last-first<2){first=last;continue;}
    const size_t group=roadDrawGroups.size();
    roadDrawGroups.emplace_back();auto& g=roadDrawGroups.back();
    g.owner=-3;g.stripRun=base.stripRun;
    g.bag=std::make_unique<Tyra::StaPipBag>(*base.bag);
    g.colorBag=std::make_unique<Tyra::StaPipColorBag>(*base.colorBag);
    g.texBag=std::make_unique<Tyra::StaPipTextureBag>(*base.texBag);
    unsigned originalCount=0;
    for(size_t i=first;i<last;++i) {
      const auto& c=static_cast<const ProcChunk&>(procChunks[i]);
      originalCount+=c.bag->count;
      for(size_t k=0;k<c.vertices.size();++k) {
        g.vertices.push_back(c.vertices[k]);g.colors.push_back(c.colors[k]);g.sts.push_back(c.sts[k]);
      }
      if(i+1<last) while(g.vertices.size()%unsigned(g.stripRun)) {
        g.vertices.push_back(c.vertices.back());g.colors.push_back(c.colors.back());g.sts.push_back(c.sts.back());
      }
    }
    g.vertices.bind(g.bag);g.colors.bind(g.colorBag);g.sts.bind(g.texBag);
    g.bag->color=g.colorBag.get();g.bag->texture=g.texBag.get();g.bag->bboxVersion=++g_bboxStamp;
    size_t offset=0;
    for(size_t i=first;i<last;++i) {
      const auto& c=static_cast<const ProcChunk&>(procChunks[i]);
      TYRA_ASSERT(memcmp(g.bag->vertices+offset,c.bag->vertices,c.bag->count*sizeof(Tyra::Vec4))==0 &&
                  memcmp(g.colorBag->many+offset,c.colorBag->many,c.bag->count*sizeof(Tyra::Color))==0 &&
                  memcmp(g.texBag->coordinates+offset,c.texBag->coordinates,c.bag->count*sizeof(Tyra::Vec4))==0,
                  "Road group input mismatch");
      offset+=c.bag->count;
      if(i+1<last) while(offset%unsigned(g.stripRun)) {
        TYRA_ASSERT(memcmp(g.bag->vertices+offset,c.bag->vertices+c.bag->count-1,sizeof(Tyra::Vec4))==0 &&
                    memcmp(g.colorBag->many+offset,c.colorBag->many+c.bag->count-1,sizeof(Tyra::Color))==0 &&
                    memcmp(g.texBag->coordinates+offset,c.texBag->coordinates+c.bag->count-1,sizeof(Tyra::Vec4))==0,
                    "Road group padding mismatch");
        ++offset;
      }
    }
    TYRA_ASSERT(offset==g.bag->count,"Road group length mismatch");
    roadGroupFirst.push_back(first);roadGroupLast.push_back(last);roadGroupAt[first]=group;
    roadGroupInput.push_back(originalCount);
    first=last;
  }''')
text=vehicles.read_text(encoding='utf-8-sig')
start=text.index('void TerrainGame::renderRoadChunks() {')
end=text.index('\n}',start)+2
old=text[start:end]
loop_start=old.index('  for (ProcChunk& c : procChunks) {')
loop_end=old.index('    submitHeavy(c.bag.get());',loop_start)
accept=old[loop_start+len('  for (ProcChunk& c : procChunks) {'):loop_end]
accept=accept.replace('continue;','return false;')
new=old[:loop_start]+'''  TYRA_ASSERT(roadDrawVisible.size()==procChunks.size(),"Road groups invalidated");
  auto accepted=[&](const ProcChunk& c) {'''+accept+'''    return true;
  };
  const bool witness=MotionProbe::offset>=1000 && MotionProbe::offset<1100;
  bool unlit=!engine->renderer.core.spot.enabled && !engine->renderer.core.blss.wantsProxies();
  for(const auto& light:engine->renderer.core.dynLights) if(light.enabled) unlit=false;
  const bool useGroups=MotionProbe::variant()==6 && unlit;
  for(size_t i=0;i<procChunks.size();++i) {
    roadDrawVisible[i]=accepted(procChunks[i]);
    if(witness && roadDrawVisible[i]) {
      ++MotionProbe::groupAccepted;MotionProbe::groupInput+=procChunks[i].bag->count;
    }
  }
  for(size_t i=0;i<procChunks.size();++i) {
    if(!roadDrawVisible[i])continue;
    const size_t group=roadGroupAt[i];
    bool merged=useGroups && group!=size_t(-1);
    if(merged) for(size_t k=i;k<roadGroupLast[group];++k) if(!roadDrawVisible[k])merged=false;
    Tyra::StaPipBag* draw=merged?roadDrawGroups[group].bag.get():procChunks[i].bag.get();
    if(witness) {
      ++MotionProbe::groupSubmitted;MotionProbe::groupOutput+=draw->count;
      if(merged){++MotionProbe::groupMerged;MotionProbe::groupPadding+=draw->count-roadGroupInput[group];}
    }
    submitHeavy(draw);
    if(merged)i=roadGroupLast[group]-1;
  }
}'''
edit(vehicles,old,new)
header=out/'tyra/engine/inc/debug/motion_probe.hpp'
edit(header,'inline bool observing(){return false;}',
     'inline U groupAccepted=0,groupInput=0,groupSubmitted=0,groupOutput=0,groupMerged=0,groupPadding=0;\ninline bool observing(){return false;}')
edit(game/'src/terrain_game.cpp',
     '  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {',r'''  if(MotionProbe::offset==1000)
    MotionProbe::groupAccepted=MotionProbe::groupInput=MotionProbe::groupSubmitted=MotionProbe::groupOutput=MotionProbe::groupMerged=MotionProbe::groupPadding=0;
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {
    const bool grouped=MotionProbe::variant()==6;
    if(roadDrawGroups.empty() || !MotionProbe::groupAccepted || MotionProbe::groupInput+MotionProbe::groupPadding!=MotionProbe::groupOutput ||
       (grouped ? (!MotionProbe::groupMerged || MotionProbe::groupSubmitted>=MotionProbe::groupAccepted)
                : (MotionProbe::groupMerged || MotionProbe::groupSubmitted!=MotionProbe::groupAccepted))) MotionProbe::valid=false;
    printf("LOG: ROADGROUP phase=%u groups=%u accepted=%u input=%u submitted=%u output=%u merged=%u padding=%u interleave=%u\n",
      MotionProbe::phase,unsigned(roadDrawGroups.size()),MotionProbe::groupAccepted,MotionProbe::groupInput,
      MotionProbe::groupSubmitted,MotionProbe::groupOutput,MotionProbe::groupMerged,MotionProbe::groupPadding,unsigned(INTERLEAVE_PASSES));''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='road-draw-only-groups',
    modes=[6,0,6] if reverse else [0,6,0],fixedInterleave=2,maxMembers=4,constantVuPaint=True,
    originalChunksRetained=True,allMembersMustBeAccepted=True,
    sourceProject=str(source),files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
