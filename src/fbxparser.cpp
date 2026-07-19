#include "fbxparser.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

#include "ufbx.h"

namespace fbxparser {
namespace {

using glbparser::Baked;
using glbparser::Clip;
using glbparser::Image;
using glbparser::Part;
using glbparser::Skel;
using glbparser::SkelChannel;
using glbparser::SkelClip;
using glbparser::SkelJoint;
using glbparser::SkelNode;
using glbparser::SkelPart;

// Fixed resample rate for FBX animation. Curves are sampled here and RDP-
// reduced per channel; 24 Hz keeps any authored pose within half a frame.
constexpr double kSampleFps = 24.0;

ufbx_scene* loadScene(const std::string& path, std::string& error) {
    // Normalize every FBX to the .glb convention the whole pipeline assumes:
    // right-handed Y-up, meters. Geometry transforms (Maya pivots) and the
    // unit/axis conversion are baked into the vertex data, so the node
    // hierarchy stays a plain TRS chain the PS2 runtime can lerp.
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.geometry_transform_handling =
        UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    opts.generate_missing_normals = true;

    ufbx_error err;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) {
        char buf[512];
        ufbx_format_error(buf, sizeof buf, &err);
        error = buf;
    }
    return scene;
}

// --- textures ------------------------------------------------------------

void stbWriteToVector(void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<unsigned char>*>(ctx);
    const unsigned char* p = static_cast<const unsigned char*>(data);
    v->insert(v->end(), p, p + size);
}

std::string sanitizePngName(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (isalnum((unsigned char)c) || c == '_' || c == '-')
            out += (char)tolower((unsigned char)c);
    }
    if (out.empty()) out = "tex";
    return out + ".png";
}

bool isPng(const unsigned char* d, size_t n) {
    static const unsigned char magic[] = {0x89, 'P', 'N', 'G'};
    return n >= 4 && memcmp(d, magic, 4) == 0;
}

