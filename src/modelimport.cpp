#include "modelimport.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include "fbxparser.hpp"
#include "glbparser.hpp"
#include "meshlod.hpp"
#include "meshstrip.hpp"
#include "objparser.hpp"
#include "pngquant.hpp"
#include "stb_image.h"
#include "stb_image_write.h"
#include "ufbx.h"

namespace fs = std::filesystem;

namespace modelimport {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string extOf(const std::string& path) {
    return lower(fs::path(path).extension().string());
}

// The file-name part of a reference written on any OS ("C:\x\wood.jpg").
std::string refBaseName(std::string ref) {
    for (char& c : ref)
        if (c == '\\') c = '/';
    const size_t slash = ref.find_last_of('/');
    return slash == std::string::npos ? ref : ref.substr(slash + 1);
}

bool isImageExt(const std::string& e) {
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" ||
           e == ".bmp" || e == ".gif" || e == ".psd";
}

// Index of every image under a few folders, by lowercased file name and by
// lowercased stem - a pack's materials say "Texture/wood.jpg" or a path on
// the artist's machine, and the file usually sits a folder or two away,
// sometimes as .png where the .fbx says .jpg.
class TextureFinder {
public:
    explicit TextureFinder(const std::vector<std::string>& dirs) {
        for (const std::string& d : dirs) index(d);
    }
    std::string find(const std::string& ref) const {
        const std::string base = lower(refBaseName(ref));
        if (base.empty()) return "";
        if (auto it = byName_.find(base); it != byName_.end()) return it->second;
        if (auto it = byStem_.find(lower(fs::path(base).stem().string()));
            it != byStem_.end())
            return it->second;
        return "";
    }

private:
    void index(const std::string& dir) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) return;
        int budget = 20000;  // a pointed-at home folder must not hang the UI
        for (fs::recursive_directory_iterator it(
                 dir, fs::directory_options::skip_permission_denied, ec),
             end;
             it != end && budget > 0; it.increment(ec)) {
            if (ec) break;
            if (it.depth() > 4) {
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            --budget;
            const std::string e = lower(it->path().extension().string());
            if (!isImageExt(e)) continue;
            const std::string name = lower(it->path().filename().string());
            const std::string stem = lower(it->path().stem().string());
            byName_.emplace(name, it->path().string());
            // .png wins a stem collision - it needs no transcoding
            auto [st, inserted] = byStem_.emplace(stem, it->path().string());
            if (!inserted && e == ".png") st->second = it->path().string();
        }
    }
    std::unordered_map<std::string, std::string> byName_, byStem_;
};

void toVector(void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<unsigned char>*>(ctx);
    const unsigned char* p = static_cast<const unsigned char*>(data);
    v->insert(v->end(), p, p + size);
}

bool isPng(const std::vector<unsigned char>& d) {
    static const unsigned char magic[] = {0x89, 'P', 'N', 'G'};
    return d.size() >= 4 && std::memcmp(d.data(), magic, 4) == 0;
}

std::vector<unsigned char> readBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
}

// Bytes of any image the editor can decode -> a PNG Image with its preview.
// False when the format cannot be decoded (the editor's stb is PNG + JPEG).
bool makeImage(std::vector<unsigned char> bytes, const std::string& name,
               Image& out) {
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
                                              &w, &h, &comp, 4);
    if (!px) return false;
    out.name = name;
    out.w = w;
    out.h = h;
    if (isPng(bytes)) {
        out.png = std::move(bytes);
    } else {
        out.png.clear();
        stbi_write_png_to_func(toVector, &out.png, w, h, 4, px, w * 4);
    }
    const int longest = std::max(w, h);
    if (longest > kPreviewTex) {
        out.pw = std::max(1, w * kPreviewTex / longest);
        out.ph = std::max(1, h * kPreviewTex / longest);
        out.preview = pngquant::resizeRGBA(px, w, h, out.pw, out.ph);
    } else {
        out.pw = w;
        out.ph = h;
        out.preview.assign(px, px + (size_t)w * h * 4);
    }
    stbi_image_free(px);
    return true;
}

std::string uniqueImageName(const std::vector<Image>& images, std::string stem) {
    stem = safeName(stem);
    std::string name = stem + ".png";
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const Image& im : images) taken |= lower(im.name) == lower(name);
        if (!taken) return name;
        name = stem + "-" + std::to_string(n) + ".png";
    }
}

void grow(float mn[3], float mx[3], const float* p, bool& first) {
    for (int a = 0; a < 3; ++a) {
        if (first || p[a] < mn[a]) mn[a] = p[a];
        if (first || p[a] > mx[a]) mx[a] = p[a];
    }
    first = false;
}

void finishBounds(Source& src) {
    bool any = true;
    src.tris = 0;
    for (Object& o : src.objects) {
        bool first = true;
        o.tris = 0;
        for (const Piece& p : o.pieces) {
            o.tris += (int)(p.verts.size() / 24);
            for (size_t i = 0; i + 7 < p.verts.size(); i += 8) {
                grow(o.mn, o.mx, &p.verts[i], first);
                grow(src.mn, src.mx, &p.verts[i], any);
            }
        }
        src.tris += o.tris;
    }
}

// --- .obj ------------------------------------------------------------------

