#include "mhdata.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "json.hpp"

namespace mhdata {

namespace {

bool readFile(const std::string& path, std::string& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "Could not open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Splits an OBJ face corner ("12", "12/34", "12/34/56") into its position and
// uv index. MakeHuman writes v/vt; the third field is accepted and ignored.
void parseCorner(const char* s, const char* end, int& v, int& t) {
    v = std::strtol(s, nullptr, 10);
    t = 0;
    const char* slash = (const char*)std::memchr(s, '/', end - s);
    if (slash && slash + 1 < end && slash[1] != '/') t = std::strtol(slash + 1, nullptr, 10);
}

}  // namespace

int BaseMesh::groupIndex(const std::string& name) const {
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i] == name) return (int)i;
    return -1;
}

const Skeleton::Bone* Skeleton::find(const std::string& name) const {
    for (const Bone& b : bones)
        if (b.name == name) return &b;
    return nullptr;
}

bool loadBaseMesh(const std::string& path, BaseMesh& out, std::string& error) {
    std::ifstream f(path);
    if (!f) {
        error = "Could not open " + path;
        return false;
    }

    out = BaseMesh();
    int group = -1;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 2 || line[0] == '#') continue;
        if (line[0] == 'v' && line[1] == ' ') {
            float x = 0, y = 0, z = 0;
            if (std::sscanf(line.c_str() + 2, "%f %f %f", &x, &y, &z) == 3) {
                out.pos.push_back(x);
                out.pos.push_back(y);
                out.pos.push_back(z);
            }
        } else if (line[0] == 'v' && line[1] == 't') {
            float u = 0, v = 0;
            if (std::sscanf(line.c_str() + 3, "%f %f", &u, &v) == 2) {
                out.uv.push_back(u);
                out.uv.push_back(v);
            }
        } else if (line[0] == 'g' && line[1] == ' ') {
            std::string name = line.substr(2);
            while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
            group = out.groupIndex(name);
            if (group < 0) {
                group = (int)out.groups.size();
                out.groups.push_back(name);
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            BaseMesh::Face face;
            face.n = 0;
            const char* p = line.c_str() + 2;
            const char* end = line.c_str() + line.size();
            while (p < end && face.n < 4) {
                while (p < end && *p == ' ') ++p;
                if (p >= end || *p == '\r') break;
                const char* tokEnd = p;
                while (tokEnd < end && *tokEnd != ' ' && *tokEnd != '\r') ++tokEnd;
                int v = 0, t = 0;
                parseCorner(p, tokEnd, v, t);
                face.v[face.n] = v - 1;  // OBJ indices are 1-based
                face.t[face.n] = t - 1;
                ++face.n;
                p = tokEnd;
            }
            if (face.n >= 3) {
                out.faces.push_back(face);
                out.faceGroup.push_back(group);
            }
        }
    }

    if (out.pos.empty() || out.faces.empty()) {
        error = path + ": no geometry (expected an OBJ with v/f lines)";
        return false;
    }
    return true;
}

bool loadTarget(const std::string& path, Target& out, std::string& error) {
    std::ifstream f(path);
    if (!f) {
        error = "Could not open " + path;
        return false;
    }

    out = Target();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        int idx = 0;
        float dx = 0, dy = 0, dz = 0;
        // The values are written without a leading zero (".011 -.018"), which
        // sscanf's %f handles.
        if (std::sscanf(line.c_str(), "%d %f %f %f", &idx, &dx, &dy, &dz) != 4) continue;
        out.index.push_back(idx);
        out.delta.push_back(dx);
        out.delta.push_back(dy);
        out.delta.push_back(dz);
    }
    // An all-zero target is legal upstream (the "average" combinations are
    // header-only files), so an empty result is not an error.
    return true;
}