// The material's base color texture -> an index into `images`, importing it
// on first use. Embedded payloads win; otherwise the referenced file is
// looked up as exported and then next to the .fbx. Non-PNG data is
// transcoded - the PS2 loader is PNG-only.
int imageFor(const ufbx_material* mat, const std::filesystem::path& fbxDir,
             std::vector<Image>& images,
             std::map<const ufbx_texture*, int>& slots,
             std::vector<std::string>& warnings) {
    if (!mat) return -1;
    const ufbx_texture* tex = mat->pbr.base_color.texture
                                  ? mat->pbr.base_color.texture
                                  : mat->fbx.diffuse_color.texture;
    if (!tex) return -1;
    if (auto it = slots.find(tex); it != slots.end()) return it->second;

    std::vector<unsigned char> bytes;
    if (tex->content.size) {
        const unsigned char* p =
            static_cast<const unsigned char*>(tex->content.data);
        bytes.assign(p, p + tex->content.size);
    } else {
        std::filesystem::path cand(
            std::string(tex->absolute_filename.data, tex->absolute_filename.length));
        std::error_code ec;
        if (cand.empty() || !std::filesystem::exists(cand, ec)) {
            std::string rel(tex->filename.data, tex->filename.length);
            cand = fbxDir / std::filesystem::path(rel).filename();
        }
        std::ifstream f(cand, std::ios::binary);
        if (f) {
            std::vector<char> raw((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
            bytes.assign(raw.begin(), raw.end());
        }
    }
    if (bytes.empty()) {
        warnings.push_back("texture \"" +
                           std::string(tex->filename.data, tex->filename.length) +
                           "\" not embedded and not found next to the .fbx - "
                           "skipped");
        slots[tex] = -1;
        return -1;
    }

    Image baked;
    if (isPng(bytes.data(), bytes.size())) {
        baked.png = std::move(bytes);
    } else {
        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = stbi_load_from_memory(
            bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
        if (!pixels) {
            warnings.push_back("undecodable texture - skipped");
            slots[tex] = -1;
            return -1;
        }
        stbi_write_png_to_func(stbWriteToVector, &baked.png, w, h, 4, pixels,
                               w * 4);
        stbi_image_free(pixels);
        warnings.push_back("texture transcoded to PNG");
    }
    {
        int w = 0, h = 0, comp = 0;
        if (stbi_info_from_memory(baked.png.data(), (int)baked.png.size(), &w,
                                  &h, &comp)) {
            const bool pot = w > 0 && h > 0 && !(w & (w - 1)) && !(h & (h - 1));
            if (!pot)
                warnings.push_back("texture " + std::to_string(w) + "x" +
                                   std::to_string(h) +
                                   " is not power-of-two - the PS2 cannot "
                                   "load it");
        }
    }
    std::string base(tex->filename.data, tex->filename.length);
    baked.name = sanitizePngName(
        std::filesystem::path(base).stem().string());
    for (const Image& other : images)
        if (other.name == baked.name)
            baked.name = "t" + std::to_string(images.size()) + "-" + baked.name;
    images.push_back(std::move(baked));
    const int idx = (int)images.size() - 1;
    slots[tex] = idx;
    return idx;
}

void baseColorOf(const ufbx_material* mat, float out[4]) {
    out[0] = out[1] = out[2] = out[3] = 1.0f;
    if (!mat) return;
    if (mat->pbr.base_color.has_value) {
        out[0] = (float)mat->pbr.base_color.value_vec4.x;
        out[1] = (float)mat->pbr.base_color.value_vec4.y;
        out[2] = (float)mat->pbr.base_color.value_vec4.z;
    } else if (mat->fbx.diffuse_color.has_value) {
        out[0] = (float)mat->fbx.diffuse_color.value_vec3.x;
        out[1] = (float)mat->fbx.diffuse_color.value_vec3.y;
        out[2] = (float)mat->fbx.diffuse_color.value_vec3.z;
    }
}

// --- geometry ------------------------------------------------------------

// One triangle-list vertex of one mesh instance, in geometry (bind) space,
// remembering which control point it skins from.
struct SrcVertex {
    float pos[3], nrm[3], uv[2];
    uint32_t controlPoint;  // index into the mesh's skin weights
    int instance;           // index into Instances
};

struct Instance {
    ufbx_node* node;
    ufbx_mesh* mesh;
};

// All triangles of the scene, grouped by material (the draw-batch shape both
// Baked and Skel want). Instance geometry is expanded to a flat list here.
struct MaterialBatch {
    const ufbx_material* material = nullptr;
    std::string name;
    std::vector<SrcVertex> verts;
};

void collectBatches(ufbx_scene* scene, std::vector<Instance>& instances,
                    std::vector<MaterialBatch>& batches,
                    std::vector<std::string>& warnings) {
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        ufbx_node* node = scene->nodes.data[ni];
        if (!node->mesh) continue;
        instances.push_back({node, node->mesh});
    }
    std::map<const ufbx_material*, size_t> slot;
    std::vector<uint32_t> tri;
    for (int ii = 0; ii < (int)instances.size(); ++ii) {
        ufbx_mesh* mesh = instances[ii].mesh;
        if (mesh->blend_deformers.count)
            warnings.push_back("blend shapes are not supported - skipped");
        tri.resize(mesh->max_face_triangles * 3);
        for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
            const ufbx_face face = mesh->faces.data[fi];
            const uint32_t numTris =
                ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
            const ufbx_material* mat = nullptr;
            if (mesh->face_material.count > fi &&
                mesh->materials.count)
                mat = mesh->materials.data[mesh->face_material.data[fi]];
            auto it = slot.find(mat);
            if (it == slot.end()) {
                MaterialBatch b;
                b.material = mat;
                b.name = mat ? std::string(mat->name.data, mat->name.length) : "";
                it = slot.emplace(mat, batches.size()).first;
                batches.push_back(std::move(b));
            }
            MaterialBatch& batch = batches[it->second];
            for (uint32_t k = 0; k < numTris * 3; ++k) {
                const uint32_t idx = tri[k];
                SrcVertex v = {};
                const ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, idx);
                v.pos[0] = (float)p.x, v.pos[1] = (float)p.y, v.pos[2] = (float)p.z;
                if (mesh->vertex_normal.exists) {
                    const ufbx_vec3 n =
                        ufbx_get_vertex_vec3(&mesh->vertex_normal, idx);
                    v.nrm[0] = (float)n.x, v.nrm[1] = (float)n.y,
                    v.nrm[2] = (float)n.z;
                } else {
                    v.nrm[1] = 1.0f;
                }
                if (mesh->vertex_uv.exists) {
                    const ufbx_vec2 t = ufbx_get_vertex_vec2(&mesh->vertex_uv, idx);
                    // FBX UVs are bottom-left origin; images (and the whole
                    // .glb pipeline) are top-left.
                    v.uv[0] = (float)t.x;
                    v.uv[1] = 1.0f - (float)t.y;
                }
                v.controlPoint = mesh->vertex_indices.data[idx];
                v.instance = ii;
                batch.verts.push_back(v);
            }
        }
    }
}