bool loadObj(const std::string& path, const TextureFinder& finder, Source& out,
             std::string& error) {
    objparser::Model m;
    if (!objparser::load(path, m, "", /*splitObjects=*/true)) {
        error = "not a readable Wavefront model (no triangles)";
        return false;
    }
    const fs::path dir = fs::path(path).parent_path();
    std::map<std::string, int> matIndex;
    std::map<std::string, int> imageByPath;
    std::map<std::string, int> objIndex;
    const std::string stem = fs::path(path).stem().string();

    for (const objparser::Submesh& s : m.submeshes) {
        int mi = -1;
        if (auto it = matIndex.find(s.material); it != matIndex.end()) {
            mi = it->second;
        } else {
            Material mat;
            mat.name = s.material.empty() ? "default" : s.material;
            for (int i = 0; i < 3; ++i) mat.kd[i] = s.kd[i], mat.ke[i] = s.ke[i];
            mat.glowRange = s.glowRange;
            mat.glowLight = s.glowLight;
            if (s.refl == "@sky") {
                mat.refl = "@sky";
                mat.reflStrength = s.reflStrength;
                mat.reflRounded = s.reflRounded;
            }
            if (!s.texture.empty()) {
                // relative to the .mtl that named it: try each library's folder
                std::string found;
                std::error_code ec;
                for (const std::string& lib : m.mtlLibs) {
                    const fs::path cand =
                        ((dir / lib).parent_path() / s.texture).lexically_normal();
                    if (fs::exists(cand, ec)) {
                        found = cand.string();
                        break;
                    }
                }
                if (found.empty() && fs::exists(dir / s.texture, ec))
                    found = (dir / s.texture).string();
                if (found.empty()) found = finder.find(s.texture);
                if (found.empty()) {
                    mat.missingTexture = refBaseName(s.texture);
                } else if (auto ii = imageByPath.find(found);
                           ii != imageByPath.end()) {
                    mat.image = ii->second;
                } else {
                    Image im;
                    if (makeImage(readBytes(found),
                                  uniqueImageName(out.images,
                                                  fs::path(found).stem().string()),
                                  im)) {
                        out.images.push_back(std::move(im));
                        mat.image = (int)out.images.size() - 1;
                    } else {
                        out.warnings.push_back("texture " +
                                               fs::path(found).filename().string() +
                                               " is not PNG/JPEG - skipped");
                        mat.missingTexture = fs::path(found).filename().string();
                    }
                    imageByPath[found] = mat.image;
                }
            }
            out.materials.push_back(mat);
            mi = (int)out.materials.size() - 1;
            matIndex[s.material] = mi;
        }
        const std::string objName = s.object.empty() ? stem : s.object;
        int oi;
        if (auto it = objIndex.find(objName); it != objIndex.end()) {
            oi = it->second;
        } else {
            Object o;
            o.name = objName;
            o.group = (int)out.groups.size();
            out.groups.push_back(objName);
            out.objects.push_back(std::move(o));
            oi = (int)out.objects.size() - 1;
            objIndex[objName] = oi;
        }
        Piece pc;
        pc.material = mi;
        pc.verts = s.verts;
        out.objects[(size_t)oi].pieces.push_back(std::move(pc));
    }
    return true;
}

// --- .glb (through the skeletal parse every animated path uses) -------------

struct M4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static M4 mul(const M4& a, const M4& b) {
        M4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
};

M4 localOf(const glbparser::SkelNode& n) {
    M4 r;
    if (n.hasMatrix) {
        std::memcpy(r.m, n.matrix, sizeof(r.m));
        return r;
    }
    const float x = n.r[0], y = n.r[1], z = n.r[2], w = n.r[3];
    r.m[0] = (1 - 2 * (y * y + z * z)) * n.s[0];
    r.m[1] = (2 * (x * y + z * w)) * n.s[0];
    r.m[2] = (2 * (x * z - y * w)) * n.s[0];
    r.m[4] = (2 * (x * y - z * w)) * n.s[1];
    r.m[5] = (1 - 2 * (x * x + z * z)) * n.s[1];
    r.m[6] = (2 * (y * z + x * w)) * n.s[1];
    r.m[8] = (2 * (x * z + y * w)) * n.s[2];
    r.m[9] = (2 * (y * z - x * w)) * n.s[2];
    r.m[10] = (1 - 2 * (x * x + y * y)) * n.s[2];
    r.m[12] = n.t[0];
    r.m[13] = n.t[1];
    r.m[14] = n.t[2];
    return r;
}

// Global bind transforms, resolved along the parent chain (glTF node order
// is arbitrary - the animmerge/vehbake trap).
std::vector<M4> globals(const glbparser::Skel& sk) {
    std::vector<M4> out(sk.nodes.size());
    std::vector<bool> done(sk.nodes.size(), false);
    std::vector<int> chain;
    for (size_t i = 0; i < sk.nodes.size(); ++i) {
        chain.clear();
        int cur = (int)i;
        while (cur >= 0 && !done[(size_t)cur]) {
            chain.push_back(cur);
            cur = sk.nodes[(size_t)cur].parent;
        }
        for (int k = (int)chain.size() - 1; k >= 0; --k) {
            const int n = chain[(size_t)k];
            const int p = sk.nodes[(size_t)n].parent;
            out[(size_t)n] = p >= 0 ? M4::mul(out[(size_t)p], localOf(sk.nodes[(size_t)n]))
                                    : localOf(sk.nodes[(size_t)n]);
            done[(size_t)n] = true;
        }
    }
    return out;
}

bool identityIbm(const float* m) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::fabs(m[c * 4 + r] - (c == r ? 1.0f : 0.0f)) > 1e-6f)
                return false;
    return true;
}

