#include "placement.hpp"

#include <algorithm>
#include <cmath>

namespace placement {
namespace {

constexpr float kPi = 3.14159265358979f;

// Forward euler rotation (X, then Y, then Z) - the model matrix composition
// the viewport and the generated game both use.
void rotateEuler(const float* rotDeg, float& x, float& y, float& z) {
    const float d2r = kPi / 180.0f;
    {
        const float c = std::cos(rotDeg[0] * d2r), s = std::sin(rotDeg[0] * d2r);
        const float ny = y * c - z * s, nz = y * s + z * c;
        y = ny, z = nz;
    }
    {
        const float c = std::cos(rotDeg[1] * d2r), s = std::sin(rotDeg[1] * d2r);
        const float nx = x * c + z * s, nz = -x * s + z * c;
        x = nx, z = nz;
    }
    {
        const float c = std::cos(rotDeg[2] * d2r), s = std::sin(rotDeg[2] * d2r);
        const float nx = x * c - y * s, ny = x * s + y * c;
        x = nx, y = ny;
    }
}

// Rotation about Y alone (the model frame's content-forward correction),
// same sense as the Y step of rotateEuler.
void rotateY(float deg, float& x, float& z) {
    if (deg == 0.0f) return;
    const float c = std::cos(deg * kPi / 180.0f), s = std::sin(deg * kPi / 180.0f);
    const float nx = x * c + z * s, nz = -x * s + z * c;
    x = nx, z = nz;
}

// Do two boxes overlap on the ground plane? Footprints are shrunk by a hair so
// props standing edge to edge don't read as "one is under the other".
bool footprintsOverlap(const Aabb& a, const Aabb& b) {
    const float eps = 0.01f;
    return a.mx[0] - eps > b.mn[0] && a.mn[0] + eps < b.mx[0] &&
           a.mx[2] - eps > b.mn[2] && a.mn[2] + eps < b.mx[2];
}

}  // namespace

Aabb worldAabb(const SceneObject& o, const aobake::ModelAabbFn& modelAabb) {
    const CollisionBox b = collisionBox(o, modelAabb);
    Aabb out;
    for (int k = 0; k < 3; ++k) out.mn[k] = 1e30f, out.mx[k] = -1e30f;
    for (int corner = 0; corner < 8; ++corner) {
        float x = b.center[0] + ((corner & 1) ? b.half[0] : -b.half[0]);
        float y = b.center[1] + ((corner & 2) ? b.half[1] : -b.half[1]);
        float z = b.center[2] + ((corner & 4) ? b.half[2] : -b.half[2]);
        rotateY(b.yaw, x, z);
        rotateEuler(o.rotation, x, y, z);
        const float p[3] = {o.position[0] + x, o.position[1] + y,
                            o.position[2] + z};
        for (int k = 0; k < 3; ++k) {
            out.mn[k] = std::min(out.mn[k], p[k]);
            out.mx[k] = std::max(out.mx[k], p[k]);
        }
    }
    return out;
}

bool collides(const SceneObject& o) {
    if (o.collisionMode == 2) return false;  // "no collision" opts out
    switch (o.type) {
        // Everything with no geometry in the game. Kept as one list because
        // the generated runtime used to carry three copies of it by NUMBER
        // and they had already drifted - the scroller belt marker was missing
        // from the camera's, so an invisible belt origin shoved the boom.
        case PrimitiveType::SpawnPoint:
        case PrimitiveType::Player:
        case PrimitiveType::Emitter:
        case PrimitiveType::SoundEmitter:
        case PrimitiveType::PointLight:
        case PrimitiveType::Empty:
        case PrimitiveType::Decal:
        case PrimitiveType::Camera:
        case PrimitiveType::Area:
        case PrimitiveType::Scatter:
        case PrimitiveType::Scroller: return false;
        default: return true;
    }
}

CollisionBox collisionBox(const SceneObject& o,
                          const aobake::ModelAabbFn& modelAabb) {
    // The box before rotation: the unit primitive, or - for a model - the
    // MESH's own bounds, which is the whole reason this is not just the scale.
    // A model authored standing on its origin has a box sitting entirely above
    // it, and the unit cube around that origin is its ankles.
    float lmn[3] = {-0.5f, -0.5f, -0.5f}, lmx[3] = {0.5f, 0.5f, 0.5f};
    // A Vehicle asks the same callback as a Model, and for the same reason: the
    // unit cube around a car's origin is a box in the middle of the cabin. The
    // caller's ModelAabbFn resolves a Vehicle through its definition (only the
    // App knows those), so this needs no wider signature.
    if ((o.type == PrimitiveType::Model || o.type == PrimitiveType::Vehicle) && modelAabb) {
        float mn[3], mx[3];
        if (modelAabb(o, mn, mx))
            for (int k = 0; k < 3; ++k) lmn[k] = mn[k], lmx[k] = mx[k];
    }
    CollisionBox b;
    for (int k = 0; k < 3; ++k) {
        float a = lmn[k] * o.scale[k], c = lmx[k] * o.scale[k];
        if (a > c) std::swap(a, c);  // negative scale mirrors the box
        b.center[k] = 0.5f * (a + c);
        b.half[k] = 0.5f * (c - a);
    }
    // The content-forward correction turns the mesh between scale and
    // rotation, so it turns the box the same way. Only an animated model has
    // one (the property is not offered for anything else, and codegen emits 0
    // for the rest), which is why the mesh test rides along here.
    if (o.modelYawOffset != 0.0f && isAnimatedModelPath(o.modelPath))
        b.yaw = o.modelYawOffset;
    return b;
}

bool isSupport(const SceneObject& o) {
    if (o.collisionMode == 2) return false;  // "no collision" opts out
    switch (o.type) {
        case PrimitiveType::Box:
        case PrimitiveType::Sphere:
        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone:
        case PrimitiveType::Plane:
        case PrimitiveType::SavePoint:
        case PrimitiveType::Model:
            return true;
        default:
            return false;  // markers, lights, emitters, decals, mirrors, portals
    }
}

float restOffsetY(const SceneObject& o, const std::vector<SceneObject>& objects,
                  const std::vector<char>& skip,
                  const aobake::ModelAabbFn& modelAabb, const HeightFn& height,
                  float ceilingY) {
    const Aabb box = worldAabb(o, modelAabb);

    // Terrain under the footprint: the corners plus the center, inset so a
    // prop hanging half a texel over a cliff isn't lifted by the cliff.
    float support = -1e30f;
    if (height) {
        const float ix = std::min(0.05f * (box.mx[0] - box.mn[0]), 0.25f);
        const float iz = std::min(0.05f * (box.mx[2] - box.mn[2]), 0.25f);
        const float xs[3] = {box.mn[0] + ix, 0.5f * (box.mn[0] + box.mx[0]),
                             box.mx[0] - ix};
        const float zs[3] = {box.mn[2] + iz, 0.5f * (box.mn[2] + box.mx[2]),
                             box.mx[2] - iz};
        for (float x : xs)
            for (float z : zs) support = std::max(support, height(x, z));
    }

    // ...and the top of every solid object whose footprint it overlaps.
    for (size_t i = 0; i < objects.size(); ++i) {
        if (i < skip.size() && skip[i]) continue;
        if (&objects[i] == &o) continue;  // an in-scene object placing itself
        if (!isSupport(objects[i])) continue;
        const Aabb b = worldAabb(objects[i], modelAabb);
        if (b.mx[1] > ceilingY) continue;
        if (!footprintsOverlap(box, b)) continue;
        support = std::max(support, b.mx[1]);
    }

    if (support < -1e29f) return 0.0f;  // nothing under it at all
    return support - box.mn[1];
}

float restOffsetYGroup(const std::vector<SceneObject>& group,
                       const std::vector<SceneObject>& objects,
                       const std::vector<char>& skip,
                       const aobake::ModelAabbFn& modelAabb,
                       const HeightFn& height, float ceilingY) {
    float best = 0.0f;
    bool any = false;
    for (const SceneObject& g : group) {
        const float dy =
            restOffsetY(g, objects, skip, modelAabb, height, ceilingY);
        if (!any || dy > best) best = dy;
        any = true;
    }
    return any ? best : 0.0f;
}

}  // namespace placement