// World matrix that skins vertex `cp` of `inst` in an (evaluated) scene:
// the blended skin matrix when the mesh is skinned, the node's world
// transform otherwise.
ufbx_matrix skinMatrix(const ufbx_scene* ev, const Instance& inst,
                       uint32_t cp) {
    const ufbx_node* node = ev->nodes.data[inst.node->typed_id];
    if (node->mesh && node->mesh->skin_deformers.count) {
        return ufbx_get_skin_vertex_matrix(node->mesh->skin_deformers.data[0],
                                           cp, &node->geometry_to_world);
    }
    return node->geometry_to_world;
}

// --- animation channels (parseSkel) --------------------------------------

// Recursive RDP keyframe reduction: keep the sample farthest from the
// kept-neighbor lerp while any component errs more than eps.
void rdpKeep(const std::vector<float>& times,
             const std::vector<float>& values, int stride, int lo, int hi,
             float eps, std::vector<char>& keep) {
    if (hi - lo < 2) return;
    float worst = 0.0f;
    int worstIdx = -1;
    const float t0 = times[lo], t1 = times[hi];
    for (int i = lo + 1; i < hi; ++i) {
        const float f = (t1 - t0) > 1e-9f ? (times[i] - t0) / (t1 - t0) : 0.0f;
        for (int c = 0; c < stride; ++c) {
            const float interp = values[lo * stride + c] +
                                 (values[hi * stride + c] -
                                  values[lo * stride + c]) *
                                     f;
            const float err = fabsf(values[i * stride + c] - interp);
            if (err > worst) {
                worst = err;
                worstIdx = i;
            }
        }
    }
    if (worstIdx >= 0 && worst > eps) {
        keep[worstIdx] = 1;
        rdpKeep(times, values, stride, lo, worstIdx, eps, keep);
        rdpKeep(times, values, stride, worstIdx, hi, eps, keep);
    }
}