bool loadSkel(const std::string& path, const TextureFinder& finder, Source& out,
              std::string& error) {
    // .glb only (embedded images, no texture search needed - .fbx has its own
    // reader below).
    (void)finder;
    glbparser::Skel sk;
    if (!animimport::parseSkel(path, sk, error)) return false;
    for (const std::string& w : sk.warnings)
        if (w.find("not found") == std::string::npos &&
            w.find("transcoded") == std::string::npos)
            out.warnings.push_back(w);
    for (const glbparser::SkelClip& c : sk.clips)
        if (c.duration > 0.0f && !c.channels.empty()) out.clips.push_back(c.name);

    // images: the parser already holds PNG bytes
    std::vector<int> imageMap(sk.images.size(), -1);
    for (size_t i = 0; i < sk.images.size(); ++i) {
        Image im;
        const std::string stem = fs::path(sk.images[i].name).stem().string();
        if (makeImage(sk.images[i].png, uniqueImageName(out.images, stem), im)) {
            out.images.push_back(std::move(im));
            imageMap[i] = (int)out.images.size() - 1;
        }
    }

    const std::vector<M4> g = globals(sk);
    std::map<std::string, int> matIndex;
    std::map<int, int> objOfNode;  // owner node (-1 = skinned) -> object
    std::vector<int> ownerOfObject;
    const std::string stem = fs::path(path).stem().string();

    for (const glbparser::SkelPart& p : sk.parts) {
        if (p.vertexCount < 3) continue;
        int mi;
        if (auto it = matIndex.find(p.material); it != matIndex.end()) {
            mi = it->second;
        } else {
            Material mat;
            mat.name = p.material.empty() ? "default" : p.material;
            for (int i = 0; i < 3; ++i) mat.kd[i] = p.baseColor[i];
            if (p.image >= 0 && p.image < (int)imageMap.size())
                mat.image = imageMap[(size_t)p.image];
            out.materials.push_back(mat);
            mi = (int)out.materials.size() - 1;
            matIndex[p.material] = mi;
        }
        const bool haveSkin = p.joints.size() == (size_t)p.vertexCount * 4 &&
                              p.weights.size() == (size_t)p.vertexCount * 4;
        std::map<int, std::vector<float>> byOwner;
        for (int v0 = 0; v0 + 2 < p.vertexCount; v0 += 3) {
            float corner[3][8];
            int owner = -2;
            for (int k = 0; k < 3; ++k) {
                const int v = v0 + k;
                M4 m;
                int node = -1;
                if (haveSkin) {
                    const size_t ji = (size_t)v * 4;
                    const int j0 = p.joints[ji];
                    const bool rigid =
                        p.weights[ji] == 255 && j0 < (int)sk.palette.size() &&
                        identityIbm(sk.palette[(size_t)j0].ibm);
                    if (rigid) {
                        node = sk.palette[(size_t)j0].node;
                        m = g[(size_t)node];
                    } else {
                        std::memset(m.m, 0, sizeof(m.m));
                        for (int b = 0; b < 4; ++b) {
                            const int j = p.joints[ji + (size_t)b];
                            const float w = p.weights[ji + (size_t)b] / 255.0f;
                            if (w <= 0.0f || j >= (int)sk.palette.size()) continue;
                            M4 ibm;
                            std::memcpy(ibm.m, sk.palette[(size_t)j].ibm, sizeof(ibm.m));
                            const M4 jm =
                                M4::mul(g[(size_t)sk.palette[(size_t)j].node], ibm);
                            for (int e = 0; e < 16; ++e) m.m[e] += w * jm.m[e];
                        }
                        out.skinned = true;
                    }
                }
                if (k == 0) owner = node;
                const float* ps = &p.positions[(size_t)v * 3];
                const float* ns = &p.normals[(size_t)v * 3];
                float* c = corner[k];
                for (int r = 0; r < 3; ++r) {
                    c[r] = m.m[r] * ps[0] + m.m[4 + r] * ps[1] + m.m[8 + r] * ps[2] +
                           m.m[12 + r];
                    c[3 + r] = m.m[r] * ns[0] + m.m[4 + r] * ns[1] + m.m[8 + r] * ns[2];
                }
                const float len =
                    std::sqrt(c[3] * c[3] + c[4] * c[4] + c[5] * c[5]);
                if (len > 1e-8f) c[3] /= len, c[4] /= len, c[5] /= len;
                c[6] = p.uvs[(size_t)v * 2];
                c[7] = p.uvs[(size_t)v * 2 + 1];
            }
            std::vector<float>& dst = byOwner[owner];
            for (int k = 0; k < 3; ++k) dst.insert(dst.end(), corner[k], corner[k] + 8);
        }
        for (auto& [owner, verts] : byOwner) {
            int oi;
            if (auto it = objOfNode.find(owner); it != objOfNode.end()) {
                oi = it->second;
            } else {
                Object o;
                o.name = owner >= 0 && !sk.nodes[(size_t)owner].name.empty()
                             ? sk.nodes[(size_t)owner].name
                             : stem;
                out.objects.push_back(std::move(o));
                ownerOfObject.push_back(owner);
                oi = (int)out.objects.size() - 1;
                objOfNode[owner] = oi;
            }
            Piece pc;
            pc.material = mi;
            pc.verts = std::move(verts);
            out.objects[(size_t)oi].pieces.push_back(std::move(pc));
        }
    }

    // Top-level grouping: the level of the hierarchy where objects stop being
    // one wrapper. An exporter's lone scene root ("RootNode", Blender's
    // collection empty) is unwrapped as long as it owns no geometry itself.
    const size_t nn = sk.nodes.size();
    std::vector<char> owns(nn, 0), inUse(nn, 0);
    for (int owner : ownerOfObject)
        if (owner >= 0) owns[(size_t)owner] = 1;
    for (size_t i = 0; i < nn; ++i)
        if (owns[i])
            for (int cur = (int)i; cur >= 0; cur = sk.nodes[(size_t)cur].parent)
                inUse[(size_t)cur] = 1;
    std::vector<int> level;
    for (size_t i = 0; i < nn; ++i)
        if (inUse[i] && sk.nodes[i].parent < 0) level.push_back((int)i);
    while (level.size() == 1 && !owns[(size_t)level[0]]) {
        std::vector<int> next;
        for (size_t i = 0; i < nn; ++i)
            if (inUse[i] && sk.nodes[i].parent == level[0]) next.push_back((int)i);
        if (next.empty()) break;
        level = next;
    }
    std::set<int> levelSet(level.begin(), level.end());
    std::map<int, int> groupOfNode;
    for (size_t oi = 0; oi < out.objects.size(); ++oi) {
        int top = ownerOfObject[oi];
        if (top >= 0) {
            int cur = top;
            while (cur >= 0 && !levelSet.count(cur)) cur = sk.nodes[(size_t)cur].parent;
            if (cur >= 0) top = cur;
        }
        auto it = groupOfNode.find(top);
        if (it == groupOfNode.end()) {
            out.groups.push_back(top >= 0 && !sk.nodes[(size_t)top].name.empty()
                                     ? sk.nodes[(size_t)top].name
                                     : stem);
            it = groupOfNode.emplace(top, (int)out.groups.size() - 1).first;
        }
        out.objects[oi].group = it->second;
    }
    return true;
}

