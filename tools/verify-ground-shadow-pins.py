"""Compile actual generated-runtime pin/reset bodies against ownership stubs."""
import importlib.util
import subprocess
import tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('quiet',root/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
source=(root/'src/game_templates.inc').read_text()
bodies=[]
for name in ('resetTerrainChunks','prepareGroundShadowTextures'):
    start,end=quiet.body_span(source,'TerrainGame::'+name)
    bodies.append('void TerrainGame::'+name+'() '+source[start:end])
stub=r'''
#include <cassert>
#include <map>
#include <string>
#include <vector>
#define GROUND_SHADOWS_ON 1
namespace Tyra { enum { bpp4=4 }; }
struct Core { int bpp=4; };
struct Texture { Core value; Core* core=&value; int width=128;
  int getWidth()const{return width;} int getHeight()const{return width;} };
int HM_W=33,HM_D=33,TERRAIN_CHUNK_CELLS=16;
bool TERRAIN_ENABLED=true;
float TERRAIN_VIEW_DISTANCE=0,TERRAIN_WIDTH=256,TERRAIN_DEPTH=256;
int MULTIPLAYER_MODE=0,PLAYER2_INDEX=-1,g_activeScene=0,SCENE_GROUND_CHUNKS_X=2;
unsigned SCENE_GROUND_PRELOAD_BYTES[2]={40000,40000};
std::vector<unsigned short> masks(4096,1);
const unsigned short* SCENE_GROUND=masks.data();
struct TerrainGame {
 struct TerrainChunk { std::string gsTexPath; };
 std::vector<TerrainChunk> terrainChunks;
 std::vector<short> terrainChunkSlot;
 int terrainChunksX=0,terrainChunksZ=0;
 bool infoBag=true,layerInfoBag=true;
 std::vector<std::string> groundShadowPins;
 struct Entry {Texture* texture;int refs;};
 std::map<std::string,Entry> cache;
 int loads=0,frees=0,badAt=-1;
 Texture* acquireTexture(const std::string& path){
   auto it=cache.find(path);
   if(it==cache.end()){
     auto* t=new Texture; if(loads==badAt)t->value.bpp=32;
     ++loads;it=cache.emplace(path,Entry{t,0}).first;
   }
   ++it->second.refs;return it->second.texture;
 }
 void releaseTexture(const std::string& path){
   auto it=cache.find(path);assert(it!=cache.end());
   assert(it->second.refs>0);
   if(--it->second.refs==0){delete it->second.texture;cache.erase(it);++frees;}
 }
 void resetTerrainChunks();void prepareGroundShadowTextures();
};
'''
tests=r'''
int main(){
 TerrainGame g;g.resetTerrainChunks();
 assert(g.groundShadowPins.size()==4 && g.loads==4 && g.cache.size()==4);
 auto path=g.groundShadowPins.front();auto* original=g.acquireTexture(path);
 g.terrainChunks[0].gsTexPath=path;
 assert(g.acquireTexture(path)==original && g.loads==4);g.releaseTexture(path);
 g_activeScene=1;g.resetTerrainChunks();
 assert(g.loads==8 && g.frees==4 && g.cache.size()==4);
 for(auto& p:g.groundShadowPins)assert(p.find("gshadow/s1_")==0);
 TERRAIN_ENABLED=false;g.resetTerrainChunks();
 assert(g.cache.empty() && g.groundShadowPins.empty() && g.loads==g.frees);
 TERRAIN_ENABLED=true;SCENE_GROUND_PRELOAD_BYTES[1]=512*1024+1;
 g.resetTerrainChunks();assert(g.cache.empty() && g.loads==8);
 SCENE_GROUND_PRELOAD_BYTES[1]=40000;g.badAt=9;g.resetTerrainChunks();
 assert(g.cache.empty() && g.groundShadowPins.empty() && g.loads==g.frees);
 g.badAt=-1;SCENE_GROUND=nullptr;g.resetTerrainChunks();assert(g.cache.empty());
 SCENE_GROUND=masks.data();g.layerInfoBag=false;g.resetTerrainChunks();assert(g.cache.empty());
 g.layerInfoBag=true;for(auto& r:masks)r=0;g.resetTerrainChunks();assert(g.cache.empty());
 for(auto& r:masks)r=1;
 HM_W=HM_D=257;SCENE_GROUND_CHUNKS_X=16;
 // Corrupt/underestimated metadata must still respect the runtime bound.
 int before=g.loads;g.resetTerrainChunks();
 assert(g.loads-before==57 && g.cache.empty() && g.groundShadowPins.empty());
 assert(g.loads==g.frees);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)
    for name,defines,main in [('ground','',tests),('no-ground','#undef GROUND_SHADOWS_ON\n',
            'int main(){TerrainGame g;g.resetTerrainChunks();assert(g.loads==0 && g.cache.empty());}')]:
        (p/'test.cpp').write_text(stub+defines+'\n'.join(bodies)+main)
        subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                        str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
print('PASS: actual reset/preload bodies; reuse, scene switch, terrain off, budget, invalid format, no maps, runtime rollback; ASan/UBSan')