// Sample one node's local TRS over a clip and emit up to three reduced
// channels. Constant channels equal to the bind pose are dropped entirely.
void sampleNodeChannels(const ufbx_anim* anim, const ufbx_node* node,
                        double timeBegin, double duration, SkelClip& clip) {
    const int samples =
        duration > 0.0 ? (int)llround(duration * kSampleFps) + 1 : 1;
    std::vector<float> times((size_t)samples);
    std::vector<float> tv((size_t)samples * 3), rv((size_t)samples * 4),
        sv((size_t)samples * 3);
    ufbx_quat prevQ = {0, 0, 0, 1};
    for (int i = 0; i < samples; ++i) {
        const double t = samples > 1 ? duration * i / (samples - 1) : 0.0;
        times[(size_t)i] = (float)t;
        const ufbx_transform tr =
            ufbx_evaluate_transform(anim, node, timeBegin + t);
        tv[(size_t)i * 3 + 0] = (float)tr.translation.x;
        tv[(size_t)i * 3 + 1] = (float)tr.translation.y;
        tv[(size_t)i * 3 + 2] = (float)tr.translation.z;
        ufbx_quat q = tr.rotation;
        // Hemisphere continuity: the runtime lerps raw components.
        if (i > 0 && (q.x * prevQ.x + q.y * prevQ.y + q.z * prevQ.z +
                      q.w * prevQ.w) < 0.0) {
            q.x = -q.x, q.y = -q.y, q.z = -q.z, q.w = -q.w;
        }
        prevQ = q;
        rv[(size_t)i * 4 + 0] = (float)q.x;
        rv[(size_t)i * 4 + 1] = (float)q.y;
        rv[(size_t)i * 4 + 2] = (float)q.z;
        rv[(size_t)i * 4 + 3] = (float)q.w;
        sv[(size_t)i * 3 + 0] = (float)tr.scale.x;
        sv[(size_t)i * 3 + 1] = (float)tr.scale.y;
        sv[(size_t)i * 3 + 2] = (float)tr.scale.z;
    }

    const float bindT[3] = {(float)node->local_transform.translation.x,
                            (float)node->local_transform.translation.y,
                            (float)node->local_transform.translation.z};
    const float bindR[4] = {(float)node->local_transform.rotation.x,
                            (float)node->local_transform.rotation.y,
                            (float)node->local_transform.rotation.z,
                            (float)node->local_transform.rotation.w};
    const float bindS[3] = {(float)node->local_transform.scale.x,
                            (float)node->local_transform.scale.y,
                            (float)node->local_transform.scale.z};

    auto emit = [&](int path, const std::vector<float>& vals, int stride,
                    const float* bind, float eps) {
        // Constant channel identical to the bind pose: nothing to store.
        bool differs = false;
        for (size_t i = 0; i < vals.size() && !differs; ++i)
            differs = fabsf(vals[i] - bind[i % (size_t)stride]) > eps;
        if (!differs) return;

        std::vector<char> keep((size_t)samples, 0);
        keep.front() = keep.back() = 1;
        rdpKeep(times, vals, stride, 0, samples - 1, eps, keep);

        SkelChannel ch;
        ch.node = (int)node->typed_id;
        ch.path = path;
        ch.step = 0;
        for (int i = 0; i < samples; ++i) {
            if (!keep[(size_t)i]) continue;
            ch.times.push_back(times[(size_t)i]);
            for (int c = 0; c < stride; ++c)
                ch.values.push_back(vals[(size_t)i * stride + c]);
        }
        clip.channels.push_back(std::move(ch));
    };
    emit(0, tv, 3, bindT, 1e-4f);
    emit(1, rv, 4, bindR, 1e-4f);
    emit(2, sv, 3, bindS, 1e-4f);
}

// FBX takes are named "Armature|Action" by most exporters; the short action
// name is what authors type into clip fields. Falls back to the full name on
// a collision (two rigs sharing an action name).
std::string clipName(const ufbx_anim_stack* st,
                     const std::vector<std::string>& taken) {
    std::string full(st->name.data, st->name.length);
    std::string n = full;
    if (const size_t bar = n.find_last_of('|');
        bar != std::string::npos && bar + 1 < n.size())
        n = n.substr(bar + 1);
    for (const std::string& t : taken)
        if (t == n) return full;
    return n;
}

void matrixTo16(const ufbx_matrix& m, float out[16]) {
    // ufbx is 3x4 column-major; pad to the 4x4 column-major layout Skel uses.
    const float cols[4][3] = {
        {(float)m.m00, (float)m.m10, (float)m.m20},
        {(float)m.m01, (float)m.m11, (float)m.m21},
        {(float)m.m02, (float)m.m12, (float)m.m22},
        {(float)m.m03, (float)m.m13, (float)m.m23}};
    for (int c = 0; c < 4; ++c) {
        out[c * 4 + 0] = cols[c][0];
        out[c * 4 + 1] = cols[c][1];
        out[c * 4 + 2] = cols[c][2];
        out[c * 4 + 3] = c == 3 ? 1.0f : 0.0f;
    }
}

}  // namespace