// --- .fbx (read straight off ufbx) -----------------------------------------
//
// NOT through the skeletal parse: that one gives every rigid mesh node a
// matrix-palette slot and the slot index is a byte, so a pack of 546 props in
// one file (exactly the file a split import exists for) loses every object
// past the 256th. Static geometry needs no palette at all - each mesh node's
// triangles go through its own geometry_to_world. The load options are
// fbxparser's (Y-up, meters, ADJUST_TRANSFORMS), so the static twin and the
// animated model of one file sit in the same space.
bool loadFbx(const std::string& path, const TextureFinder& finder, Source& out,
             std::string& error) {
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    opts.generate_missing_normals = true;
    ufbx_error err;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) {
        char buf[512];
        ufbx_format_error(buf, sizeof buf, &err);
        error = buf;
        return false;
    }
    const fs::path dir = fs::path(path).parent_path();
    out.skinned = scene->skin_deformers.count > 0;
    for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
        const ufbx_anim_stack* st = scene->anim_stacks.data[i];
        bool moves = false;
        for (size_t l = 0; l < st->layers.count; ++l)
            moves |= st->layers.data[l]->anim_values.count > 0;
        if (moves && st->time_end > st->time_begin)
            out.clips.push_back(std::string(st->name.data, st->name.length));
    }

    std::map<const ufbx_material*, int> matIndex;
    std::map<const ufbx_texture*, int> texIndex;
    auto materialOf = [&](const ufbx_material* m) -> int {
        if (auto it = matIndex.find(m); it != matIndex.end()) return it->second;
        Material mat;
        mat.name = m ? std::string(m->name.data, m->name.length) : "default";
        if (mat.name.empty()) mat.name = "default";
        if (m && m->pbr.base_color.has_value) {
            mat.kd[0] = (float)m->pbr.base_color.value_vec4.x;
            mat.kd[1] = (float)m->pbr.base_color.value_vec4.y;
            mat.kd[2] = (float)m->pbr.base_color.value_vec4.z;
        } else if (m && m->fbx.diffuse_color.has_value) {
            mat.kd[0] = (float)m->fbx.diffuse_color.value_vec3.x;
            mat.kd[1] = (float)m->fbx.diffuse_color.value_vec3.y;
            mat.kd[2] = (float)m->fbx.diffuse_color.value_vec3.z;
        }
        const ufbx_texture* tex = !m ? nullptr
                                  : m->pbr.base_color.texture ? m->pbr.base_color.texture
                                                              : m->fbx.diffuse_color.texture;
        if (tex) {
            // a texture multiplies the colour on the PS2 (MODULATE), and an
            // exporter's grey "diffuse" under a texture darkens it - Blender
            // writes 0.8; the texture is the colour
            mat.kd[0] = mat.kd[1] = mat.kd[2] = 1.0f;
            if (auto it = texIndex.find(tex); it != texIndex.end()) {
                mat.image = it->second;
            } else {
                const std::string rel(tex->filename.data, tex->filename.length);
                std::vector<unsigned char> bytes;
                std::string found;
                if (tex->content.size) {
                    const unsigned char* p =
                        static_cast<const unsigned char*>(tex->content.data);
                    bytes.assign(p, p + tex->content.size);
                } else {
                    std::error_code ec;
                    const std::string abs(tex->absolute_filename.data,
                                          tex->absolute_filename.length);
                    if (!abs.empty() && fs::exists(abs, ec))
                        found = abs;
                    else if (fs::exists(dir / refBaseName(rel), ec))
                        found = (dir / refBaseName(rel)).string();
                    else
                        found = finder.find(rel);
                    if (!found.empty()) bytes = readBytes(found);
                }
                Image im;
                const std::string stem =
                    fs::path(refBaseName(rel.empty() ? mat.name : rel)).stem().string();
                if (!bytes.empty() &&
                    makeImage(std::move(bytes), uniqueImageName(out.images, stem), im)) {
                    out.images.push_back(std::move(im));
                    mat.image = (int)out.images.size() - 1;
                } else if (!bytes.empty()) {
                    out.warnings.push_back("texture " + refBaseName(rel) +
                                           " is not PNG/JPEG - skipped");
                }
                if (mat.image < 0) {
                    mat.missingTexture = refBaseName(rel);
                    mat.kd[0] = mat.kd[1] = mat.kd[2] = 1.0f;
                }
                texIndex[tex] = mat.image;
            }
        }
        out.materials.push_back(mat);
        const int idx = (int)out.materials.size() - 1;
        matIndex[m] = idx;
        return idx;
    };

    // top-level grouping: children of the scene root, unwrapping a lone
    // wrapper node that has no mesh of its own
    auto hasMeshBelow = [&](const ufbx_node* n, auto&& self) -> bool {
        if (n->mesh) return true;
        for (size_t c = 0; c < n->children.count; ++c)
            if (self(n->children.data[c], self)) return true;
        return false;
    };
    const ufbx_node* level = scene->root_node;
    for (;;) {
        const ufbx_node* only = nullptr;
        int count = 0;
        for (size_t c = 0; c < level->children.count; ++c)
            if (hasMeshBelow(level->children.data[c], hasMeshBelow)) {
                only = level->children.data[c];
                ++count;
            }
        if (count == 1 && !only->mesh && only->children.count > 0)
            level = only;
        else
            break;
    }
    std::map<const ufbx_node*, int> groupOf;
    auto groupFor = [&](const ufbx_node* n) {
        const ufbx_node* top = n;
        while (top->parent && top->parent != level) top = top->parent;
        if (!top->parent) top = n;
        auto it = groupOf.find(top);
        if (it != groupOf.end()) return it->second;
        std::string name(top->name.data, top->name.length);
        if (name.empty()) name = fs::path(path).stem().string();
        out.groups.push_back(name);
        groupOf[top] = (int)out.groups.size() - 1;
        return (int)out.groups.size() - 1;
    };

    std::vector<uint32_t> tri;
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node* node = scene->nodes.data[ni];
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || mesh->num_triangles == 0) continue;
        Object obj;
        obj.name = std::string(node->name.data, node->name.length);
        if (obj.name.empty()) obj.name = fs::path(path).stem().string();
        obj.group = groupFor(node);
        const ufbx_matrix& g = node->geometry_to_world;
        const ufbx_matrix nm = ufbx_get_compatible_matrix_for_normals(node);
        const bool mirrored = ufbx_matrix_determinant(&g) < 0.0;
        std::map<int, size_t> pieceOf;
        tri.resize(mesh->max_face_triangles * 3);
        for (size_t f = 0; f < mesh->faces.count; ++f) {
            const uint32_t slot =
                mesh->face_material.count ? mesh->face_material.data[f] : 0;
            const ufbx_material* m =
                slot < node->materials.count  ? node->materials.data[slot]
                : slot < mesh->materials.count ? mesh->materials.data[slot]
                                               : nullptr;
            const int mi = materialOf(m);
            auto it = pieceOf.find(mi);
            if (it == pieceOf.end()) {
                obj.pieces.push_back(Piece{mi, {}});
                it = pieceOf.emplace(mi, obj.pieces.size() - 1).first;
            }
            std::vector<float>& v = obj.pieces[it->second].verts;
            const uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), mesh,
                                                     mesh->faces.data[f]);
            for (uint32_t t = 0; t < n; ++t) {
                for (int k = 0; k < 3; ++k) {
                    const uint32_t ix = tri[t * 3 + (uint32_t)(mirrored ? 2 - k : k)];
                    const ufbx_vec3 p =
                        ufbx_transform_position(&g, ufbx_get_vertex_vec3(&mesh->vertex_position, ix));
                    ufbx_vec3 nr = {0, 1, 0};
                    if (mesh->vertex_normal.exists)
                        nr = ufbx_transform_direction(
                            &nm, ufbx_get_vertex_vec3(&mesh->vertex_normal, ix));
                    const double len = std::sqrt(nr.x * nr.x + nr.y * nr.y + nr.z * nr.z);
                    if (len > 1e-12) nr.x /= len, nr.y /= len, nr.z /= len;
                    ufbx_vec2 uv = {0, 0};
                    if (mesh->vertex_uv.exists)
                        uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
                    v.insert(v.end(), {(float)p.x, (float)p.y, (float)p.z, (float)nr.x,
                                       (float)nr.y, (float)nr.z, (float)uv.x,
                                       1.0f - (float)uv.y});
                }
            }
        }
        out.objects.push_back(std::move(obj));
    }
    ufbx_free_scene(scene);
    return true;
}

