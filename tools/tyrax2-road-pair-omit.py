"""P07 destructive attribution of the two P06 66-vertex clip-heavy road bags."""
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out,source=[Path(p).resolve() for p in sys.argv[1:3]]
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-vu-paint.py'),str(out),str(source),'--roads-day'],
 check=True,env={**os.environ,'PYTHONUTF8':'1'})
def edit(path,old,new):
    text=path.read_text(encoding='utf-8-sig');assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())
engine=out/'tyra/engine';game=out/'game'
edit(game/'inc/terrain_config.hpp','constexpr int INTERLEAVE_PASSES = 1;','constexpr int INTERLEAVE_PASSES = 2;')
edit(game/'src/gen/game_physics.gen.cpp','part.envTexBag->paintVu = MotionProbe::variant() == 6;',
 'part.envTexBag->paintVu = true;')
edit(engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp','  StaPipBag();',
 '  unsigned diagnosticRoadId=0;\n  StaPipBag();')
edit(game/'src/gen/game_collision.gen.cpp','    c.colors.bind(c.colorBag);',
 '    c.colors.bind(c.colorBag);\n    c.bag->diagnosticRoadId=c.owner==-3?unsigned(&c-procChunks.data())+1:0;')
edit(engine/'inc/debug/motion_probe.hpp','inline bool observing(){return false;}',r'''inline U pairEligible=0,pairSkipped=0,pairTriangles=0;
inline bool omitPairClip(U id,U triangles) {
  if(id!=24 && id!=34)return false;
  const bool skip=variant()==6;
  if(offset>=1000 && offset<1100){++pairEligible;pairTriangles+=triangles;if(skip)++pairSkipped;}
  return skip;
}
inline bool observing(){return false;}''')
edit(engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp',
 '      for (u32 t = 0; t < triangles; t += trisPerChunk) {',
 '      if(MotionProbe::omitPairClip(packages[i].bag->diagnosticRoadId,triangles))continue;\n      for (u32 t = 0; t < triangles; t += trisPerChunk) {')
edit(game/'src/terrain_game.cpp',
 '  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {',r'''  if(MotionProbe::offset==1000)MotionProbe::pairEligible=MotionProbe::pairSkipped=MotionProbe::pairTriangles=0;
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {
    unsigned found=0;
    for(const auto& c:procChunks) if(c.bag && (c.bag->diagnosticRoadId==24 || c.bag->diagnosticRoadId==34)) {
      TYRA_ASSERT(c.owner==-3 && c.bag->stripped && c.bag->count==66 && c.stripRun==75 && !c.bag->refineStrips,"Pair identity changed");
      ++found;unsigned repeated=0;
      auto same=[](const Tyra::Vec4& a,const Tyra::Vec4& b){return a.x==b.x && a.y==b.y && a.z==b.z && a.w==b.w;};
      for(unsigned k=0;k+2<c.bag->count;++k)if(same(c.vertices[k],c.vertices[k+1]) || same(c.vertices[k],c.vertices[k+2]) || same(c.vertices[k+1],c.vertices[k+2]))++repeated;
      printf("LOG: PAIRMETA phase=%u id=%u count=%u repeated=%u blend=%u edge=%u\n",MotionProbe::phase,c.bag->diagnosticRoadId,c.bag->count,repeated,unsigned(c.roadBlend),unsigned(c.roadEdge));
    }
    if(found!=2 || !MotionProbe::pairEligible || (MotionProbe::variant()==6?MotionProbe::pairSkipped!=MotionProbe::pairEligible:MotionProbe::pairSkipped!=0))MotionProbe::valid=false;
    printf("LOG: PAIRCLIP phase=%u eligible=%u skipped=%u triangles=%u interleave=%u\n",MotionProbe::phase,MotionProbe::pairEligible,MotionProbe::pairSkipped,MotionProbe::pairTriangles,unsigned(INTERLEAVE_PASSES));''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='two-road-clip-omission',modes=[0,6,0],
 fixedInterleave=2,ids=[24,34],normalPreparation=True,destructive=True,constantVuPaint=True,
 files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