void applyTarget(const Target& t, float weight, std::vector<float>& pos) {
    if (weight == 0.0f) return;
    const int vertCount = (int)pos.size() / 3;
    for (size_t i = 0; i < t.index.size(); ++i) {
        const int v = t.index[i];
        if (v < 0 || v >= vertCount) continue;
        pos[v * 3 + 0] += t.delta[i * 3 + 0] * weight;
        pos[v * 3 + 1] += t.delta[i * 3 + 1] * weight;
        pos[v * 3 + 2] += t.delta[i * 3 + 2] * weight;
    }
}

bool loadProxy(const std::string& path, Proxy& out, std::string& error) {
    std::ifstream f(path);
    if (!f) {
        error = "Could not open " + path;
        return false;
    }

    out = Proxy();
    bool inVerts = false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        // Data rows are right-aligned numbers, so a row can start with either
        // a space or a digit - the keyword test is "starts with a letter".
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        const bool keyword = std::isalpha((unsigned char)line[s]) != 0;

        if (keyword) {
            std::istringstream ls(line);
            std::string key;
            ls >> key;
            inVerts = false;
            if (key == "verts") {
                inVerts = true;
            } else if (key == "obj_file") {
                std::getline(ls >> std::ws, out.objFile);
                while (!out.objFile.empty() &&
                       (out.objFile.back() == '\r' || out.objFile.back() == ' '))
                    out.objFile.pop_back();
            } else if (key == "name") {
                std::getline(ls >> std::ws, out.name);
                while (!out.name.empty() && (out.name.back() == '\r' || out.name.back() == ' '))
                    out.name.pop_back();
            } else if (key == "x_scale" || key == "y_scale" || key == "z_scale") {
                const int axis = key[0] == 'x' ? 0 : (key[0] == 'y' ? 1 : 2);
                int a = 0, b = 0;
                float dist = 1.0f;
                if (ls >> a >> b >> dist) {
                    out.scaleVert[axis][0] = a;
                    out.scaleVert[axis][1] = b;
                    out.scaleDist[axis] = dist != 0.0f ? dist : 1.0f;
                }
            }
            continue;
        }

        if (!inVerts) continue;
        int v0 = 0, v1 = 0, v2 = 0;
        float w0 = 0, w1 = 0, w2 = 0, ox = 0, oy = 0, oz = 0;
        int n = std::sscanf(line.c_str(), "%d %d %d %f %f %f %f %f %f", &v0, &v1, &v2, &w0, &w1,
                            &w2, &ox, &oy, &oz);
        if (n == 1) {
            // The short form: this proxy vertex sits exactly on one base
            // vertex. proxy741 has one of these, and dropping it shifts every
            // later vertex against the .obj's face indices - the mesh comes
            // out subtly scrambled rather than obviously broken.
            v1 = v2 = v0;
            w0 = 1.0f;
            w1 = w2 = 0.0f;
            ox = oy = oz = 0.0f;
            n = 9;
        }
        if (n < 6) continue;
        out.ref.push_back(v0);
        out.ref.push_back(v1);
        out.ref.push_back(v2);
        out.weight.push_back(w0);
        out.weight.push_back(w1);
        out.weight.push_back(w2);
        out.offset.push_back(n >= 9 ? ox : 0.0f);
        out.offset.push_back(n >= 9 ? oy : 0.0f);
        out.offset.push_back(n >= 9 ? oz : 0.0f);
    }

    if (out.ref.empty()) {
        error = path + ": no vertex bindings (expected a `verts` block)";
        return false;
    }
    return true;
}