// --- planning --------------------------------------------------------------

void dropDegenerate(std::vector<float>& v, std::set<std::string>& seen) {
    std::vector<float> keep;
    keep.reserve(v.size());
    for (size_t t = 0; t + 23 < v.size(); t += 24) {
        const float* a = &v[t];
        const float* b = &v[t + 8];
        const float* c = &v[t + 16];
        const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        const float cx = uy * vz - uz * vy, cy = uz * vx - ux * vz,
                    cz = ux * vy - uy * vx;
        const float edge = std::max({ux * ux + uy * uy + uz * uz,
                                     vx * vx + vy * vy + vz * vz, 1e-20f});
        if (cx * cx + cy * cy + cz * cz <= 1e-12f * edge * edge) continue;
        // an exact copy of a triangle already kept (same corners, any
        // rotation of the same winding) is z-fighting and nothing else
        float key[9];
        int start = 0;
        for (int k = 1; k < 3; ++k)
            if (std::memcmp(&v[t + (size_t)k * 8], &v[t + (size_t)start * 8],
                            3 * sizeof(float)) < 0)
                start = k;
        for (int k = 0; k < 3; ++k)
            std::memcpy(key + k * 3, &v[t + (size_t)((start + k) % 3) * 8],
                        3 * sizeof(float));
        if (!seen.insert(std::string(reinterpret_cast<const char*>(key),
                                     sizeof(key)))
                 .second)
            continue;
        keep.insert(keep.end(), a, a + 24);
    }
    v.swap(keep);
}

