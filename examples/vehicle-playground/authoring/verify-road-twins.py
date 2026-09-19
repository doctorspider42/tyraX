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
struct RoadJunctionRt { int scene,tex; float xz[10]; };
int ROAD_JUNCTION_COUNT=0;
RoadJunctionRt ROAD_JUNCTIONS[1]{};
struct TerrainGame {
std::vector<ProcChunk> procChunks;
Tyra::Texture* roadTextures_[1]={nullptr};
std::function<float(float,float)> height;
float terrainHeightAt(float x,float z) { return height(x,z); }
Tyra::Texture* acquireTexture(const char*) { return nullptr; }
// The engine's smallest derived package - 75 on every static program class
// this game can route a road bag through, which is why roadgen::kStripRun is
// that number; buildRoads asks rather than assumes, so the oracle answers.
// Tied to the constant rather than written out: this stub was a literal 72,
// and when the package ceiling moved to 75 it answered 72 to a runtime asking
// for 75, so buildRoads correctly refused to strip and the harness failed with
// "runtime chunk is not marked stripped" - a true report of a stale ORACLE,
// which reads exactly like a bug in the code under test.
unsigned int minPackageSize() { return (unsigned int)roadgen::kStripRun; }
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
// The two budgets, as the surface check reads them. SURFACE is the one this
// repository keeps exact: kSpanFlatness is the float noise floor, so `dy` may
// not move at all. UV is the one that was relaxed, so it is bounded by the
// shear budget rather than by zero - and the bound is reported per case, not
// merely asserted, because the number IS the quality claim. 0.02 of a texture
// repeat is 2.6 texels on the district's 128-pixel road; the measured worst
// anywhere in the Motor District is 0.36 of a texel, and these synthetic
// fixtures bend harder than any street in it.
static const float kSurfaceTol = 2.f*roadgen::kSpanFlatness + 1e-4f;
static const float kUvTol = 0.02f;
void requireBaselineSurface(const std::vector<roadgen::Vertex>& opt,
                            const std::vector<roadgen_dense::Vertex>& dense,
                            double* worstY, double* worstUv) {
  for(size_t i=0;i+2<dense.size();i+=3) {
    const auto& a=dense[i]; const auto& b=dense[i+1]; const auto& c=dense[i+2];
    const float x=(a.x+b.x+c.x)/3.f, z=(a.z+b.z+c.z)/3.f;
    const float y=(a.y+b.y+c.y)/3.f, u=(a.u+b.u+c.u)/3.f, v=(a.v+b.v+c.v)/3.f;
    bool matched=false;
    // A tightly looping road can overlap itself in XZ. Test every covering
    // triangle: the preserved baseline surface must still be represented.
    double bestY=1e30, bestUv=1e30;
    for(size_t k=0;k+2<opt.size();k+=3) {
      std::vector<roadgen::Vertex> one={opt[k],opt[k+1],opt[k+2]}; roadgen::Vertex q;
      if(!sample(one,x,z,&q)) continue;
      const double dy=std::fabs(q.y-y);
      const double duv=std::max(std::fabs(q.u-u),std::fabs(q.v-v));
      // A tightly looping road overlaps itself in XZ; the far side of the loop
      // is many texture repeats away, and is not this sample's surface.
      if(duv>0.5) continue;
      if(dy<bestY) bestY=dy;
      if(duv<bestUv) bestUv=duv;
      if(dy<=kSurfaceTol && duv<=kUvTol) { matched=true; break; }
    }
    if(bestY<1e29 && bestY>*worstY) *worstY=bestY;
    if(bestUv<1e29 && bestUv>*worstUv) *worstUv=bestUv;
    require(matched,"optimized surface or UV differs from the dense reference "
                    "by more than the published budget");
  }
}
// The seam check, and the reason the lateral merge needs one. Neighbouring
// station pairs cut the row they SHARE independently: one may merge through a
// lateral sample the other keeps, which leaves a T-vertex. Sample every
// covering candidate triangle at each dense-reference vertex and require the
// spread to vanish - a merged span's plane contains every sample of both its
// rows, and a row's samples are a straight line in XZ, so the shared segment
// is collinear and no gap can open. This is what makes the reduction safe
// without a global decision per row; the check exists because the argument is
// subtle, not because it is doubtful.
// It is a T-VERTEX test, not a sampled one: sampling a surface at its own
// vertices reads barycentric noise off every triangle that merely touches the
// point, and that noise floor is larger than the seams worth finding. A crack
// exists exactly when some vertex lies strictly inside another triangle's edge
// in XZ and off it in Y, so that is what this looks for.
void requireNoSeams(const std::vector<roadgen::Vertex>& opt, double* worst) {
  std::vector<roadgen::Vertex> pts;
  for (const auto& v : opt) {
    bool seen = false;
    for (const auto& p : pts)
      if (p.x==v.x && p.y==v.y && p.z==v.z) { seen = true; break; }
    if (!seen) pts.push_back(v);
  }
  for (size_t t = 0; t + 2 < opt.size(); t += 3)
    for (int e = 0; e < 3; ++e) {
      const auto& a = opt[t + (size_t)e];
      const auto& b = opt[t + (size_t)((e + 1) % 3)];
      const float dx = b.x - a.x, dz = b.z - a.z;
      const float len2 = dx*dx + dz*dz;
      if (len2 < 1e-12f) continue;
      for (const auto& v : pts) {
        const float s = ((v.x-a.x)*dx + (v.z-a.z)*dz) / len2;
        if (s <= 1e-4f || s >= 1.f-1e-4f) continue;   // an endpoint, not a T
        const float px = a.x + s*dx, pz = a.z + s*dz;
        const float perp = (v.x-px)*(v.x-px) + (v.z-pz)*(v.z-pz);
        if (perp > 1e-8f) continue;                   // not on this edge
        const double gap = std::fabs((double)(a.y + s*(b.y-a.y)) - (double)v.y);
        if (gap > *worst) *worst = gap;
      }
    }
  require(*worst <= 2.0 * (double)roadgen::kSpanFlatness + 1e-5,
          "a lateral merge opened a seam between neighbouring spans");
}
// The invariant the EDITOR's selected-road overlay reads, pinned here because
// nothing else would catch it breaking. The overlay draws the two authored
// borders by U - A->D is a border when A's u is 0, B->C when B's u is 1 -
// rather than by counting `crossSteps * 6` vertices per station pair, which
// stopped being true the moment a pair could be cut anywhere. That is only
// correct if every station pair's spans run left to right, starting at u 0 and
// ending at u 1, which is exactly what this checks: a span begins a pair if and
// only if the span before it ended one.
void requireBorderRule(const std::vector<roadgen::Vertex>& list, size_t* pairs) {
  *pairs = 0;
  require(list.size() % 6 == 0, "the list emitter did not emit whole spans");
  bool expectStart = true;
  for (size_t s = 0; s + 5 < list.size(); s += 6) {
    const bool startsPair = list[s].u == 0.0f;
    const bool endsPair = list[s + 1].u == 1.0f;
    require(startsPair == expectStart,
            "a station pair's spans do not start at the left shoulder");
    if (endsPair) ++*pairs;
    expectStart = endsPair;
  }
  require(expectStart, "the last station pair does not reach the right shoulder");
  require(*pairs > 0, "no station pair reached both shoulders");
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
  double worstY = 0.0, worstUv = 0.0;
  requireBaselineSurface(host,dense,&worstY,&worstUv);
  double seam = 0.0;
  requireNoSeams(host,&seam);
  size_t pairs = 0;
  requireBorderRule(host,&pairs);

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
  std::printf("%s: %zu dense -> %zu list (%.3fx) -> %zu strip vertices in %zu "
              "chunks, %zu GS primitives (%zu degenerate), %zu station pairs; "
              "worst dY %.6f, worst dUV %.6f (%.2f texel at 128), "
              "worst seam %.6f; twins agree\n",
              name,dense.size(),host.size(),
              dense.empty()?0.0:(double)host.size()/(double)dense.size(),
              hostStrip.size(),chunkSizes.size(),prims,degen,pairs,
              worstY,worstUv,worstUv*128.0,seam);
  return host.size();
}
// The district's own ground: a triangulated heightfield on a FOUR-unit grid,
// sampled by a road every 0.5 units across and every 1.0 along. The diagonal
// is the renderer's (10 -> 01), the one Viewport::terrainHeight matches. This
// is the only fixture in this file where the lateral merge can fire at all -
// the analytic surfaces above are curved everywhere and have no coplanar runs
// to find - so it is the fixture that stands for the Motor District.
static float gridHeight(float x, float z) {
  const float cell = 4.0f;
  auto corner = [](int a, int b) {
    // Deterministic, and deliberately not smooth: neighbouring cells must have
    // genuinely different slopes or a merge would be trivially available.
    const int h = (a * 73856093) ^ (b * 19349663);
    return (float)((h >> 8) & 31) * 0.08f;
  };
  const float gx = x / cell + 64.0f, gz = z / cell + 64.0f;
  const int ix = (int)std::floor(gx), iz = (int)std::floor(gz);
  const float fx = gx - (float)ix, fz = gz - (float)iz;
  if (fx + fz <= 1.0f)
    return corner(ix,iz) + fx*(corner(ix+1,iz)-corner(ix,iz))
                         + fz*(corner(ix,iz+1)-corner(ix,iz));
  return corner(ix+1,iz+1) + (1.0f-fz)*(corner(ix+1,iz)-corner(ix+1,iz+1))
                           + (1.0f-fx)*(corner(ix,iz+1)-corner(ix+1,iz+1));
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
  // This used to read `curved == 9240` - "a curved plane must keep its dense
  // UV mapping" - and it was the rule that refused every merge on a bend, so
  // a district of curved splines got nothing from the lateral reduction. The
  // rule is now a BUDGET rather than a veto: the surface stays exact (checked
  // inside check(), against the float noise floor) and the UV may drift by up
  // to kSpanShear's worth, which check() measures and prints. A curved plane
  // is the fixture that proves the budget is live, so it must MERGE.
  require(curved<9240,"a curved plane must merge within the shear budget");
  check("saddle",{0,0,0,20,15,40,35,30},11,[](float x,float z){return .003f*x*z;});
  check("flat-to-crest",{0,0,0,20,15,40,35,30},11,[](float x,float z){return z<20?0.f:.01f*(z-20)*(z-20)+.02f*x;});
  check("minimum width",straight,0,[](float,float){return 0.f;});
  // The heightfield pair. The straight road's rows are parallel, so its spans
  // are parallelograms and the merge is limited only by the ground; the curved
  // one's rows converge, so the shear budget refuses most of it. Both must
  // keep the dense surface, the dense UVs and a seamless join - that is what
  // the three checks above assert, case by case - and BOTH must beat the
  // dense reference, or the district's roads would not have got cheaper.
  const auto fieldStraight=check("heightfield straight",{0,0,0,60},13,gridHeight);
  require(fieldStraight < 60u*26u*6u,"the heightfield straight must merge laterally");
  const auto fieldCurved=check("heightfield curve",{0,0,0,20,15,40,35,30},11,gridHeight);
  require(fieldCurved > 0,"the heightfield curve must still build");
  // A crest on the heightfield: the ground folds under the road, and the merge
  // has to stop at the fold rather than bridge it.
  check("heightfield crest",{-20,0,0,0,20,0},13,
        [](float x,float z){return gridHeight(x,z)+(x<0?0.f:0.05f*x);});
  std::vector<roadgen::Junction> junctions;
  roadgen::findJunctions({-10,0,10,0},6,{0,-10,0,10},8,junctions);
  require(junctions.size()==1,"perpendicular roads must create one junction");
  std::vector<roadgen::Vertex> junctionMesh;
  roadgen::tessellateJunction(junctions[0],gridHeight,junctionMesh);
  require(junctionMesh.size()==12,"one junction must be four triangles");
  for(const auto& v:junctionMesh)
    require(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&
            std::isfinite(v.u)&&std::isfinite(v.v),
            "junction geometry contains a non-finite value");
  roadgen::findJunctions({-10,0,10,0},6,{-10,1,10,1},6,junctions);
  require(junctions.empty(),"near-parallel roads must not create a junction");
  std::printf("junctions: perpendicular=1 vertices=12; near-parallel=0\n");
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='tyrax-roads-') as tmp:
    source = Path(tmp)/'oracle.cpp'
    dense_source = Path(tmp)/'roadgen_dense.cpp'
    dense_header = Path(tmp)/'roadgen_dense.hpp'
    binary = Path(tmp)/'oracle.exe'
    dense_header.write_text(roadgen_header.replace('namespace roadgen', 'namespace roadgen_dense'))
    baseline_merge = 'while (j1 < crossSteps && spanIsExact(rows, i, j0, j1 + 1)) ++j1;'
    if roadgen_source.count(baseline_merge) != 1:
        raise RuntimeError('road lateral merge changed; update the oracle deliberately')
    # The reference is the oldest shipped behavior: horizontal spans collapse,
    # every non-flat span stays fully dense. Deleting the greedy extension is
    # exactly that - `cuts` then holds every lateral sample. The candidate may
    # additionally merge its proven coplanar/affine runs, and the checks below
    # are what say the merge kept the surface, the UVs and the seams.
    dense_source.write_text(roadgen_source.replace('#include "roadgen.hpp"', '#include "roadgen_dense.hpp"')
                            .replace('namespace roadgen', 'namespace roadgen_dense')
                            .replace(baseline_merge, ''))
    source.write_text(stub + '#include "roadgen_dense.hpp"\n' + runtime + test)
    subprocess.run(['g++','-std=c++20','-O2','-static','-I',str(root/'src'),'-I',tmp,str(source),
                    str(root/'src/roadgen.cpp'),str(dense_source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
