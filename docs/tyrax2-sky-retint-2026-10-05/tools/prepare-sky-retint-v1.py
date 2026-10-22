from pathlib import Path
import json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');source=(b/'prepare-night-producer-probe-v1.py').read_text(encoding='utf8')
head=source[:source.index("g='game/src/gen/game_lighting.gen.cpp'")].replace('night-producer-probe-physical-v2','sky-retint-physical-v1').replace('kind==13','kind==19').replace('kind=13','kind=19');exec(compile(head,str(Path(__file__)),'exec'))
edit(e+'night_runtime.hpp','NightAblation::producerTiming=NightAblation::producerEnabled&&o>=800&&o<1120;','NightAblation::producerTiming=false;')
edit('game/inc/terrain_game.hpp','  void buildSkyDome();','  void buildSkyDome();\n  bool retintSkyDomeColors();')
g='game/src/gen/game_collision.gen.cpp';text=(base/g).read_text(encoding='utf8');start=text.index('void TerrainGame::buildSkyDome() {');end=text.index('// Coplanar passes',start);body=text[start:end];prefix=body[body.index('#ifdef SKY_TEXTURES_ON'):body.index('  auto latOf')]
function='''// PRIVATE exact colors-only retint. Only the runtime color caller uses this;
// all initialization and scene/texture changes retain the full builder.
bool TerrainGame::retintSkyDomeColors(){
 if(!SKY_DOME)return false;
'''+prefix+'''
 const u32 count=static_cast<u32>(stacks*slices*6);
 if(!skyDome.bag||!skyDome.infoBag||!skyDome.colorBag||
    skyDome.bag->info!=skyDome.infoBag.get()||skyDome.bag->color!=skyDome.colorBag.get()||
    skyDome.bag->count!=count||skyDome.vertices.size()!=count||skyDome.colors.size()!=count||
    skyDome.bag->vertices!=skyDome.vertices.data()||skyDome.colorBag->many!=skyDome.colors.data())return false;
#ifdef SKY_TEXTURES_ON
 if(textured){if(!skyDome.texBag||skyDome.bag->texture!=skyDome.texBag.get()||skyDome.texBag->texture!=skyTex||skyDome.sts.size()!=count||skyDome.texBag->coordinates!=skyDome.sts.data())return false;}
 else if(skyDome.bag->texture!=nullptr)return false;
#else
 if(skyDome.bag->texture!=nullptr)return false;
#endif
 auto colors=skyDome.colors.span(0,count);u32 v=0;
 for(int st=0;st<stacks;++st){
  const float t0=powf((float)st/stacks,SKY_ZENITH_EXP),t1=powf((float)(st+1)/stacks,SKY_ZENITH_EXP);
  const Color c0=textured?tintAt(t0):skyAt(t0),c1=textured?tintAt(t1):skyAt(t1);
  for(int sl=0;sl<slices;++sl){colors[v++]=c0;colors[v++]=c1;colors[v++]=c1;colors[v++]=c0;colors[v++]=c1;colors[v++]=c0;}
 }
 return true;
}

'''
edit(g,'void TerrainGame::buildSkyDome() {',function+'void TerrainGame::buildSkyDome() {')
g='game/src/gen/game_physics.gen.cpp'
old='    skyTopR = dayNightTopR, skyTopG = dayNightTopG, skyTopB = dayNightTopB;\n    buildSkyDome();'
new='''    skyTopR = dayNightTopR, skyTopG = dayNightTopG, skyTopB = dayNightTopB;
    NightAblation::producerCount(0,0);
    const bool cold=NightAblation::producerEnabled&&NightAblation::collectCounters;
    const u32 bboxBefore=skyDome.bag->bboxVersion,vertexBefore=skyDome.vertices.stamp(),stBefore=skyDome.sts.stamp(),colorBefore=skyDome.colors.stamp();
    std::vector<unsigned char> oldVertices,oldSts,newColors;
    if(cold){oldVertices.resize(skyDome.vertices.size()*sizeof(Vec4));oldSts.resize(skyDome.sts.size()*sizeof(Vec4));memcpy(oldVertices.data(),skyDome.vertices.data(),oldVertices.size());if(!oldSts.empty())memcpy(oldSts.data(),skyDome.sts.data(),oldSts.size());}
    const bool retained=NightAblation::producerEnabled&&retintSkyDomeColors();
    if(retained){
     NightAblation::producerCount(0,1);
     if(cold){
      NightAblation::producerCount(1,0);
      const bool geometryStable=bboxBefore==skyDome.bag->bboxVersion&&vertexBefore==skyDome.vertices.stamp()&&stBefore==skyDome.sts.stamp();
      const bool colorsChanged=colorBefore!=skyDome.colors.stamp();
      NightAblation::producerCount(1,1,geometryStable?1:0);NightAblation::producerCount(1,2,colorsChanged?1:0);
      if(!geometryStable||!colorsChanged){NightAblation::producerCount(1,3);NightAblation::valid=false;}
      newColors.resize(skyDome.colors.size()*sizeof(Color));memcpy(newColors.data(),skyDome.colors.data(),newColors.size());
      // Independent original full build only outside the priced window.
      // Its equal output remains live in this cold frame; warm candidate frames
      // retain that geometry and update only colors. No restored stamp fiction.
      buildSkyDome();NightAblation::producerCount(0,2,skyDome.bag->count);
      const bool equal=oldVertices.size()==skyDome.vertices.size()*sizeof(Vec4)&&oldSts.size()==skyDome.sts.size()*sizeof(Vec4)&&newColors.size()==skyDome.colors.size()*sizeof(Color)&&memcmp(oldVertices.data(),skyDome.vertices.data(),oldVertices.size())==0&&(oldSts.empty()||memcmp(oldSts.data(),skyDome.sts.data(),oldSts.size())==0)&&memcmp(newColors.data(),skyDome.colors.data(),newColors.size())==0;
      if(!equal){NightAblation::producerCount(0,3);NightAblation::valid=false;}
     }
    }else{if(NightAblation::producerEnabled)NightAblation::producerCount(0,4);buildSkyDome();}'''
edit(g,old,new)
for n in m['files']:m['files'][n]=sha(out/n)
m['files']=dict(sorted(m['files'].items()));(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());(out/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(out/'target-source-manifest.json'),sourceFiles=501,changes=sorted(changes),baseManifestSha256=sha(base/'target-source-manifest.json'),candidate='Exact colors-only sky retint at runtime RGB caller; initial/scene builders retained; cold independent original arrays/geometry stamps oracle, no scoped clocks',commonCodeFootprintUnpriced=True,existingWaitsRetained=True,coldOriginalOutputLeftLive=True),indent=2)+'\n').encode());print(out)