void reduce(std::vector<float>& v, float keep) {
    const size_t corners = v.size() / 8;
    if (keep >= 0.999f || corners < 12) return;
    meshlod::Mesh m = meshlod::weldInterleaved(v.data(), corners, false);
    const size_t target =
        std::max<size_t>(4, (size_t)std::lround(m.vertexCount() * keep));
    meshlod::decimate(m, target);
    std::vector<float> out = meshlod::unweldInterleaved(m);
    if (out.size() >= 24) {
        meshlod::recomputeFaceNormals(out);
        v.swap(out);
    }
}

}  // namespace

std::string safeName(const std::string& in) {
    std::string out;
    for (char c : in) {
        const unsigned char u = (unsigned char)c;
        if (std::isalnum(u) || c == '_' || c == '-')
            out += c;
        else if (c == '.' || c == ' ')
            out += '-';
        else if (u < 0x80 || (u & 0xC0) == 0xC0)
            out += '_';  // one '_' per character, not per UTF-8 byte
    }
    while (!out.empty() && (out.back() == '-' || out.back() == '_')) out.pop_back();
    if (out.empty()) out = "model";
    return out;
}

int Source::missingTextures() const {
    int n = 0;
    for (const Material& m : materials) n += !m.missingTexture.empty();
    return n;
}

bool load(const std::string& path, const std::vector<std::string>& textureDirs,
          Source& out, std::string& error) {
    out = Source{};
    out.path = path;
    const std::string e = extOf(path);
    out.format = e.size() > 1 ? e.substr(1) : e;
    std::vector<std::string> dirs{fs::path(path).parent_path().string()};
    dirs.insert(dirs.end(), textureDirs.begin(), textureDirs.end());
    const TextureFinder finder(dirs);
    bool ok = false;
    try {
        if (e == ".obj")
            ok = loadObj(path, finder, out, error);
        else if (e == ".fbx")
            ok = loadFbx(path, finder, out, error);
        else if (e == ".glb")
            ok = loadSkel(path, finder, out, error);
        else
            error = "unsupported format " + e + " (use .obj, .glb or .fbx)";
    } catch (const std::exception& ex) {
        error = ex.what();
        ok = false;
    }
    if (!ok) return false;
    finishBounds(out);
    if (out.tris == 0) {
        error = "the file contains no triangles";
        return false;
    }
    return true;
}

std::vector<Planned> plan(const Source& src, const Options& opt) {
    // which objects go into which model
    std::vector<std::vector<int>> members;
    std::vector<std::string> names;
    const std::string base =
        safeName(opt.baseName.empty() ? fs::path(src.path).stem().string()
                                      : opt.baseName);
    if (opt.split == Split::None || src.objects.size() <= 1) {
        members.emplace_back();
        for (size_t i = 0; i < src.objects.size(); ++i)
            members.back().push_back((int)i);
        names.push_back(base);
    } else if (opt.split == Split::TopLevel) {
        members.resize(src.groups.size());
        for (size_t i = 0; i < src.objects.size(); ++i)
            members[(size_t)src.objects[i].group].push_back((int)i);
        names = src.groups;
    } else {
        for (size_t i = 0; i < src.objects.size(); ++i) {
            members.push_back({(int)i});
            names.push_back(src.objects[i].name);
        }
    }

    std::vector<Planned> out;
    std::set<std::string> taken;
    for (size_t mi = 0; mi < members.size(); ++mi) {
        if (members[mi].empty()) continue;
        Planned pl;
        std::string stem = mi < names.size() && members.size() > 1
                               ? safeName(names[mi])
                               : base;
        pl.name = stem;
        for (int n = 2; taken.count(lower(pl.name)); ++n)
            pl.name = stem + "-" + std::to_string(n);
        taken.insert(lower(pl.name));

        // one piece per material
        std::map<int, size_t> byMat;
        for (int oi : members[mi])
            for (const Piece& p : src.objects[(size_t)oi].pieces) {
                auto it = byMat.find(p.material);
                if (it == byMat.end()) {
                    pl.pieces.push_back(Piece{p.material, {}});
                    it = byMat.emplace(p.material, pl.pieces.size() - 1).first;
                }
                std::vector<float>& dst = pl.pieces[it->second].verts;
                dst.insert(dst.end(), p.verts.begin(), p.verts.end());
            }
        std::set<std::string> seen;
        for (Piece& p : pl.pieces) {
            if (opt.cleanup) dropDegenerate(p.verts, seen);
            reduce(p.verts, std::clamp(opt.keep, 0.05f, 1.0f));
        }
        pl.pieces.erase(std::remove_if(pl.pieces.begin(), pl.pieces.end(),
                                       [](const Piece& p) { return p.verts.size() < 24; }),
                        pl.pieces.end());
        if (pl.pieces.empty()) continue;

        bool first = true;
        float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
        for (const Piece& p : pl.pieces)
            for (size_t i = 0; i + 7 < p.verts.size(); i += 8)
                grow(mn, mx, &p.verts[i], first);
        if (opt.pivot != Pivot::Keep) {
            pl.origin = {(mn[0] + mx[0]) * 0.5f,
                         opt.pivot == Pivot::BottomCenter ? mn[1]
                                                          : (mn[1] + mx[1]) * 0.5f,
                         (mn[2] + mx[2]) * 0.5f};
            for (Piece& p : pl.pieces)
                for (size_t i = 0; i + 7 < p.verts.size(); i += 8)
                    for (int a = 0; a < 3; ++a) p.verts[i + (size_t)a] -= pl.origin[(size_t)a];
        }
        for (int a = 0; a < 3; ++a) {
            pl.mn[a] = mn[a] - pl.origin[(size_t)a];
            pl.mx[a] = mx[a] - pl.origin[(size_t)a];
        }
        // What the build will do with it: flat face normals (the static
        // loader recomputes them), then the same stripifier bakeStaticModels
        // runs - so this number is the bake's own answer, not an estimate of
        // one.
        for (const Piece& p : pl.pieces) {
            const int list = (int)(p.verts.size() / 8);
            pl.tris += list / 3;
            pl.listVerts += list;
            std::vector<float> flat = p.verts, strip;
            std::vector<unsigned char> noAo, stripAo;
            meshlod::recomputeFaceNormals(flat);
            pl.stripVerts += meshstrip::build(flat, noAo, meshstrip::kRun, strip, stripAo)
                                 ? (int)(strip.size() / 8)
                                 : list;
        }
        out.push_back(std::move(pl));
    }
    return out;
}