std::vector<float> fitProxy(const Proxy& proxy, const std::vector<float>& basePos) {
    const int baseVerts = (int)basePos.size() / 3;
    const int n = proxy.vertCount();
    std::vector<float> out((size_t)n * 3, 0.0f);

    // The offsets are stored in units of three reference distances measured on
    // the base mesh, so they stretch with the body instead of staying absolute.
    float scale[3] = {1.0f, 1.0f, 1.0f};
    for (int a = 0; a < 3; ++a) {
        const int i = proxy.scaleVert[a][0], j = proxy.scaleVert[a][1];
        if (i < 0 || j < 0 || i >= baseVerts || j >= baseVerts) continue;
        const float d = basePos[i * 3 + a] - basePos[j * 3 + a];
        scale[a] = (d < 0 ? -d : d) / proxy.scaleDist[a];
    }

    for (int i = 0; i < n; ++i) {
        float p[3] = {0, 0, 0};
        for (int k = 0; k < 3; ++k) {
            const int v = proxy.ref[i * 3 + k];
            if (v < 0 || v >= baseVerts) continue;
            const float w = proxy.weight[i * 3 + k];
            p[0] += basePos[v * 3 + 0] * w;
            p[1] += basePos[v * 3 + 1] * w;
            p[2] += basePos[v * 3 + 2] * w;
        }
        out[i * 3 + 0] = p[0] + proxy.offset[i * 3 + 0] * scale[0];
        out[i * 3 + 1] = p[1] + proxy.offset[i * 3 + 1] * scale[1];
        out[i * 3 + 2] = p[2] + proxy.offset[i * 3 + 2] * scale[2];
    }
    return out;
}

bool loadSkeleton(const std::string& path, Skeleton& out, std::string& error) {
    std::string src;
    if (!readFile(path, src, error)) return false;
    json::Value root;
    if (!json::parse(src, root)) {
        error = path + ": malformed JSON";
        return false;
    }

    out = Skeleton();
    if (const json::Value* bones = root.find("bones")) {
        for (const auto& [name, v] : bones->obj) {
            Skeleton::Bone b;
            b.name = name;
            if (const json::Value* p = v.find("parent")) b.parent = p->stringOr("");
            if (const json::Value* h = v.find("head")) b.head = h->stringOr("");
            if (const json::Value* t = v.find("tail")) b.tail = t->stringOr("");
            out.bones.push_back(std::move(b));
        }
    }
    if (const json::Value* joints = root.find("joints")) {
        for (const auto& [name, v] : joints->obj) {
            std::vector<int> idx;
            idx.reserve(v.arr.size());
            for (const json::Value& e : v.arr) idx.push_back((int)e.numberOr(-1));
            out.joints[name] = std::move(idx);
        }
    }

    if (out.bones.empty() || out.joints.empty()) {
        error = path + ": no bones/joints";
        return false;
    }
    return true;
}

bool jointPos(const Skeleton& skel, const std::string& joint,
              const std::vector<float>& basePos, float out[3]) {
    auto it = skel.joints.find(joint);
    if (it == skel.joints.end() || it->second.empty()) return false;
    const int vertCount = (int)basePos.size() / 3;
    double sum[3] = {0, 0, 0};
    int used = 0;
    for (int v : it->second) {
        if (v < 0 || v >= vertCount) continue;
        sum[0] += basePos[v * 3 + 0];
        sum[1] += basePos[v * 3 + 1];
        sum[2] += basePos[v * 3 + 2];
        ++used;
    }
    if (!used) return false;
    for (int k = 0; k < 3; ++k) out[k] = (float)(sum[k] / used);
    return true;
}

bool loadWeights(const std::string& path, Weights& out, std::string& error) {
    std::string src;
    if (!readFile(path, src, error)) return false;
    json::Value root;
    if (!json::parse(src, root)) {
        error = path + ": malformed JSON";
        return false;
    }

    out = Weights();
    const json::Value* weights = root.find("weights");
    if (!weights) {
        error = path + ": no `weights` object";
        return false;
    }
    for (const auto& [bone, list] : weights->obj) {
        std::vector<std::pair<int, float>> vw;
        vw.reserve(list.arr.size());
        for (const json::Value& e : list.arr) {
            if (e.arr.size() < 2) continue;
            vw.emplace_back((int)e.arr[0].numberOr(-1), (float)e.arr[1].numberOr(0.0));
        }
        if (!vw.empty()) out.bone[bone] = std::move(vw);
    }
    return true;
}

}  // namespace mhdata