// --- bake (morph frames, editor preview) ---------------------------------

bool bake(const std::string& path, float fps, Baked& out, std::string& error) {
    out = Baked{};
    ufbx_scene* scene = loadScene(path, error);
    if (!scene) return false;

    std::vector<Instance> instances;
    std::vector<MaterialBatch> batches;
    collectBatches(scene, instances, batches, out.warnings);
    if (batches.empty()) {
        error = "no triangles in file";
        ufbx_free_scene(scene);
        return false;
    }

    const std::filesystem::path dir =
        std::filesystem::path(path).parent_path();
    std::map<const ufbx_texture*, int> texSlots;
    for (const MaterialBatch& b : batches) {
        Part part;
        part.material = b.name;
        baseColorOf(b.material, part.baseColor);
        part.image = imageFor(b.material, dir, out.images, texSlots,
                              out.warnings);
        part.vertexCount = (int)b.verts.size();
        part.uvs.reserve(b.verts.size() * 2);
        for (const SrcVertex& v : b.verts) {
            part.uvs.push_back(v.uv[0]);
            part.uvs.push_back(v.uv[1]);
        }
        out.parts.push_back(std::move(part));
    }

    // Clip table: every anim stack, sampled at `fps`; a static file gets a
    // single-frame "default" clip - same convention as .glb.
    struct StackFrames {
        const ufbx_anim_stack* stack;
        int frames;
    };
    std::vector<StackFrames> stacks;
    std::vector<std::string> takenNames;
    int total = 0;
    for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
        const ufbx_anim_stack* st = scene->anim_stacks.data[i];
        const double dur = st->time_end - st->time_begin;
        const int frames =
            dur > 0.0 ? (int)llround(dur * (double)fps) + 1 : 1;
        Clip clip;
        clip.name = clipName(st, takenNames);
        takenNames.push_back(clip.name);
        clip.firstFrame = total;
        clip.frameCount = frames;
        out.clips.push_back(clip);
        stacks.push_back({st, frames});
        total += frames;
    }
    if (stacks.empty()) {
        out.clips.push_back({"default", 0, 1});
        stacks.push_back({nullptr, 1});
        total = 1;
    }
    out.frameCount = total;
    out.fps = fps;

    for (Part& p : out.parts) {
        p.positions.resize((size_t)total * p.vertexCount * 3);
        p.normals.resize((size_t)total * p.vertexCount * 3);
    }

    int frameBase = 0;
    for (const StackFrames& sf : stacks) {
        for (int f = 0; f < sf.frames; ++f) {
            const ufbx_scene* ev = scene;
            ufbx_scene* owned = nullptr;
            if (sf.stack) {
                const double dur = sf.stack->time_end - sf.stack->time_begin;
                const double t =
                    sf.frames > 1 ? dur * f / (sf.frames - 1) : 0.0;
                ufbx_error err;
                owned = ufbx_evaluate_scene(scene, sf.stack->anim,
                                            sf.stack->time_begin + t, nullptr,
                                            &err);
                if (owned) ev = owned;
            }
            for (size_t bi = 0; bi < batches.size(); ++bi) {
                Part& part = out.parts[bi];
                const size_t base =
                    (size_t)(frameBase + f) * part.vertexCount * 3;
                for (size_t vi = 0; vi < batches[bi].verts.size(); ++vi) {
                    const SrcVertex& v = batches[bi].verts[vi];
                    const ufbx_matrix m =
                        skinMatrix(ev, instances[(size_t)v.instance],
                                   v.controlPoint);
                    const ufbx_vec3 wp = ufbx_transform_position(
                        &m, {v.pos[0], v.pos[1], v.pos[2]});
                    const ufbx_matrix nm = ufbx_matrix_for_normals(&m);
                    ufbx_vec3 wn = ufbx_transform_direction(
                        &nm, {v.nrm[0], v.nrm[1], v.nrm[2]});
                    const double len = sqrt(wn.x * wn.x + wn.y * wn.y +
                                            wn.z * wn.z);
                    if (len > 1e-9) {
                        wn.x /= len, wn.y /= len, wn.z /= len;
                    }
                    part.positions[base + vi * 3 + 0] = (float)wp.x;
                    part.positions[base + vi * 3 + 1] = (float)wp.y;
                    part.positions[base + vi * 3 + 2] = (float)wp.z;
                    part.normals[base + vi * 3 + 0] = (float)wn.x;
                    part.normals[base + vi * 3 + 1] = (float)wn.y;
                    part.normals[base + vi * 3 + 2] = (float)wn.z;
                }
            }
            if (owned) ufbx_free_scene(owned);
        }
        frameBase += sf.frames;
    }

    // Frame-0 AABB across all parts (the .glb convention).
    bool first = true;
    for (const Part& p : out.parts) {
        for (int vi = 0; vi < p.vertexCount; ++vi) {
            const float* pos = &p.positions[(size_t)vi * 3];
            for (int c = 0; c < 3; ++c) {
                if (first || pos[c] < out.min[c]) out.min[c] = pos[c];
                if (first || pos[c] > out.max[c]) out.max[c] = pos[c];
            }
            first = false;
        }
    }

    ufbx_free_scene(scene);
    return true;
}