namespace {

std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

// .mtl-safe material names: objparser and the PS2 loader read `usemtl` with
// a whitespace tokenizer.
std::vector<std::string> materialNames(const Source& src) {
    std::vector<std::string> out;
    std::set<std::string> taken;
    for (const Material& m : src.materials) {
        std::string n = m.name.empty() ? "default" : m.name;
        for (char& c : n)
            if (std::isspace((unsigned char)c)) c = '_';
        const std::string stem = n;
        for (int k = 2; taken.count(n); ++k) n = stem + "-" + std::to_string(k);
        taken.insert(n);
        out.push_back(n);
    }
    return out;
}

bool writeText(const fs::path& p, const std::string& text, std::string& error) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot write " + p.string();
        return false;
    }
    f << text;
    return (bool)f;
}

std::string objText(const Planned& pl, const std::vector<std::string>& matNames,
                    const std::string& mtlFile, const std::string& sourceName) {
    std::ostringstream o;
    o << "# Imported by TyraX from " << sourceName << "\n";
    o << "mtllib " << mtlFile << "\n";
    std::map<std::string, int> posIdx, uvIdx;
    std::ostringstream vs, vts, fs_;
    int np = 0, nt = 0;
    for (const Piece& p : pl.pieces) {
        fs_ << "usemtl "
            << (p.material >= 0 && p.material < (int)matNames.size()
                    ? matNames[(size_t)p.material]
                    : std::string("default"))
            << "\n";
        for (size_t t = 0; t + 23 < p.verts.size(); t += 24) {
            fs_ << "f";
            for (int k = 0; k < 3; ++k) {
                const float* c = &p.verts[t + (size_t)k * 8];
                const std::string pk(reinterpret_cast<const char*>(c),
                                     3 * sizeof(float));
                auto pi = posIdx.find(pk);
                if (pi == posIdx.end()) {
                    vs << "v " << fmt(c[0]) << " " << fmt(c[1]) << " " << fmt(c[2])
                       << "\n";
                    pi = posIdx.emplace(pk, ++np).first;
                }
                const float uv[2] = {c[6], 1.0f - c[7]};  // image space -> obj
                const std::string tk(reinterpret_cast<const char*>(uv), sizeof(uv));
                auto ti = uvIdx.find(tk);
                if (ti == uvIdx.end()) {
                    vts << "vt " << fmt(uv[0]) << " " << fmt(uv[1]) << "\n";
                    ti = uvIdx.emplace(tk, ++nt).first;
                }
                fs_ << " " << pi->second << "/" << ti->second;
            }
            fs_ << "\n";
        }
    }
    o << vs.str() << vts.str() << fs_.str();
    return o.str();
}

}  // namespace

