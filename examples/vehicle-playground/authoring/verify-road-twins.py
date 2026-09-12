"""Compile and compare the actual host and generated PS2 road tessellators.

Run with Python 3 and g++ on PATH. No emulator or third-party Python modules.
Tiny Tyra stubs replace only storage/rendering; buildRoads is extracted verbatim.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
templates = (root/'src/templates.cpp').read_text()
start = templates.index('void TerrainGame::buildRoads(int scene) {')
end = templates.index('\n}\n)";', start) + 2
runtime = templates[start:end]
stub = r'''
#include "roadgen.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>
#define TYRA_LOG(...) ((void)0)
namespace Tyra {
struct Texture {};
struct Vec4 { float x,y,z,w; Vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
using Color = Vec4;
}
struct ProcChunk { int owner=0; Tyra::Texture* roadTex=nullptr;
std::vector<Tyra::Vec4> vertices,sts,colors; };
struct RoadDefRt { int scene,pointCount,tex,first; float width; };
int ROAD_COUNT=1, ROAD_TEXTURE_COUNT=0;
RoadDefRt ROAD_DEFS[1];
float ROAD_POINTS[64];
const char* ROAD_TEXTURE_PATHS[1]={""};
struct TerrainGame {
std::vector<ProcChunk> procChunks;
Tyra::Texture* roadTextures_[1]={nullptr};
std::function<float(float,float)> height;
float terrainHeightAt(float x,float z) { return height(x,z); }
Tyra::Texture* acquireTexture(const char*) { return nullptr; }
void procFinishChunks() {}
void buildRoads(int);
};
'''
test = r'''
void require(bool ok, const char* text) { if(!ok) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); } }
size_t check(const char* name, const std::vector<float>& points, float width,
             std::function<float(float,float)> height) {
  ROAD_DEFS[0]={0,(int)points.size()/2,-1,0,width};
  for(size_t i=0;i<points.size();++i) ROAD_POINTS[i]=points[i];
  std::vector<roadgen::Vertex> host;
  roadgen::tessellate(points,width,height,host);
  TerrainGame game; game.height=height; game.buildRoads(0);
  size_t i=0;
  for(const auto& c:game.procChunks) for(size_t k=0;k<c.vertices.size();++k,++i) {
    require(i<host.size(),"runtime has extra vertices");
    float a[]={host[i].x,host[i].y,host[i].z,host[i].u,host[i].v};
    float b[]={c.vertices[k].x,c.vertices[k].y,c.vertices[k].z,c.sts[k].x,c.sts[k].y};
    for(int n=0;n<5;++n) require(std::isfinite(b[n]) && std::fabs(a[n]-b[n])<1e-5f,"host/runtime vertex or UV mismatch");
  }
  require(i==host.size(),"runtime has missing vertices");
  const size_t before=i;
  game.buildRoads(0); // Revisit must replace, not accumulate road geometry.
  i=0; for(const auto& c:game.procChunks) i+=c.vertices.size();
  require(i==before,"scene revisit accumulates geometry");
  std::printf("%s: %zu vertices; twins agree\n",name,i);
  return i;
}
int main() {
  std::vector<float> straight={0,0,0,30};
  const auto flat=check("flat",straight,13,[](float,float){return 0.f;});
  const auto slope=check("slope",straight,13,[](float x,float z){return .1f*x+.2f*z;});
  require(slope==flat*26,"13m flat street must use 26x fewer vertices");
  const auto crown=check("crown with equal shoulders",straight,13,[](float x,float){return 1.f-x*x/42.25f;});
  require(crown==slope,"equal shoulders must not flatten an interior crown");
  check("curved flat",{0,0,0,20,15,40,35,30},11,[](float,float){return 3.f;});
  check("saddle",{0,0,0,20,15,40,35,30},11,[](float x,float z){return .003f*x*z;});
  check("flat-to-crest",{0,0,0,20,15,40,35,30},11,[](float x,float z){return z<20?0.f:.01f*(z-20)*(z-20)+.02f*x;});
  check("minimum width",straight,0,[](float,float){return 0.f;});
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='tyrax-roads-') as tmp:
    source = Path(tmp)/'oracle.cpp'
    binary = Path(tmp)/'oracle.exe'
    source.write_text(stub + runtime + test)
    subprocess.run(['g++','-std=c++20','-O2','-I',str(root/'src'),str(source),
                    str(root/'src/roadgen.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
