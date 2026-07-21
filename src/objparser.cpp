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
    std::string texture;  // map_Kd, relative to its .mtl's directory
    float scale[2] = {1.0f, 1.0f};  // map_Kd -s (u, v); UV multiplier
    std::string refl;          // refl sphere map, relative to the .mtl ("" = none)
    float reflStrength = 0.0f;  // refl -mm gain operand, 0..1
    bool reflRounded = false;   // refl -rounded: centroid-radial env normals
};

// Parses the tokens after "map_Kd" into a texture filename (the last token)
// and the optional "-s <u> [v] [w]" scale option. Other map options (-o, -bm,
// -clamp, ...) are ignored; the filename is always the trailing token.
static void parseMapKd(std::istringstream& ss, std::string& outTexture, float outScale[2]) {
    std::vector<std::string> toks;
    for (std::string t; ss >> t;) toks.push_back(t);
    if (toks.empty()) return;
    outTexture = toks.back();
    for (char& c : outTexture)
        if (c == '\\') c = '/';
    // -s takes up to 3 floats; never consume the trailing filename token.
    for (size_t i = 0; i + 1 < toks.size(); ++i) {
        if (toks[i] != "-s") continue;
        for (int a = 0; a < 2 && i + 1 + (size_t)a < toks.size() - 1; ++a) {
            char* end = nullptr;
            const float v = std::strtof(toks[i + 1 + a].c_str(), &end);
            if (end == toks[i + 1 + a].c_str()) break;  // not a number
            outScale[a] = v;
        }
        break;
    }
}

// Parses newmtl/Kd/map_Kd from one .mtl file. Texture paths keep any relative
// subdirectories (normalized to forward slashes); the map_Kd "-s <u> <v>"
// scale option (a UV multiplier) is read, other options are ignored.
// order (optional) records material names in file order.
bool parseMtl(const std::filesystem::path& path,
              std::map<std::string, Material>& materials,
              std::vector<std::string>* order = nullptr) {
    std::ifstream f(path);
    if (!f) return false;

    std::string line, current;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "newmtl") {
            ss >> current;
            if (materials.emplace(current, Material{}).second && order)
                order->push_back(current);
        } else if (tag == "Kd" && !current.empty()) {
            Material& m = materials[current];
            ss >> m.kd[0] >> m.kd[1] >> m.kd[2];
        } else if (tag == "map_Kd" && !current.empty()) {
            Material& m = materials[current];
            parseMapKd(ss, m.texture, m.scale);
        } else if (tag == "refl" && !current.empty()) {
            // Spherical environment map:
            //   refl -type sphere -mm 0 <strength> [-rounded] <file>.
            // Filename = last token; -mm's gain operand is the strength;
            // -rounded switches the env pass to centroid-radial normals.
            Material& m = materials[current];
            std::vector<std::string> toks;
            for (std::string t; ss >> t;) toks.push_back(t);
            if (toks.empty()) continue;
            m.refl = toks.back();
            for (char& c : m.refl)
                if (c == '\\') c = '/';
            for (size_t i = 0; i + 2 < toks.size(); ++i) {
                if (toks[i] != "-mm") continue;
                m.reflStrength = std::strtof(toks[i + 2].c_str(), nullptr);
                break;
            }
            for (size_t i = 0; i + 1 < toks.size(); ++i)
                if (toks[i] == "-rounded") m.reflRounded = true;
            if (m.reflStrength <= 0.0f && !m.refl.empty())
                m.reflStrength = 0.5f;  // refl without -mm: sensible default
        }
    }
    return true;
}

}  // namespace

bool loadMtl(const std::string& path, std::vector<MtlMaterial>& out) {
    out.clear();
    std::map<std::string, Material> materials;
    std::vector<std::string> order;
    if (!parseMtl(path, materials, &order)) return false;
    for (const std::string& name : order) {
        MtlMaterial m;
        m.name = name;
        m.texture = materials[name].texture;
        m.kd[0] = materials[name].kd[0];
        m.kd[1] = materials[name].kd[1];
        m.kd[2] = materials[name].kd[2];
        m.scale[0] = materials[name].scale[0];
        m.scale[1] = materials[name].scale[1];
        m.refl = materials[name].refl;
        m.reflStrength = materials[name].reflStrength;
        m.reflRounded = materials[name].reflRounded;
        out.push_back(std::move(m));
    }
    return !out.empty();
}

bool load(const std::string& path, Model& out, const std::string& overrideMtl) {
    std::ifstream f(path);
    if (!f) return false;

    out = Model{};
    const std::filesystem::path dir = std::filesystem::path(path).parent_path();

    std::vector<float> positions;  // x,y,z triples
    std::vector<float> texcoords;  // u,v pairs
    std::map<std::string, Material> materials;

    // A material override replaces the model's own libraries entirely -
    // usemtl names resolve against it (universal .mtl shared by many models).
    if (!overrideMtl.empty()) {
        parseMtl(overrideMtl, materials);
        out.mtlLibs.push_back(overrideMtl);
    } else {
        // Implicit material library: a sibling .mtl named like the .obj is
        // loaded even without a mtllib line (many exporters rely on that
        // convention). Explicit mtllib files parse later and win on clashes.
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
            s.refl = m->second.refl;
            s.reflStrength = m->second.reflStrength;
            s.reflRounded = m->second.reflRounded;
        }
        out.submeshes.push_back(std::move(s));
        submeshIndex[matName] = (int)out.submeshes.size() - 1;
        return (int)out.submeshes.size() - 1;
    };

    auto vertexAt = [&](int objIndex, float* xyz, int& resolved) {
        // obj indices are 1-based; negative = relative to the end
        const int count = (int)positions.size() / 3;
        int i = objIndex > 0 ? objIndex - 1 : count + objIndex;
        if (i < 0 || i >= count) return false;
        xyz[0] = positions[i * 3];
        xyz[1] = positions[i * 3 + 1];
        xyz[2] = positions[i * 3 + 2];
        resolved = i;
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
        } else if (tag == "mtllib" && overrideMtl.empty()) {
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
            std::vector<int>& outPosIdx = out.submeshes[current].posIdx;

            for (size_t k = 2; k < vIdx.size(); ++k) {
                float a[3], b[3], c[3];
                int ia, ib, ic;
                if (!vertexAt(vIdx[0], a, ia) || !vertexAt(vIdx[k - 1], b, ib) ||
                    !vertexAt(vIdx[k], c, ic))
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
                const int ids[3] = {ia, ib, ic};
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
                    outPosIdx.push_back(ids[i]);
                }
            }
        }
    }

    // drop submeshes that got no faces (usemtl with nothing after it)
    for (size_t i = out.submeshes.size(); i-- > 0;)
        if (out.submeshes[i].verts.empty())
            out.submeshes.erase(out.submeshes.begin() + i);

    out.positionCount = (int)positions.size() / 3;
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