bool write(const Source& src, const std::vector<Planned>& models,
           const Options& opt, const std::string& projectDir,
           const std::string& destFolderRel, Result& out, std::string& error) {
    out = Result{};
    const fs::path destDir = fs::path(projectDir) / destFolderRel;
    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (ec) {
        error = "cannot create " + destFolderRel + ": " + ec.message();
        return false;
    }
    const std::string base =
        safeName(opt.baseName.empty() ? fs::path(src.path).stem().string()
                                      : opt.baseName);
    const std::string mtlFile = base + ".mtl";
    const std::vector<std::string> matNames = materialNames(src);

    // Which materials and images this import actually carries.
    std::vector<char> usedMat(src.materials.size(), 0);
    bool usesDefault = false;
    std::vector<char> include = opt.include;
    include.resize(models.size(), 1);
    if (opt.writeStatic)
        for (size_t i = 0; i < models.size(); ++i)
            if (include[i])
                for (const Piece& p : models[i].pieces) {
                    if (p.material >= 0 && p.material < (int)usedMat.size())
                        usedMat[(size_t)p.material] = 1;
                    else
                        usesDefault = true;
                }
    if (opt.writeAnimated)
        std::fill(usedMat.begin(), usedMat.end(), 1);

    std::vector<char> usedImage(src.images.size(), 0);
    for (size_t i = 0; i < src.materials.size(); ++i)
        if (usedMat[i] && src.materials[i].image >= 0)
            usedImage[(size_t)src.materials[i].image] = 1;
    for (size_t i = 0; i < src.images.size(); ++i) {
        if (!usedImage[i]) continue;
        const Image& im = src.images[i];
        std::vector<unsigned char> png = im.png;
        const int longest = std::max(im.w, im.h);
        if (opt.maxTexture > 0 && longest > opt.maxTexture) {
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load_from_memory(png.data(), (int)png.size(),
                                                      &w, &h, &comp, 4);
            if (px) {
                const int dw = std::max(1, w * opt.maxTexture / longest);
                const int dh = std::max(1, h * opt.maxTexture / longest);
                const std::vector<unsigned char> small =
                    pngquant::resizeRGBA(px, w, h, dw, dh);
                stbi_image_free(px);
                png.clear();
                stbi_write_png_to_func(toVector, &png, dw, dh, 4, small.data(),
                                       dw * 4);
                out.notes.push_back(im.name + " " + std::to_string(w) + "x" +
                                    std::to_string(h) + " -> " +
                                    std::to_string(dw) + "x" + std::to_string(dh));
            }
        }
        std::ofstream f(destDir / im.name, std::ios::binary | std::ios::trunc);
        if (!f) {
            error = "cannot write texture " + im.name;
            return false;
        }
        f.write(reinterpret_cast<const char*>(png.data()), (std::streamsize)png.size());
        ++out.textures;
    }

    // The material library - always, even for a model with no materials at
    // all: a .mtl next to every model is what the editor's material tools
    // open, so an import never leaves a model without one.
    {
        std::ostringstream m;
        m << "# Imported by TyraX from " << fs::path(src.path).filename().string()
          << "\n";
        if (usesDefault)
            m << "\nnewmtl default\nKd 1 1 1\n";
        for (size_t i = 0; i < src.materials.size(); ++i) {
            if (!usedMat[i]) continue;
            const Material& mt = src.materials[i];
            m << "\nnewmtl " << matNames[i] << "\n";
            m << "Kd " << fmt(mt.kd[0]) << " " << fmt(mt.kd[1]) << " "
              << fmt(mt.kd[2]) << "\n";
            if (mt.ke[0] > 0 || mt.ke[1] > 0 || mt.ke[2] > 0)
                m << "Ke " << fmt(mt.ke[0]) << " " << fmt(mt.ke[1]) << " "
                  << fmt(mt.ke[2]) << "\n";
            if (mt.glowRange > 0)
                m << "# tyra-glow-light " << fmt(mt.glowRange) << " "
                  << fmt(mt.glowLight) << "\n";
            if (mt.image >= 0)
                m << "map_Kd " << src.images[(size_t)mt.image].name << "\n";
            if (mt.refl == "@sky")
                m << "refl -type sphere" << (mt.reflRounded ? " -rounded" : "")
                  << " -mm 0 " << fmt(mt.reflStrength) << " @sky\n";
            if (!mt.missingTexture.empty())
                m << "# texture not found at import: " << mt.missingTexture << "\n";
        }
        if (!writeText(destDir / mtlFile, m.str(), error)) return false;
        out.material = (fs::path(destFolderRel) / mtlFile).generic_string();
    }

    if (opt.writeStatic) {
        const std::string srcName = fs::path(src.path).filename().string();
        for (size_t i = 0; i < models.size(); ++i) {
            if (!include[i]) continue;
            const std::string file = models[i].name + ".obj";
            if (!writeText(destDir / file,
                           objText(models[i], matNames, mtlFile, srcName), error))
                return false;
            out.staticModels.push_back((fs::path(destFolderRel) / file).generic_string());
            out.origins.push_back(models[i].origin);
        }
    }

    if (opt.writeAnimated && src.canAnimate()) {
        const std::string e = extOf(src.path);
        const fs::path dst = destDir / (base + e);
        if (!(fs::exists(dst, ec) && fs::equivalent(src.path, dst, ec))) {
            fs::copy_file(src.path, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                error = "cannot copy " + src.path + ": " + ec.message();
                return false;
            }
        }
        out.animatedModel = (fs::path(destFolderRel) / (base + e)).generic_string();
        for (size_t i = 0; i < src.materials.size(); ++i)
            if (matNames[i] != (src.materials[i].name.empty() ? "default"
                                                               : src.materials[i].name))
                out.notes.push_back("material \"" + src.materials[i].name +
                                    "\" was renamed in the .mtl - the animated "
                                    "model shows it untextured");
    }
    return true;
}

std::string staticTwinPath(const std::string& modelRel) {
    const std::string e = extOf(modelRel);
    if (e != ".glb" && e != ".fbx") return "";
    return (fs::path(modelRel).parent_path() / (fs::path(modelRel).stem().string() + ".obj"))
        .generic_string();
}

std::string siblingMaterialPath(const std::string& modelRel) {
    return (fs::path(modelRel).parent_path() /
            (fs::path(modelRel).stem().string() + ".mtl"))
        .generic_string();
}

std::string staticTwin(const std::string& projectDir, const std::string& modelRel,
                       std::string& error) {
    const std::string twin = staticTwinPath(modelRel);
    if (twin.empty()) {
        error = "not an animated model";
        return "";
    }
    std::error_code ec;
    if (fs::exists(fs::path(projectDir) / twin, ec)) return twin;
    Source src;
    if (!load((fs::path(projectDir) / modelRel).string(), {}, src, error)) return "";
    Options opt;
    opt.baseName = fs::path(modelRel).stem().string();
    opt.maxTexture = 0;
    const std::vector<Planned> models = plan(src, opt);
    if (models.empty()) {
        error = "nothing to write";
        return "";
    }
    // The .mtl an animated import already wrote is the user's material from
    // then on - keep it (and its textures) when it is there.
    const fs::path mtl = fs::path(projectDir) / siblingMaterialPath(modelRel);
    const bool hadMtl = fs::exists(mtl, ec);
    std::string kept;
    if (hadMtl) {
        std::ifstream f(mtl, std::ios::binary);
        kept.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    Result res;
    if (!write(src, models, opt, projectDir,
               fs::path(modelRel).parent_path().generic_string(), res, error))
        return "";
    if (hadMtl) writeText(mtl, kept, error);
    return res.staticModels.empty() ? "" : res.staticModels.front();
}

}  // namespace modelimport