// --- parseSkel (skeletal runtime) ----------------------------------------

bool parseSkel(const std::string& path, Skel& out, std::string& error) {
    out = Skel{};
    ufbx_scene* scene = loadScene(path, error);
    if (!scene) return false;

    std::vector<Instance> instances;
    std::vector<MaterialBatch> batches;
    collectBatches(scene, instances, batches, out.warnings);
    if (batches.empty()) {
        error = "no triangles in file";
        ufbx_free_scene(scene);
        return false;
    }

    // Nodes: the ufbx order, parents always before children (typed_id is the
    // index into scene->nodes, so parent links map 1:1).
    out.nodes.resize(scene->nodes.count);
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* n = scene->nodes.data[i];
        SkelNode& sn = out.nodes[i];
        sn.parent = n->parent ? (int)n->parent->typed_id : -1;
        sn.hasMatrix = false;
        sn.t[0] = (float)n->local_transform.translation.x;
        sn.t[1] = (float)n->local_transform.translation.y;
        sn.t[2] = (float)n->local_transform.translation.z;
        sn.r[0] = (float)n->local_transform.rotation.x;
        sn.r[1] = (float)n->local_transform.rotation.y;
        sn.r[2] = (float)n->local_transform.rotation.z;
        sn.r[3] = (float)n->local_transform.rotation.w;
        sn.s[0] = (float)n->local_transform.scale.x;
        sn.s[1] = (float)n->local_transform.scale.y;
        sn.s[2] = (float)n->local_transform.scale.z;
    }

    // Palette: one slot per skin cluster (bone + inverse bind matrix) and
    // one identity-IBM slot per rigid mesh node.
    std::map<const void*, int> palette;  // cluster or node -> slot
    auto clusterSlot = [&](const ufbx_skin_cluster* cl) {
        if (auto it = palette.find(cl); it != palette.end()) return it->second;
        SkelJoint j;
        j.node = (int)cl->bone_node->typed_id;
        matrixTo16(cl->geometry_to_bone, j.ibm);
        out.palette.push_back(j);
        palette[cl] = (int)out.palette.size() - 1;
        return (int)out.palette.size() - 1;
    };
    auto rigidSlot = [&](const ufbx_node* node) {
        if (auto it = palette.find(node); it != palette.end())
            return it->second;
        SkelJoint j;
        j.node = (int)node->typed_id;
        for (int c = 0; c < 4; ++c) j.ibm[c * 4 + c] = 1.0f;
        out.palette.push_back(j);
        palette[node] = (int)out.palette.size() - 1;
        return (int)out.palette.size() - 1;
    };

    const std::filesystem::path dir =
        std::filesystem::path(path).parent_path();
    std::map<const ufbx_texture*, int> texSlots;
    for (const MaterialBatch& b : batches) {
        SkelPart part;
        part.material = b.name;
        baseColorOf(b.material, part.baseColor);
        part.image = imageFor(b.material, dir, out.images, texSlots,
                              out.warnings);
        part.vertexCount = (int)b.verts.size();
        part.positions.reserve(b.verts.size() * 3);
        part.normals.reserve(b.verts.size() * 3);
        part.uvs.reserve(b.verts.size() * 2);
        part.joints.reserve(b.verts.size() * 4);
        part.weights.reserve(b.verts.size() * 4);
        for (const SrcVertex& v : b.verts) {
            part.positions.insert(part.positions.end(),
                                  {v.pos[0], v.pos[1], v.pos[2]});
            part.normals.insert(part.normals.end(),
                                {v.nrm[0], v.nrm[1], v.nrm[2]});
            part.uvs.insert(part.uvs.end(), {v.uv[0], v.uv[1]});

            unsigned char joints[4] = {0, 0, 0, 0};
            unsigned char weights[4] = {0, 0, 0, 0};
            const Instance& inst = instances[(size_t)v.instance];
            const ufbx_skin_deformer* skin =
                inst.mesh->skin_deformers.count
                    ? inst.mesh->skin_deformers.data[0]
                    : nullptr;
            if (skin && v.controlPoint < skin->vertices.count) {
                const ufbx_skin_vertex sv =
                    skin->vertices.data[v.controlPoint];
                // Weights come sorted by decreasing influence; the strongest
                // four are kept and renormalized to sum 255.
                float w[4] = {0, 0, 0, 0};
                int slots4[4] = {0, 0, 0, 0};
                int n = 0;
                float sum = 0.0f;
                for (uint32_t k = 0; k < sv.num_weights && n < 4; ++k) {
                    const ufbx_skin_weight sw =
                        skin->weights.data[sv.weight_begin + k];
                    const int slot = clusterSlot(
                        skin->clusters.data[sw.cluster_index]);
                    if (slot > 255) continue;  // palette overflow guard
                    slots4[n] = slot;
                    w[n] = (float)sw.weight;
                    sum += w[n];
                    ++n;
                }
                if (n > 0 && sum > 1e-9f) {
                    int acc = 0;
                    for (int k = 0; k < n; ++k) {
                        int q = (int)lroundf(w[k] / sum * 255.0f);
                        if (k == n - 1) q = 255 - acc;
                        if (q < 0) q = 0;
                        if (q > 255) q = 255;
                        acc += q;
                        joints[k] = (unsigned char)slots4[k];
                        weights[k] = (unsigned char)q;
                    }
                } else {
                    joints[0] = (unsigned char)rigidSlot(inst.node);
                    weights[0] = 255;
                }
            } else {
                joints[0] = (unsigned char)rigidSlot(inst.node);
                weights[0] = 255;
            }
            part.joints.insert(part.joints.end(), joints, joints + 4);
            part.weights.insert(part.weights.end(), weights, weights + 4);
        }
        out.parts.push_back(std::move(part));
    }
    if (out.palette.size() > 256)
        out.warnings.push_back(
            "more than 256 matrix-palette slots - extra bones are skinned "
            "rigidly");

    // Clips: resample every stack's node transforms and keyframe-reduce.
    std::vector<std::string> takenNames;
    for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
        const ufbx_anim_stack* st = scene->anim_stacks.data[i];
        SkelClip clip;
        clip.name = clipName(st, takenNames);
        takenNames.push_back(clip.name);
        const double dur = st->time_end - st->time_begin;
        clip.duration = (float)(dur > 0.0 ? dur : 0.0);
        for (size_t ni = 0; ni < scene->nodes.count; ++ni)
            sampleNodeChannels(st->anim, scene->nodes.data[ni],
                               st->time_begin, dur, clip);
        out.clips.push_back(std::move(clip));
    }
    if (out.clips.empty()) {
        SkelClip clip;
        clip.name = "default";
        clip.duration = 0.0f;
        out.clips.push_back(std::move(clip));
    }

    // Pose AABB: union over every clip, sampled sparsely (6 Hz) - the same
    // conservative box .glb models get, used for culling and box collision.
    bool first = true;
    auto unionPose = [&](const ufbx_scene* ev) {
        for (const MaterialBatch& b : batches) {
            for (const SrcVertex& v : b.verts) {
                const ufbx_matrix m =
                    skinMatrix(ev, instances[(size_t)v.instance],
                               v.controlPoint);
                const ufbx_vec3 wp = ufbx_transform_position(
                    &m, {v.pos[0], v.pos[1], v.pos[2]});
                const float p[3] = {(float)wp.x, (float)wp.y, (float)wp.z};
                for (int c = 0; c < 3; ++c) {
                    if (first || p[c] < out.min[c]) out.min[c] = p[c];
                    if (first || p[c] > out.max[c]) out.max[c] = p[c];
                }
                first = false;
            }
        }
    };
    if (scene->anim_stacks.count == 0) {
        unionPose(scene);
    } else {
        for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
            const ufbx_anim_stack* st = scene->anim_stacks.data[i];
            const double dur = st->time_end - st->time_begin;
            const int samples = dur > 0.0 ? (int)llround(dur * 6.0) + 1 : 1;
            for (int f = 0; f < samples; ++f) {
                const double t =
                    samples > 1 ? dur * f / (samples - 1) : 0.0;
                ufbx_error err;
                ufbx_scene* ev = ufbx_evaluate_scene(
                    scene, st->anim, st->time_begin + t, nullptr, &err);
                if (!ev) continue;
                unionPose(ev);
                ufbx_free_scene(ev);
            }
        }
    }

    ufbx_free_scene(scene);
    return true;
}

