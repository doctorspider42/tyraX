from pathlib import Path
import hashlib,json
r=Path('F:/Projects/tyrax2-lab-20261001/strip-batch-trial-v1');g=r/'vehicle/game';engine=r/'tyra/engine'
def edit(p,old,new):
 s=p.read_text();assert s.count(old)==1,(p,old[:70],s.count(old));p.write_bytes(s.replace(old,new).encode())
p=g/'src/gen/game_collision.gen.cpp'
edit(p,'#include "game_runtime.gen.hpp"','#include "game_runtime.gen.hpp"\n#include "strip_pack.hpp"')
edit(p,'  bool useStrips = b.stripRun != 0;','  std::vector<std::pair<std::size_t,std::size_t>> packSpans;\n  bool useStrips = b.stripRun != 0;')
edit(p,'      for (size_t vi = 0; vi + 7 < sv.size(); vi += 8) {','      const size_t memberStart = b.vertices.size();\n      for (size_t vi = 0; vi + 7 < sv.size(); vi += 8) {')
edit(p,'      if (useStrips && !b.vertices.empty())\n        while', '''      if (useStrips) {
        const size_t count = sv.size()/8;
        for(size_t off=0;off<count;off+=b.stripRun)
          packSpans.emplace_back(memberStart+off,std::min<size_t>(b.stripRun,count-off));
      }
      if (useStrips && !b.vertices.empty())
        while''')
edit(p,'  if (!b.bag) {\n    b.colorBag =', '''  if(useStrips && !Closeout::middle(NightSampler::phase)) {
    const auto packed = StripPack::indices(packSpans,b.stripRun);
    if(!packed.empty() && packed.size()<b.vertices.size()) {
      BagArray<Vec4> vertices,sts;
      BagArray<Color> colors;
      vertices.reserve(packed.size());colors.reserve(packed.size());
      if(!b.sts.empty())sts.reserve(packed.size());
      const auto& oldVertices=static_cast<const BagArray<Vec4>&>(b.vertices);
      const auto& oldColors=static_cast<const BagArray<Color>&>(b.colors);
      const auto& oldSts=static_cast<const BagArray<Vec4>&>(b.sts);
      for(const size_t i:packed) {
        vertices.push_back(oldVertices[i]);colors.push_back(oldColors[i]);
        if(!oldSts.empty())sts.push_back(oldSts[i]);
      }
      b.vertices.swap(vertices);b.colors.swap(colors);b.sts.swap(sts);
    }
  }
  if (!b.bag) {
    b.colorBag =''')
# Remove heavy observers for this comparison, keep ordinary coarse clocks.
edit(engine/'inc/debug/loop_balance.hpp','bool middle=i>=40&&i<88;return p==0||(p==1?middle:!middle);','return false;')
# Fixed parked camera, same full ordinary city for all three arms.
p=g/'src/terrain_game.cpp';s=p.read_text();start=s.index('  // PRIVATE daylight route/view diagnosis.');end=s.index('  // PRIVATE cold actual motion endpoints;',start)
s=s[:start]+'''  // PRIVATE stationary comparison. Capture ordinary spawn camera once.
  static Vec4 fixedEye,fixedLook;
  static bool fixed=false;
  if(NightSampler::frame==700){fixedEye=cameraPosition;fixedLook=cameraLookAt;fixed=true;}
  if(fixed){cameraPosition=fixedEye;cameraLookAt=fixedLook;}
  if(NightSampler::frame==1900 || NightSampler::frame==3700)
    for(auto& batch:staticBatches)batch.dirty=true;
  if(Closeout::cold()) {
    unsigned vertices=0,batches=0,stripBatches=0;
    for(const auto& batch:staticBatches)if(batch.bag){vertices+=batch.bag->count;++batches;stripBatches+=batch.bag->stripped?1:0;}
    printf("LOG: STRIPPACK phase=%u offset=%u enabled=%u batches=%u stripped=%u residentVertices=%u fixed=%u\\n",unsigned(NightSampler::phase),unsigned(NightSampler::frame%1800),unsigned(!Closeout::middle(NightSampler::phase)),batches,stripBatches,vertices,unsigned(fixed));fflush(stdout);
  }
'''+s[end:];s=s.replace('CloseoutMotion::apply(engine->pad, NightSampler::frame, 1U);','CloseoutMotion::apply(engine->pad, NightSampler::frame, 0U);');p.write_bytes(s.encode())
(g/'bin/closeout.cfg').write_bytes(b'0 3\n')
files={}
for f in sorted(g.rglob('*')):
 if f.is_file() and f.suffix in ('.cpp','.hpp','.h','.c','.S') or f.is_file() and f.name=='Makefile':files['game/'+f.relative_to(g).as_posix()]=hashlib.sha256(f.read_bytes()).hexdigest()
for f in sorted((r/'tyra').rglob('*')):
 if f.is_file() and (f.suffix in ('.cpp','.hpp','.h','.c','.vcl','.S') or f.name in ('Makefile','Makefile.base')):files[f.relative_to(r).as_posix()]=hashlib.sha256(f.read_bytes()).hexdigest()
(r/'source-review.json').write_text(json.dumps(dict(files=files,route=0,sourceHead='28628d8d7',privateOnly=True,ordinaryClocks=True,addedWaits=0),indent=2)+'\n')
print('prepared',len(files))
