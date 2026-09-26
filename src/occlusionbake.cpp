#include "occlusionbake.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <tuple>
#include <utility>

#include <stb_image.h>  // implementation lives in app.cpp

#include "objparser.hpp"

namespace occlusionbake {
namespace {

struct P { float x, y, z; };
struct Tri { P a, b, c; };

bool textureOpaque(const std::filesystem::path& path) {
    int w = 0, h = 0, n = 0;
    unsigned char* p = stbi_load(path.string().c_str(), &w, &h, &n, 4);
    if (!p) return false;  // unknown is not evidence of opacity
    bool opaque = true;
    for (int i = 0; i < w * h; ++i)
        if (p[i * 4 + 3] < 250) { opaque = false; break; }
    stbi_image_free(p);
    return opaque;
}

bool rayHitX(const P& p, const Tri& t, float& distance) {
    // Moller-Trumbore for direction (1,0,0). The caller jitters Y/Z so grid
    // samples almost never land exactly on an edge shared by two triangles.
    const P e1{t.b.x-t.a.x,t.b.y-t.a.y,t.b.z-t.a.z};
    const P e2{t.c.x-t.a.x,t.c.y-t.a.y,t.c.z-t.a.z};
    const P h{0.0f, -e2.z, e2.y};
    const float det = e1.x*h.x + e1.y*h.y + e1.z*h.z;
    if (std::fabs(det) < 1e-8f) return false;
    const float inv = 1.0f / det;
    const P s{p.x-t.a.x,p.y-t.a.y,p.z-t.a.z};
    const float u = inv * (s.x*h.x + s.y*h.y + s.z*h.z);
    if (u < 0.0f || u > 1.0f) return false;
    const P q{s.y*e1.z-s.z*e1.y, s.z*e1.x-s.x*e1.z,
              s.x*e1.y-s.y*e1.x};
    const float v = inv * q.x;
    if (v < 0.0f || u + v > 1.0f) return false;
    distance = inv * (e2.x*q.x + e2.y*q.y + e2.z*q.z);
    return distance > 1e-6f;
}

bool inside(const P& in, const std::vector<Tri>& tris, float jitter) {
    P p = in;
    p.y += jitter * 0.371f;
    p.z += jitter * 0.613f;
    int hits = 0;
    for (const Tri& t : tris) {
        float d = 0;
        if (rayHitX(p, t, d)) ++hits;
    }
    return (hits & 1) != 0;
}

P axisOrder(const P& p, int axis) {
    return axis == 0 ? p : axis == 1 ? P{p.y,p.z,p.x} : P{p.z,p.x,p.y};
}

bool closedByParity(const std::vector<Tri>& tris, const float* mn,
                    const float* mx) {
    // Geometric closure check, tolerant of T-junctions (common on optimized
    // building shells). A real hole produces an odd crossing count for rays
    // through it; a merely split wall/roof edge does not.
    for (int axis=0; axis<3; ++axis) {
        const int b=(axis+1)%3,c=(axis+2)%3;
        for(int ib=0;ib<11;++ib) for(int ic=0;ic<11;++ic){
            P p{0,0,0};
            float* q=&p.x;
            q[axis]=mn[axis]-(mx[axis]-mn[axis]+1.0f);
            q[b]=mn[b]+(ib+0.371f)/11.0f*(mx[b]-mn[b]);
            q[c]=mn[c]+(ic+0.613f)/11.0f*(mx[c]-mn[c]);
            const P rp=axisOrder(p,axis); int hits=0;
            for(const Tri& t:tris){
                const Tri rt{axisOrder(t.a,axis),axisOrder(t.b,axis),axisOrder(t.c,axis)};
                float d=0;if(rayHitX(rp,rt,d))++hits;
            }
            if(hits&1)return false;
        }
    }
    return true;
}

}  // namespace

Result build(const std::string& modelPath, const std::string& materialOverride) {
    namespace fs = std::filesystem;
    Result out;
    objparser::Model model;
    if (!objparser::load(modelPath, model, materialOverride)) {
        out.reason = "model could not be parsed";
        return out;
    }

    const fs::path objDir = fs::path(modelPath).parent_path();
    for (const auto& s : model.submeshes) {
        if (s.texture.empty()) continue;
        std::vector<fs::path> candidates;
        if (!materialOverride.empty())
            candidates.push_back(fs::path(materialOverride).parent_path()/s.texture);
        candidates.push_back(objDir/s.texture);
        for (const std::string& lib : model.mtlLibs) {
            fs::path lp(lib);
            if (!lp.is_absolute()) lp = objDir/lp;
            candidates.push_back(lp.parent_path()/s.texture);
        }
        bool found = false, opaque = false;
        std::error_code ec;
        for (const fs::path& p : candidates) {
            if (!fs::exists(p, ec)) continue;
            found = true;
            opaque = textureOpaque(p);
            break;
        }
        if (!found || !opaque) {
            out.reason = found ? "texture contains alpha" : "texture opacity is unknown";
            return out;
        }
    }

    std::vector<Tri> tris;
    std::map<std::pair<int,int>, int> edges;
    // OBJ exporters commonly duplicate `v` rows at UV/normal seams. Topology
    // is geometric here, so weld coincident positions before the manifold
    // test instead of declaring every facade corner an open edge.
    std::map<std::tuple<long long,long long,long long>,int> welded;
    const float weldScale = 100000.0f / std::max(1.0f,
        std::max(model.max[0]-model.min[0],
        std::max(model.max[1]-model.min[1],model.max[2]-model.min[2])));
    auto weldId=[&](const float* p){
        const auto key=std::make_tuple((long long)std::llround(p[0]*weldScale),
            (long long)std::llround(p[1]*weldScale),
            (long long)std::llround(p[2]*weldScale));
        auto [it,inserted]=welded.emplace(key,(int)welded.size());
        return it->second;
    };
    for (const auto& s : model.submeshes) {
        for (size_t i = 0; i + 2 < s.posIdx.size(); i += 3) {
            const float* a = &s.verts[(i+0)*8];
            const float* b = &s.verts[(i+1)*8];
            const float* c = &s.verts[(i+2)*8];
            int ids[3] = {weldId(a),weldId(b),weldId(c)};
            for (int e = 0; e < 3; ++e) {
                int a = ids[e], b = ids[(e+1)%3];
                if (a > b) std::swap(a,b);
                ++edges[{a,b}];
            }
            tris.push_back({{a[0],a[1],a[2]}, {b[0],b[1],b[2]},
                            {c[0],c[1],c[2]}});
        }
    }
    out.inputTriangles = (int)tris.size();
    int badEdges = 0;
    for (const auto& e : edges) if (e.second != 2) ++badEdges;
    if (badEdges && !closedByParity(tris, model.min, model.max)) {
        out.reason = "mesh is open or non-manifold (" +
                     std::to_string(badEdges) + " boundary edges)";
        return out;
    }

    const float ext[3] = {model.max[0]-model.min[0], model.max[1]-model.min[1],
                          model.max[2]-model.min[2]};
    const float longest = std::max(ext[0], std::max(ext[1], ext[2]));
    if (!(longest > 1e-5f)) { out.reason = "degenerate bounds"; return out; }
    int n[3];
    for (int a=0;a<3;++a)
        n[a] = std::max(3, std::min(18, (int)std::ceil(18.0f*ext[a]/longest)));
    const float step[3] = {ext[0]/n[0], ext[1]/n[1], ext[2]/n[2]};
    const int total=n[0]*n[1]*n[2];
    std::vector<unsigned char> solid(total,0), used(total,0);
    auto at=[&](int x,int y,int z){return (y*n[2]+z)*n[0]+x;};
    // A voxel is admitted only when its centre and eight inset corners are
    // inside. This is deliberately much stricter than centre-only filling.
    for(int y=0;y<n[1];++y) for(int z=0;z<n[2];++z) for(int x=0;x<n[0];++x){
        bool ok=true;
        for(int sy=-1;sy<=1&&ok;sy+=2) for(int sz=-1;sz<=1&&ok;sz+=2)
          for(int sx=-1;sx<=1&&ok;sx+=2){
            P p{model.min[0]+(x+0.5f+sx*0.42f)*step[0],
                model.min[1]+(y+0.5f+sy*0.42f)*step[1],
                model.min[2]+(z+0.5f+sz*0.42f)*step[2]};
            ok=inside(p,tris,longest*1e-5f);
          }
        if(ok){
          P c{model.min[0]+(x+0.5f)*step[0], model.min[1]+(y+0.5f)*step[1],
              model.min[2]+(z+0.5f)*step[2]};
          solid[at(x,y,z)]=inside(c,tris,-longest*1e-5f)?1:0;
        }
    }
    // One-cell erosion buys margin for numerical and authoring imperfections.
    std::vector<unsigned char> inner=solid;
    for(int y=0;y<n[1];++y) for(int z=0;z<n[2];++z) for(int x=0;x<n[0];++x){
      if(!solid[at(x,y,z)]) continue;
      const int d[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
      for(auto& q:d){int X=x+q[0],Y=y+q[1],Z=z+q[2];
        if(X<0||Y<0||Z<0||X>=n[0]||Y>=n[1]||Z>=n[2]||!solid[at(X,Y,Z)]){
          inner[at(x,y,z)]=0; break;
        }}
    }
    // Greedy exact union of surviving grid cells into a few boxes.
    for(int y=0;y<n[1];++y) for(int z=0;z<n[2];++z) for(int x=0;x<n[0];++x){
      if(!inner[at(x,y,z)]||used[at(x,y,z)]) continue;
      int xe=x+1,ze=z+1,ye=y+1;
      while(xe<n[0]&&inner[at(xe,y,z)]&&!used[at(xe,y,z)])++xe;
      bool grow=true;
      while(ze<n[2]&&grow){for(int X=x;X<xe;++X) if(!inner[at(X,y,ze)]||used[at(X,y,ze)])grow=false; if(grow)++ze;}
      grow=true;
      while(ye<n[1]&&grow){for(int Z=z;Z<ze&&grow;++Z)for(int X=x;X<xe;++X)if(!inner[at(X,ye,Z)]||used[at(X,ye,Z)]){grow=false;break;} if(grow)++ye;}
      for(int Y=y;Y<ye;++Y)for(int Z=z;Z<ze;++Z)for(int X=x;X<xe;++X)used[at(X,Y,Z)]=1;
      Box b;
      b.min[0]=model.min[0]+(x+0.08f)*step[0]; b.max[0]=model.min[0]+(xe-0.08f)*step[0];
      b.min[1]=model.min[1]+(y+0.08f)*step[1]; b.max[1]=model.min[1]+(ye-0.08f)*step[1];
      b.min[2]=model.min[2]+(z+0.08f)*step[2]; b.max[2]=model.min[2]+(ze-0.08f)*step[2];
      out.boxes.push_back(b);
    }
    if(out.boxes.empty()) out.reason="no safely eroded interior remained";
    if(out.boxes.size()>32){out.boxes.clear();out.reason="proxy exceeded 32 boxes";}
    return out;
}

}  // namespace occlusionbake
