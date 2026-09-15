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
#include <algorithm>
#include <array>
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
struct ProcChunk { int owner=0; Tyra::Texture* roadTex=nullptr; int stripRun=0;
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
// The engine's smallest derived package. 72 on every static program class
// this game can route a road bag through, which is why roadgen::kStripRun is
// that number; buildRoads asks rather than assumes, so the oracle answers.
unsigned int minPackageSize() { return 72u; }
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
// A triangle as an ORDER-INDEPENDENT key: the three corners sorted. A strip
// keeps every triangle of the list but neither its rotation nor its winding,
// and both of those are allowed to change (nothing backface-culls).
typedef std::array<float,15> TriKey;
static TriKey triKey(const roadgen::Vertex& a, const roadgen::Vertex& b,
                     const roadgen::Vertex& c) {
  const roadgen::Vertex* p[3]={&a,&b,&c};
  std::sort(p,p+3,[](const roadgen::Vertex* x,const roadgen::Vertex* y){
    const float xa[5]={x->x,x->y,x->z,x->u,x->v};
    const float ya[5]={y->x,y->y,y->z,y->u,y->v};
    for(int i=0;i<5;++i){ if(xa[i]<ya[i]) return true; if(xa[i]>ya[i]) return false; }
    return false; });
  TriKey k{};
  for(int i=0;i<3;++i){ k[i*5+0]=p[i]->x; k[i*5+1]=p[i]->y; k[i*5+2]=p[i]->z;
                        k[i*5+3]=p[i]->u; k[i*5+4]=p[i]->v; }
  return k;
}
static bool degenerateTri(const roadgen::Vertex& a, const roadgen::Vertex& b,
                          const roadgen::Vertex& c) {
  auto same=[](const roadgen::Vertex& x,const roadgen::Vertex& y){
    return x.x==y.x&&x.y==y.y&&x.z==y.z&&x.u==y.u&&x.v==y.v; };
  return same(a,b)||same(b,c)||same(a,c);
}
// Expand strip RUNS the way StaPipCore slices them - a run never splices into
// its neighbour, which is the property pinning packageSize to the run buys -
// and return the triangle multiset the GS would rasterise, degenerates
// dropped. `chunkSizes` bounds the runs: a chunk is a bag, and only a bag's
// LAST run may be short.
static std::vector<TriKey> expandRuns(const std::vector<roadgen::Vertex>& v,
                                      const std::vector<int>& chunkSizes,
                                      size_t* prims, size_t* degen) {
  std::vector<TriKey> out;
  const size_t run=(size_t)roadgen::kStripRun;
  size_t at=0; *prims=0; *degen=0;
  for(int s:chunkSizes) {
    const size_t end=at+(size_t)s;
    for(size_t r=at;r<end;) {
      const size_t len=(end-r)<run?(end-r):run;
      require(len%3==0,"strip run length is not a multiple of 3");
      require(len>=3,"strip run holds no triangle");
      if(r+run<end) require(len==run,"interior strip run is not a full package");
      for(size_t k=0;k+2<len;++k) {
        ++*prims;
        if(degenerateTri(v[r+k],v[r+k+1],v[r+k+2])) { ++*degen; continue; }
        out.push_back(triKey(v[r+k],v[r+k+1],v[r+k+2]));
      }
      r+=len;
    }
    at=end;
  }
  require(at==v.size(),"chunk sizes do not account for every strip vertex");
  std::sort(out.begin(),out.end());
  return out;
}
size_t check(const char* name, const std::vector<float>& points, float width,
             std::function<float(float,float)> height) {
  ROAD_DEFS[0]={0,(int)points.size()/2,-1,0,width};
  for(size_t i=0;i<points.size();++i) ROAD_POINTS[i]=points[i];
  std::vector<roadgen::Vertex> host, hostStrip;
  std::vector<roadgen_dense::Vertex> dense;
  std::vector<int> chunkSizes;
  roadgen::tessellate(points,width,height,host);
  roadgen::tessellateStrips(points,width,height,hostStrip,&chunkSizes);
  roadgen_dense::tessellate(points,width,height,dense);
  requireBaselineSurface(host,dense);

  // The strip is the SAME SURFACE as the list, triangle for triangle. This is
  // the check the pixel comparison can only sample: it is exact, and it is
  // what makes the reordering safe to ship.
  size_t prims=0,degen=0;
  const std::vector<TriKey> fromStrip=expandRuns(hostStrip,chunkSizes,&prims,&degen);
  std::vector<TriKey> fromList;
  for(size_t i=0;i+2<host.size();i+=3) fromList.push_back(triKey(host[i],host[i+1],host[i+2]));
  std::sort(fromList.begin(),fromList.end());
  require(fromStrip==fromList,"strip and list do not draw the same triangles");
  require(hostStrip.size()<=host.size(),"strips are larger than the list they replace");

  TerrainGame game; game.height=height; game.buildRoads(0);
  size_t i=0,ci=0;
  for(const auto& c:game.procChunks) {
    require(c.stripRun==roadgen::kStripRun,"runtime chunk is not marked stripped");
    require(ci<chunkSizes.size(),"runtime has extra chunks");
    require((size_t)chunkSizes[ci]==c.vertices.size(),"host/runtime chunk size mismatch");
    ++ci;
    for(size_t k=0;k<c.vertices.size();++k,++i) {
      require(i<hostStrip.size(),"runtime has extra vertices");
      float a[]={hostStrip[i].x,hostStrip[i].y,hostStrip[i].z,hostStrip[i].u,hostStrip[i].v};
      float b[]={c.vertices[k].x,c.vertices[k].y,c.vertices[k].z,c.sts[k].x,c.sts[k].y};
      for(int n=0;n<5;++n) require(std::isfinite(b[n]) && std::fabs(a[n]-b[n])<1e-5f,"host/runtime vertex or UV mismatch");
    }
  }
  require(ci==chunkSizes.size(),"runtime has missing chunks");
  require(i==hostStrip.size(),"runtime has missing vertices");
  const size_t before=i;
  game.buildRoads(0); // Revisit must replace, not accumulate road geometry.
  i=0; for(const auto& c:game.procChunks) i+=c.vertices.size();
  require(i==before,"scene revisit accumulates geometry");
  std::printf("%s: %zu list -> %zu strip vertices (%.3fx) in %zu chunks, "
              "%zu GS primitives (%zu degenerate); twins agree\n",
              name,host.size(),hostStrip.size(),
              host.empty()?0.0:(double)hostStrip.size()/(double)host.size(),
              chunkSizes.size(),prims,degen);
  return host.size();
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
