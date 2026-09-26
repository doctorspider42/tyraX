#include "blobshadowbake.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

#include "decalproj.hpp"
#include "fbxparser.hpp"

#include <stb_image_write.h>

namespace blobshadowbake {
namespace {

constexpr int kSize = 128;

bool triangles(const Project& project, const SceneObject& object,
               std::vector<float>& out, std::string& error) {
    if (object.type == PrimitiveType::Model &&
        isAnimatedModelPath(object.modelPath)) {
        glbparser::Baked model;
        if (!animimport::bake(project.filePath(object.modelPath), 1.0f, model,
                              error))
            return false;
        for (const glbparser::Part& part : model.parts) {
            const size_t count = std::min(part.positions.size(),
                                          (size_t)part.vertexCount * 3);
            out.insert(out.end(), part.positions.begin(),
                       part.positions.begin() + count);
        }
        if (out.empty()) error = "The animated model has no triangles";
        return !out.empty();
    }

    // decalproj already owns the exact primitive and OBJ tessellation. Remove
    // the instance transform: the runtime turns the baked footprint by the
    // object's yaw and sizes the quad from the same authored/model bounds.
    SceneObject local = object;
    for (int axis = 0; axis < 3; ++axis) {
        local.position[axis] = 0.0f;
        local.rotation[axis] = 0.0f;
        local.scale[axis] = 1.0f;
    }
    if (!decalproj::objectTriangles(project, local, out)) {
        error = "This object has no drawable mesh to bake";
        return false;
    }
    return true;
}

}  // namespace

bool canBake(const SceneObject& object) {
    switch (object.type) {
        case PrimitiveType::Box:
        case PrimitiveType::Sphere:
        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone:
        case PrimitiveType::Plane:
        case PrimitiveType::SavePoint:
            return true;
        case PrimitiveType::Model:
            return !object.modelPath.empty();
        default:
            return false;
    }
}

bool bake(const Project& project, const SceneObject& object,
          const std::string& projectRelativePng, float footprint[2],
          std::string& error) {
    error.clear();
    if (!canBake(object)) {
        error = "This object has no drawable mesh to bake";
        return false;
    }
    std::vector<float> tri;
    if (!triangles(project, object, tri, error)) return false;

    float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
    for (size_t i = 0; i + 2 < tri.size(); i += 3) {
        minX = std::min(minX, tri[i]); maxX = std::max(maxX, tri[i]);
        minZ = std::min(minZ, tri[i + 2]); maxZ = std::max(maxZ, tri[i + 2]);
    }
    const float dx = maxX - minX, dz = maxZ - minZ;
    if (!(dx > 1e-5f) || !(dz > 1e-5f)) {
        error = "The top-down mesh footprint has no area";
        return false;
    }
    footprint[0] = dx;
    footprint[1] = dz;

    std::vector<unsigned char> mask((size_t)kSize * kSize, 0);
    constexpr float pad = 4.0f;
    auto px = [&](float x) { return pad + (x - minX) / dx * (kSize - 1 - 2 * pad); };
    auto py = [&](float z) { return pad + (z - minZ) / dz * (kSize - 1 - 2 * pad); };
    auto edge = [](float ax, float ay, float bx, float by, float x, float y) {
        return (x - ax) * (by - ay) - (y - ay) * (bx - ax);
    };
    for (size_t i = 0; i + 8 < tri.size(); i += 9) {
        const float x0 = px(tri[i]), y0 = py(tri[i + 2]);
        const float x1 = px(tri[i + 3]), y1 = py(tri[i + 5]);
        const float x2 = px(tri[i + 6]), y2 = py(tri[i + 8]);
        const float area = edge(x0, y0, x1, y1, x2, y2);
        if (std::fabs(area) < 1e-5f) continue;
        const int xa = std::max(0, (int)std::floor(std::min({x0, x1, x2})));
        const int xb = std::min(kSize - 1, (int)std::ceil(std::max({x0, x1, x2})));
        const int ya = std::max(0, (int)std::floor(std::min({y0, y1, y2})));
        const int yb = std::min(kSize - 1, (int)std::ceil(std::max({y0, y1, y2})));
        for (int y = ya; y <= yb; ++y)
            for (int x = xa; x <= xb; ++x) {
                const float fx = x + 0.5f, fy = y + 0.5f;
                const float a = edge(x0, y0, x1, y1, fx, fy);
                const float b = edge(x1, y1, x2, y2, fx, fy);
                const float c = edge(x2, y2, x0, y0, fx, fy);
                if ((a >= 0 && b >= 0 && c >= 0) ||
                    (a <= 0 && b <= 0 && c <= 0))
                    mask[(size_t)y * kSize + x] = 255;
            }
    }
    std::vector<unsigned char> tmp(mask.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x) {
                int sum = 0, n = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int qx = x + ox, qy = y + oy;
                        if (qx < 0 || qx >= kSize || qy < 0 || qy >= kSize) continue;
                        sum += mask[(size_t)qy * kSize + qx]; ++n;
                    }
                tmp[(size_t)y * kSize + x] = (unsigned char)(sum / n);
            }
        mask.swap(tmp);
    }
    std::vector<unsigned char> rgba((size_t)kSize * kSize * 4, 255);
    for (size_t i = 0; i < mask.size(); ++i) rgba[i * 4 + 3] = mask[i];

    const std::filesystem::path dst(project.filePath(projectRelativePng));
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        error = "Could not create the blob-shadow folder: " + ec.message();
        return false;
    }
    if (!stbi_write_png(dst.string().c_str(), kSize, kSize, 4, rgba.data(),
                        kSize * 4)) {
        error = "Could not write " + dst.string();
        return false;
    }
    return true;
}

}  // namespace blobshadowbake
