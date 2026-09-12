"""Compile road twins and sample them against the preserved road baseline.

Run with Python 3 and g++ on PATH. No emulator or third-party Python modules.
Tiny Tyra stubs replace only storage/rendering; buildRoads is extracted verbatim.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
templates = (root/'src/templates.cpp').read_text()
roadgen_source = (root/'src/roadgen.cpp').read_text()
roadgen_header = (root/'src/roadgen.hpp').read_text()
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
bool sample(const std::vector<roadgen::Vertex>& mesh, float x, float z, roadgen::Vertex* out) {
  for (size_t i=0; i+2<mesh.size(); i+=3) {
    const auto& a=mesh[i]; const auto& b=mesh[i+1]; const auto& c=mesh[i+2];
    const float den=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);
    if (std::fabs(den)<1e-7f) continue;
    const float w0=((b.z-c.z)*(x-c.x)+(c.x-b.x)*(z-c.z))/den;
    const float w1=((c.z-a.z)*(x-c.x)+(a.x-c.x)*(z-c.z))/den;
    const float w2=1.f-w0-w1;
    if (w0>=-1e-4f && w1>=-1e-4f && w2>=-1e-4f) {
      *out={w0*a.x+w1*b.x+w2*c.x,w0*a.y+w1*b.y+w2*c.y,x*0+z*0,
            w0*a.u+w1*b.u+w2*c.u,w0*a.v+w1*b.v+w2*c.v}; return true;
    }
  } return false;
}
void requireBaselineSurface(const std::vector<roadgen::Vertex>& opt,
                            const std::vector<roadgen_dense::Vertex>& dense) {
  for(size_t i=0;i+2<dense.size();i+=3) {
    const auto& a=dense[i]; const auto& b=dense[i+1]; const auto& c=dense[i+2];
    const float x=(a.x+b.x+c.x)/3.f, z=(a.z+b.z+c.z)/3.f;
    const float y=(a.y+b.y+c.y)/3.f, u=(a.u+b.u+c.u)/3.f, v=(a.v+b.v+c.v)/3.f;
    bool matched=false;
    // A tightly looping road can overlap itself in XZ. Test every covering
    // triangle: the preserved baseline surface must still be represented.
    for(size_t k=0;k+2<opt.size();k+=3) {
      std::vector<roadgen::Vertex> one={opt[k],opt[k+1],opt[k+2]}; roadgen::Vertex q;
      if(sample(one,x,z,&q) && std::fabs(q.y-y)<1e-4f && std::fabs(q.u-u)<1e-4f && std::fabs(q.v-v)<1e-4f) { matched=true; break; }
    }
    require(matched,"optimized surface or UV differs from dense reference");
  }
}
size_t check(const char* name, const std::vector<float>& points, float width,
             std::function<float(float,float)> height) {
  ROAD_DEFS[0]={0,(int)points.size()/2,-1,0,width};
  for(size_t i=0;i<points.size();++i) ROAD_POINTS[i]=points[i];
  std::vector<roadgen::Vertex> host;
  std::vector<roadgen_dense::Vertex> dense;
  roadgen::tessellate(points,width,height,host);
  roadgen_dense::tessellate(points,width,height,dense);
  requireBaselineSurface(host,dense);
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
  require(slope==flat,"a planar slope must collapse without changing its surface");
  const auto crown=check("crown with equal shoulders",straight,13,[](float x,float){return 1.f-x*x/42.25f;});
  require(crown==flat*26,"equal shoulders must not flatten an interior crown");
  const auto curvedFlat=check("curved flat (legacy)",{0,0,0,20,15,40,35,30},11,
      [](float,float){return 3.f;});
  require(curvedFlat==420,"curved flat spans keep the established reduction");
  const auto curved=check("curved plane",{0,0,0,20,15,40,35,30},11,
      [](float x,float z){return 3.f+.03f*x-.02f*z;});
  require(curved==9240,"a curved plane must keep its dense UV mapping");
  check("saddle",{0,0,0,20,15,40,35,30},11,[](float x,float z){return .003f*x*z;});
  check("flat-to-crest",{0,0,0,20,15,40,35,30},11,[](float x,float z){return z<20?0.f:.01f*(z-20)*(z-20)+.02f*x;});
  check("minimum width",straight,0,[](float,float){return 0.f;});
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='tyrax-roads-') as tmp:
    source = Path(tmp)/'oracle.cpp'
    dense_source = Path(tmp)/'roadgen_dense.cpp'
    dense_header = Path(tmp)/'roadgen_dense.hpp'
    binary = Path(tmp)/'oracle.exe'
    dense_header.write_text(roadgen_header.replace('namespace roadgen', 'namespace roadgen_dense'))
    baseline_stride = 'const int stride = (flat || planar) ? crossSteps : 1;'
    if roadgen_source.count(baseline_stride) != 1:
        raise RuntimeError('road baseline stride changed; update the oracle deliberately')
    # The reference is the prior shipped behavior: horizontal spans collapse,
    # every non-flat span stays dense. The candidate may additionally collapse
    # only its proven affine/coplanar non-flat spans.
    dense_source.write_text(roadgen_source.replace('#include "roadgen.hpp"', '#include "roadgen_dense.hpp"')
                            .replace('namespace roadgen', 'namespace roadgen_dense')
                            .replace(baseline_stride, 'const int stride = flat ? crossSteps : 1;'))
    source.write_text(stub + '#include "roadgen_dense.hpp"\n' + runtime + test)
    subprocess.run(['g++','-std=c++20','-O2','-I',str(root/'src'),'-I',tmp,str(source),
                    str(root/'src/roadgen.cpp'),str(dense_source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
