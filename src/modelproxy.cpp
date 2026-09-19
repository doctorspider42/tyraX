#include "modelproxy.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include "objparser.hpp"

namespace modelproxy {
namespace {
struct P { float x = 0, z = 0; };
float cross(const P& a, const P& b, const P& c) {
    return (b.x-a.x)*(c.z-a.z) - (b.z-a.z)*(c.x-a.x);
}
std::vector<P> hull(std::vector<P> p) {
    std::sort(p.begin(), p.end(), [](const P& a, const P& b) {
        return a.x < b.x || (a.x == b.x && a.z < b.z);
    });
    p.erase(std::unique(p.begin(), p.end(), [](const P& a, const P& b) {
        return std::fabs(a.x-b.x) < 1e-5f && std::fabs(a.z-b.z) < 1e-5f;
    }), p.end());
    if (p.size() < 3) return {};
    std::vector<P> h;
    for (const P& q : p) {
        while (h.size() >= 2 && cross(h[h.size()-2], h.back(), q) <= 1e-6f)
            h.pop_back();
        h.push_back(q);
    }
    const size_t lower = h.size();
    for (size_t i = p.size()-1; i-- > 0;) {
        const P& q = p[i];
        while (h.size() > lower && cross(h[h.size()-2], h.back(), q) <= 1e-6f)
            h.pop_back();
        h.push_back(q);
    }
    h.pop_back();
    return h;
}
std::string rel(const std::filesystem::path& p, const std::filesystem::path& root) {
    return std::filesystem::relative(p, root).generic_string();
}
}  // namespace

bool bakeHull(const std::string& projectDir, const std::string& modelRel,
              const std::string& materialRel, const std::string& outputStem,
              std::string* outObjRel, float* outExtent, int* outTriangles,
              std::string* error) {
    namespace fs = std::filesystem;
    auto fail = [&](const std::string& s) { if (error) *error = s; return false; };
    const fs::path root(projectDir), source = root/modelRel;
    if (source.extension() != ".obj" && source.extension() != ".OBJ")
        return fail("Select a static OBJ model");
    objparser::Model model;
    if (!objparser::load(source.string(), model,
                         materialRel.empty() ? "" : (root/materialRel).string()))
        return fail("Cannot read model: " + modelRel);

    std::vector<P> points;
    double kd[3] = {0,0,0}, weight = 0;
    for (const auto& s : model.submeshes) {
        const double w = std::max<size_t>(1, s.verts.size()/8);
        for (int c=0;c<3;++c) kd[c] += s.kd[c]*w;
        weight += w;
        for (size_t i=0;i+7<s.verts.size();i+=8)
            points.push_back({s.verts[i], s.verts[i+2]});
    }
    std::vector<P> ring = hull(std::move(points));
    if (ring.size() < 3) return fail("The model has no usable XZ silhouette");
    const fs::path obj = root/(outputStem + ".obj");
    const fs::path mtl = root/(outputStem + ".mtl");
    std::error_code ec;
    fs::create_directories(obj.parent_path(), ec);
    if (ec) return fail("Cannot create proxy directory: " + ec.message());

    std::ostringstream os;
    os << std::setprecision(9) << "# TyraX convex footprint proxy\nmtllib "
       << mtl.filename().generic_string() << "\nusemtl proxy\n";
    for (const P& p : ring) os << "v " << p.x << ' ' << model.min[1] << ' ' << p.z << "\n";
    for (const P& p : ring) os << "v " << p.x << ' ' << model.max[1] << ' ' << p.z << "\n";
    const int n = (int)ring.size();
    // Side quads, then bottom/top fans. Tyra does not backface-cull, so one
    // winding is enough and avoids coplanar duplicates.
    for (int i=0;i<n;++i) {
        const int j=(i+1)%n;
        os << "f " << i+1 << ' ' << j+1 << ' ' << n+j+1 << "\n";
        os << "f " << i+1 << ' ' << n+j+1 << ' ' << n+i+1 << "\n";
    }
    for (int i=1;i+1<n;++i) {
        os << "f 1 " << i+2 << ' ' << i+1 << "\n";
        os << "f " << n+1 << ' ' << n+i+1 << ' ' << n+i+2 << "\n";
    }
    std::ofstream of(obj, std::ios::binary);
    if (!of) return fail("Cannot write proxy OBJ");
    of << os.str(); of.close();
    std::ofstream mf(mtl, std::ios::binary);
    if (!mf) return fail("Cannot write proxy MTL");
    mf << std::setprecision(6) << "newmtl proxy\nKd "
       << kd[0]/weight << ' ' << kd[1]/weight << ' ' << kd[2]/weight << "\n";
    mf.close();
    if (outObjRel) *outObjRel = rel(obj, root);
    if (outExtent) *outExtent = std::max({model.max[0]-model.min[0],
        model.max[1]-model.min[1], model.max[2]-model.min[2]});
    if (outTriangles) *outTriangles = 4*n-4;
    return true;
}
}  // namespace modelproxy
