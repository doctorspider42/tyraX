#include "objparser.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace objparser {

namespace {

struct Material {
    float kd[3] = {1.0f, 1.0f, 1.0f};
    std::string texture;  // map_Kd, relative to the .obj directory
};

// Parses newmtl/Kd/map_Kd from one .mtl file. Texture paths keep any relative
// subdirectories (normalized to forward slashes); options of map_Kd (rare
// "-s 1 1 tex.png" forms) are skipped by taking the last token.
void parseMtl(const std::filesystem::path& path,
              std::map<std::string, Material>& materials) {
    std::ifstream f(path);
    if (!f) return;

    std::string line, current;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "newmtl") {
            ss >> current;
            materials.emplace(current, Material{});
        } else if (tag == "Kd" && !current.empty()) {
            Material& m = materials[current];
            ss >> m.kd[0] >> m.kd[1] >> m.kd[2];
        } else if (tag == "map_Kd" && !current.empty()) {
            std::string tok, last;
            while (ss >> tok) last = tok;
            for (char& c : last)
                if (c == '\\') c = '/';
            materials[current].texture = last;
        }
    }
}

}  // namespace

bool load(const std::string& path, Model& out) {
    std::ifstream f(path);
    if (!f) return false;

    out = Model{};
    const std::filesystem::path dir = std::filesystem::path(path).parent_path();

    std::vector<float> positions;  // x,y,z triples
    std::vector<float> texcoords;  // u,v pairs
    std::map<std::string, Material> materials;

    // Implicit material library: a sibling .mtl named like the .obj is loaded
    // even without a mtllib line (many exporters rely on that convention).
    // Explicit mtllib files parse later and win on name clashes.
    {
        const std::string sibling =
            std::filesystem::path(path).stem().string() + ".mtl";
        std::error_code ec;
        if (std::filesystem::exists(dir / sibling, ec)) {
            parseMtl(dir / sibling, materials);
            out.mtlLibs.push_back(sibling);
        }
    }

    // submesh lookup by material name; "" = the default white submesh
    std::map<std::string, int> submeshIndex;
    int current = -1;
    auto submeshFor = [&](const std::string& matName) {
        auto it = submeshIndex.find(matName);
        if (it != submeshIndex.end()) return it->second;
        Submesh s;
        s.material = matName;
        if (auto m = materials.find(matName); m != materials.end()) {
            s.kd[0] = m->second.kd[0];
            s.kd[1] = m->second.kd[1];
            s.kd[2] = m->second.kd[2];
            s.texture = m->second.texture;
        }
        out.submeshes.push_back(std::move(s));
        submeshIndex[matName] = (int)out.submeshes.size() - 1;
        return (int)out.submeshes.size() - 1;
    };

    auto vertexAt = [&](int objIndex, float* xyz) {
        // obj indices are 1-based; negative = relative to the end
        const int count = (int)positions.size() / 3;
        int i = objIndex > 0 ? objIndex - 1 : count + objIndex;
        if (i < 0 || i >= count) return false;
        xyz[0] = positions[i * 3];
        xyz[1] = positions[i * 3 + 1];
        xyz[2] = positions[i * 3 + 2];
        return true;
    };
    auto uvAt = [&](int objIndex, float* uv) {
        const int count = (int)texcoords.size() / 2;
        int i = objIndex > 0 ? objIndex - 1 : count + objIndex;
        if (objIndex == 0 || i < 0 || i >= count) {
            uv[0] = uv[1] = 0.0f;
            return;
        }
        uv[0] = texcoords[i * 2];
        uv[1] = 1.0f - texcoords[i * 2 + 1];  // flip to image space (like Tyra)
    };

    bool anyVertex = false;
    auto grow = [&](const float* p) {
        if (!anyVertex) {
            anyVertex = true;
            for (int i = 0; i < 3; ++i) out.min[i] = out.max[i] = p[i];
            return;
        }
        for (int i = 0; i < 3; ++i) {
            if (p[i] < out.min[i]) out.min[i] = p[i];
            if (p[i] > out.max[i]) out.max[i] = p[i];
        }
    };

    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 2) continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "v") {
            float x = 0, y = 0, z = 0;
            ss >> x >> y >> z;
            positions.push_back(x);
            positions.push_back(y);
            positions.push_back(z);
        } else if (tag == "vt") {
            float u = 0, v = 0;
            ss >> u >> v;
            texcoords.push_back(u);
            texcoords.push_back(v);
        } else if (tag == "mtllib") {
            std::string name;
            while (ss >> name) {
                out.mtlLibs.push_back(name);
                parseMtl(dir / name, materials);
            }
        } else if (tag == "usemtl") {
            std::string name;
            ss >> name;
            current = submeshFor(name);
        } else if (tag == "f") {
            // face vertex tokens: "7", "7/1", "7//2", "7/1/2", negatives
            std::vector<int> vIdx, tIdx;
            std::string part;
            while (ss >> part) {
                vIdx.push_back(std::atoi(part.c_str()));
                const size_t slash = part.find('/');
                int t = 0;
                if (slash != std::string::npos && slash + 1 < part.size() &&
                    part[slash + 1] != '/')
                    t = std::atoi(part.c_str() + slash + 1);
                tIdx.push_back(t);
            }

            if (current < 0) current = submeshFor("");
            std::vector<float>& outVerts = out.submeshes[current].verts;

            for (size_t k = 2; k < vIdx.size(); ++k) {
                float a[3], b[3], c[3];
                if (!vertexAt(vIdx[0], a) || !vertexAt(vIdx[k - 1], b) ||
                    !vertexAt(vIdx[k], c))
                    continue;
                float uva[2], uvb[2], uvc[2];
                uvAt(tIdx[0], uva);
                uvAt(tIdx[k - 1], uvb);
                uvAt(tIdx[k], uvc);

                // face normal
                const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
                const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
                float nx = uy * vz - uz * vy;
                float ny = uz * vx - ux * vz;
                float nz = ux * vy - uy * vx;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-8f) nx /= len, ny /= len, nz /= len;
                else nx = 0, ny = 1, nz = 0;

                const float* pts[3] = {a, b, c};
                const float* uvs[3] = {uva, uvb, uvc};
                for (int i = 0; i < 3; ++i) {
                    grow(pts[i]);
                    outVerts.push_back(pts[i][0]);
                    outVerts.push_back(pts[i][1]);
                    outVerts.push_back(pts[i][2]);
                    outVerts.push_back(nx);
                    outVerts.push_back(ny);
                    outVerts.push_back(nz);
                    outVerts.push_back(uvs[i][0]);
                    outVerts.push_back(uvs[i][1]);
                }
            }
        }
    }

    // drop submeshes that got no faces (usemtl with nothing after it)
    for (size_t i = out.submeshes.size(); i-- > 0;)
        if (out.submeshes[i].verts.empty())
            out.submeshes.erase(out.submeshes.begin() + i);

    return !out.submeshes.empty();
}

bool load(const std::string& path, std::vector<float>& out) {
    Model model;
    if (!load(path, model)) return false;
    out.clear();
    for (const Submesh& s : model.submeshes)
        out.insert(out.end(), s.verts.begin(), s.verts.end());
    return !out.empty();
}

}  // namespace objparser