int copyExternalTextures(const std::string& fbxPath,
                         const std::string& destDir) {
    std::string error;
    ufbx_scene* scene = loadScene(fbxPath, error);
    if (!scene) return 0;
    const std::filesystem::path srcDir =
        std::filesystem::path(fbxPath).parent_path();
    int copied = 0;
    for (size_t i = 0; i < scene->textures.count; ++i) {
        const ufbx_texture* tex = scene->textures.data[i];
        if (tex->content.size) continue;  // embedded: travels inside the .fbx
        std::error_code ec;
        std::filesystem::path cand(std::string(tex->absolute_filename.data,
                                               tex->absolute_filename.length));
        if (cand.empty() || !std::filesystem::exists(cand, ec)) {
            std::string rel(tex->filename.data, tex->filename.length);
            cand = srcDir / std::filesystem::path(rel).filename();
        }
        if (!std::filesystem::exists(cand, ec)) continue;
        const std::filesystem::path dest =
            std::filesystem::path(destDir) / cand.filename();
        if (std::filesystem::equivalent(cand, dest, ec)) continue;
        std::filesystem::copy_file(
            cand, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) ++copied;
    }
    ufbx_free_scene(scene);
    return copied;
}

}  // namespace fbxparser

// --- format dispatch ------------------------------------------------------

namespace animimport {
namespace {
bool isFbx(const std::string& path) {
    return path.size() > 4 &&
           _stricmp(path.c_str() + path.size() - 4, ".fbx") == 0;
}
}  // namespace

bool bake(const std::string& path, float fps, glbparser::Baked& out,
          std::string& error) {
    return isFbx(path) ? fbxparser::bake(path, fps, out, error)
                       : glbparser::bake(path, fps, out, error);
}

bool parseSkel(const std::string& path, glbparser::Skel& out,
               std::string& error) {
    return isFbx(path) ? fbxparser::parseSkel(path, out, error)
                       : glbparser::parseSkel(path, out, error);
}

}  // namespace animimport
