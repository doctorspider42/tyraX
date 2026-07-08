#include "viewport.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

#include <filesystem>

#include "gl_loader.h"
#include "objparser.hpp"
#include <stb_image.h>

// ---------------------------------------------------------------------------
// Minimal matrix math (column-major, OpenGL style)
// ---------------------------------------------------------------------------
namespace {

constexpr float kPi = 3.14159265358979f;

struct Mat4 {
    float m[16];
};

Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

Mat4 perspective(float fovyRad, float aspect, float zNear, float zFar) {
    const float f = 1.0f / std::tan(fovyRad / 2.0f);
    Mat4 r{};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

Mat4 translation(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

Mat4 scaleM(float x, float y, float z) {
    Mat4 r = identity();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}

Mat4 rotX(float a) {
    Mat4 r = identity();
    const float c = std::cos(a), s = std::sin(a);
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

Mat4 rotY(float a) {
    Mat4 r = identity();
    const float c = std::cos(a), s = std::sin(a);
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

Mat4 rotZ(float a) {
    Mat4 r = identity();
    const float c = std::cos(a), s = std::sin(a);
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}

// Same composition as the generated PS2 code: scale -> rotX -> rotY -> rotZ -> translate
Mat4 modelMatrix(const SceneObject& o) {
    const float d2r = kPi / 180.0f;
    Mat4 m = scaleM(o.scale[0], o.scale[1], o.scale[2]);
    m = mul(rotX(o.rotation[0] * d2r), m);
    m = mul(rotY(o.rotation[1] * d2r), m);
    m = mul(rotZ(o.rotation[2] * d2r), m);
    m = mul(translation(o.position[0], o.position[1], o.position[2]), m);
    return m;
}

struct Vec3 {
    float x, y, z;
};
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 normalize(Vec3 v) {
    float len = std::sqrt(dot(v, v));
    if (len < 1e-8f) return {0, 0, 0};
    return {v.x / len, v.y / len, v.z / len};
}

Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = normalize(sub(center, eye));
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r{};
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    r.m[15] = 1.0f;
    return r;
}

const char* VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aUV;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec3 vColor;
out vec2 vUV;
out vec3 vWorld;
void main() {
    vColor = aColor;
    vUV = aUV;
    vWorld = (uModel * vec4(aPos, 1.0)).xyz;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

// Point lights are added on top of the baked directional shade, mirroring the
// game's pointLightAt() bake: (1-d/r)^2 falloff * N.L, clamped to 1 with the
// rest of the shade. Normals come from screen-space derivatives (flat, like
// the PS2's flat shading), so the shared unit meshes need no extra data.
const char* FS = R"(#version 330 core
in vec3 vColor;
in vec2 vUV;
in vec3 vWorld;
uniform vec3 uTint;
uniform int uUseTex;
uniform sampler2D uTex;
uniform int uLit;                // 0: lines/markers/sky - skip point lights
uniform int uLightCount;
uniform vec4 uLightPos[8];       // xyz = world position, w = radius
uniform vec4 uLightCol[8];       // rgb = color, w = brightness
out vec4 FragColor;
void main() {
    vec3 tex = uUseTex != 0 ? texture(uTex, vUV).rgb : vec3(1.0);
    vec3 shade = vColor;
    if (uLit != 0 && uLightCount > 0) {
        vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
        vec3 add = vec3(0.0);
        for (int i = 0; i < uLightCount; ++i) {
            vec3 d = uLightPos[i].xyz - vWorld;
            float dist = length(d);
            float radius = uLightPos[i].w;
            if (dist >= radius) continue;
            float atten = 1.0 - dist / radius;
            atten *= atten;
            float ndotl = dist > 0.0001 ? max(dot(n, d / dist), 0.0) : 1.0;
            add += uLightCol[i].rgb * (uLightCol[i].w * atten * ndotl);
        }
        shade = min(shade + add, vec3(1.0));
    }
    FragColor = vec4(shade * uTint * tex, 1.0);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader compile error: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

// ---------------------------------------------------------------------------
// Primitive mesh generation. Unit shapes fit a 1x1x1 cube centered at origin;
// a fake directional light is baked into the (grayscale) vertex colors so the
// per-object tint comes from the uTint uniform. Mirrors the generated PS2 code.
// ---------------------------------------------------------------------------

// Directional light parameters (match the generated PS2 code)
float gLightDir[3] = {0.37f, 0.82f, 0.44f};
float gAmbient = 0.55f;
float gDiffuse = 0.45f;
float gLightColor[3] = {1.0f, 1.0f, 1.0f};
float gBrightness = 1.0f;

// Per-channel multipliers: brightness * (ambient + diffuse*d*lightColor)
Vec3 shadeOf(Vec3 n) {
    float d = n.x * gLightDir[0] + n.y * gLightDir[1] + n.z * gLightDir[2];
    if (d < 0.0f) d = 0.0f;
    const float base = gDiffuse * d;
    Vec3 s = {gBrightness * (gAmbient + base * gLightColor[0]),
              gBrightness * (gAmbient + base * gLightColor[1]),
              gBrightness * (gAmbient + base * gLightColor[2])};
    if (s.x > 1.0f) s.x = 1.0f;
    if (s.y > 1.0f) s.y = 1.0f;
    if (s.z > 1.0f) s.z = 1.0f;
    return s;
}

// Vertex layout: pos(3) + color(3) + uv(2)
void pushShaded(std::vector<float>& v, Vec3 p, Vec3 n, float tu = 0, float tv = 0) {
    const Vec3 s = shadeOf(n);
    v.insert(v.end(), {p.x, p.y, p.z, s.x, s.y, s.z, tu, tv});
}

void pushQuadShaded(std::vector<float>& v, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n) {
    pushShaded(v, a, n, 0, 0);
    pushShaded(v, b, n, 1, 0);
    pushShaded(v, c, n, 1, 1);
    pushShaded(v, a, n, 0, 0);
    pushShaded(v, c, n, 1, 1);
    pushShaded(v, d, n, 0, 1);
}

std::vector<float> unitBox() {
    std::vector<float> v;
    const float h = 0.5f;
    pushQuadShaded(v, {h, -h, -h}, {h, h, -h}, {h, h, h}, {h, -h, h}, {1, 0, 0});
    pushQuadShaded(v, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-h, -h, -h}, {-1, 0, 0});
    pushQuadShaded(v, {-h, h, -h}, {-h, h, h}, {h, h, h}, {h, h, -h}, {0, 1, 0});
    pushQuadShaded(v, {-h, -h, h}, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {0, -1, 0});
    pushQuadShaded(v, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});
    pushQuadShaded(v, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0, 0, -1});
    return v;
}

std::vector<float> unitSphere() {
    std::vector<float> v;
    const int stacks = 12, slices = 18;
    const float r = 0.5f;
    auto at = [&](float t, float p) -> Vec3 {
        return {r * std::sin(t) * std::cos(p), r * std::cos(t), r * std::sin(t) * std::sin(p)};
    };
    for (int st = 0; st < stacks; ++st) {
        const float t0 = kPi * st / stacks, t1 = kPi * (st + 1) / stacks;
        const float tv0 = (float)st / stacks, tv1 = (float)(st + 1) / stacks;
        for (int sl = 0; sl < slices; ++sl) {
            const float p0 = 2 * kPi * sl / slices, p1 = 2 * kPi * (sl + 1) / slices;
            const float tu0 = (float)sl / slices, tu1 = (float)(sl + 1) / slices;
            Vec3 v00 = at(t0, p0), v01 = at(t0, p1), v10 = at(t1, p0), v11 = at(t1, p1);
            auto n = [](Vec3 p) -> Vec3 { return {p.x * 2, p.y * 2, p.z * 2}; };
            pushShaded(v, v00, n(v00), tu0, tv0);
            pushShaded(v, v10, n(v10), tu0, tv1);
            pushShaded(v, v11, n(v11), tu1, tv1);
            pushShaded(v, v00, n(v00), tu0, tv0);
            pushShaded(v, v11, n(v11), tu1, tv1);
            pushShaded(v, v01, n(v01), tu1, tv0);
        }
    }
    return v;
}

std::vector<float> unitCylinder() {
    std::vector<float> v;
    const int seg = 24;
    const float r = 0.5f, h = 0.5f;
    for (int i = 0; i < seg; ++i) {
        const float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        const float x0 = r * std::cos(a0), z0 = r * std::sin(a0);
        const float x1 = r * std::cos(a1), z1 = r * std::sin(a1);
        const Vec3 n0 = {std::cos(a0), 0, std::sin(a0)};
        const Vec3 n1 = {std::cos(a1), 0, std::sin(a1)};
        const float u0 = (float)i / seg, u1 = (float)(i + 1) / seg;
        pushShaded(v, {x0, -h, z0}, n0, u0, 1);
        pushShaded(v, {x0, h, z0}, n0, u0, 0);
        pushShaded(v, {x1, h, z1}, n1, u1, 0);
        pushShaded(v, {x0, -h, z0}, n0, u0, 1);
        pushShaded(v, {x1, h, z1}, n1, u1, 0);
        pushShaded(v, {x1, -h, z1}, n1, u1, 1);
        pushShaded(v, {0, h, 0}, {0, 1, 0}, 0.5f, 0.5f);
        pushShaded(v, {x1, h, z1}, {0, 1, 0}, x1 + 0.5f, z1 + 0.5f);
        pushShaded(v, {x0, h, z0}, {0, 1, 0}, x0 + 0.5f, z0 + 0.5f);
        pushShaded(v, {0, -h, 0}, {0, -1, 0}, 0.5f, 0.5f);
        pushShaded(v, {x0, -h, z0}, {0, -1, 0}, x0 + 0.5f, z0 + 0.5f);
        pushShaded(v, {x1, -h, z1}, {0, -1, 0}, x1 + 0.5f, z1 + 0.5f);
    }
    return v;
}

std::vector<float> unitCone() {
    std::vector<float> v;
    const int seg = 24;
    const float r = 0.5f, h = 0.5f;
    const float nl = 0.894f, ny = 0.447f;
    for (int i = 0; i < seg; ++i) {
        const float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        const float am = (a0 + a1) * 0.5f;
        const float x0 = r * std::cos(a0), z0 = r * std::sin(a0);
        const float x1 = r * std::cos(a1), z1 = r * std::sin(a1);
        const float u0 = (float)i / seg, u1 = (float)(i + 1) / seg;
        pushShaded(v, {0, h, 0}, {nl * std::cos(am), ny, nl * std::sin(am)},
                   (u0 + u1) * 0.5f, 0);
        pushShaded(v, {x1, -h, z1}, {nl * std::cos(a1), ny, nl * std::sin(a1)}, u1, 1);
        pushShaded(v, {x0, -h, z0}, {nl * std::cos(a0), ny, nl * std::sin(a0)}, u0, 1);
        pushShaded(v, {0, -h, 0}, {0, -1, 0}, 0.5f, 0.5f);
        pushShaded(v, {x0, -h, z0}, {0, -1, 0}, x0 + 0.5f, z0 + 0.5f);
        pushShaded(v, {x1, -h, z1}, {0, -1, 0}, x1 + 0.5f, z1 + 0.5f);
    }
    return v;
}

// Spawn point marker: a pole with an arrow pointing +Z (the facing direction,
// i.e. object yaw = player start yaw in the FPP template).
std::vector<float> unitSpawnMarker() {
    std::vector<float> v;
    auto cuboid = [&](Vec3 c, Vec3 h) {
        pushQuadShaded(v, {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z},
                       {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
                       {1, 0, 0});
        pushQuadShaded(v, {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
                       {c.x - h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y - h.y, c.z - h.z},
                       {-1, 0, 0});
        pushQuadShaded(v, {c.x - h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
                       {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x + h.x, c.y + h.y, c.z - h.z},
                       {0, 1, 0});
        pushQuadShaded(v, {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x - h.x, c.y - h.y, c.z - h.z},
                       {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
                       {0, -1, 0});
        pushQuadShaded(v, {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
                       {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
                       {0, 0, 1});
        pushQuadShaded(v, {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x - h.x, c.y - h.y, c.z - h.z},
                       {c.x - h.x, c.y + h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z},
                       {0, 0, -1});
    };
    cuboid({0, -0.05f, 0}, {0.06f, 0.45f, 0.06f});   // vertical pole
    cuboid({0, 0.1f, 0.15f}, {0.04f, 0.04f, 0.15f});  // arrow shaft (+Z)
    // arrowhead pyramid, apex at +Z
    const Vec3 apex{0, 0.1f, 0.5f};
    const Vec3 b0{-0.12f, -0.02f, 0.3f}, b1{0.12f, -0.02f, 0.3f};
    const Vec3 b2{0.12f, 0.22f, 0.3f}, b3{-0.12f, 0.22f, 0.3f};
    pushShaded(v, apex, {0, -1, 0.4f});
    pushShaded(v, b1, {0, -1, 0.4f});
    pushShaded(v, b0, {0, -1, 0.4f});
    pushShaded(v, apex, {1, 0, 0.4f});
    pushShaded(v, b2, {1, 0, 0.4f});
    pushShaded(v, b1, {1, 0, 0.4f});
    pushShaded(v, apex, {0, 1, 0.4f});
    pushShaded(v, b3, {0, 1, 0.4f});
    pushShaded(v, b2, {0, 1, 0.4f});
    pushShaded(v, apex, {-1, 0, 0.4f});
    pushShaded(v, b0, {-1, 0, 0.4f});
    pushShaded(v, b3, {-1, 0, 0.4f});
    pushQuadShaded(v, b0, b1, b2, b3, {0, 0, -1});  // pyramid base
    return v;
}

// Player marker: a simple humanoid with a nose pointing +Z (start yaw).
std::vector<float> unitPlayerMarker() {
    std::vector<float> v;
    auto cuboid = [&](Vec3 c, Vec3 h) {
        pushQuadShaded(v, {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z},
                       {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
                       {1, 0, 0});
        pushQuadShaded(v, {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
                       {c.x - h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y - h.y, c.z - h.z},
                       {-1, 0, 0});
        pushQuadShaded(v, {c.x - h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
                       {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x + h.x, c.y + h.y, c.z - h.z},
                       {0, 1, 0});
        pushQuadShaded(v, {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x - h.x, c.y - h.y, c.z - h.z},
                       {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
                       {0, -1, 0});
        pushQuadShaded(v, {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
                       {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
                       {0, 0, 1});
        pushQuadShaded(v, {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x - h.x, c.y - h.y, c.z - h.z},
                       {c.x - h.x, c.y + h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z},
                       {0, 0, -1});
    };
    // proportions of a ~1.8-unit figure standing on y=0
    cuboid({0, 0.35f, 0}, {0.12f, 0.35f, 0.09f});    // legs
    cuboid({0, 1.05f, 0}, {0.22f, 0.35f, 0.12f});    // torso
    cuboid({0, 1.58f, 0}, {0.13f, 0.15f, 0.13f});    // head
    cuboid({0, 1.58f, 0.16f}, {0.04f, 0.04f, 0.05f});  // nose (facing +Z)
    return v;
}

std::vector<float> unitWireCube() {
    std::vector<float> v;
    const float h = 0.52f;  // slightly larger than the shape, avoids z-fighting
    const float c[8][3] = {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h},
                           {-h, h, -h},  {h, h, -h},  {h, h, h},  {-h, h, h}};
    const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                          {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& edge : e)
        for (int k = 0; k < 2; ++k)
            v.insert(v.end(),
                     {c[edge[k]][0], c[edge[k]][1], c[edge[k]][2], 1.0f, 1.0f, 1.0f});
    return v;
}

void pushVertexColor(std::vector<float>& v, float x, float y, float z, float r, float g,
                     float b, float tu = 0, float tv = 0) {
    v.insert(v.end(), {x, y, z, r, g, b, tu, tv});
}

// Point-light bulb: a small sphere with flat white vertex colors so the draw
// tint (the light color) shows through unshaded - i.e. it reads as emissive.
std::vector<float> unitLightBulb() {
    std::vector<float> v;
    const int stacks = 8, slices = 12;
    const float r = 0.5f;
    auto at = [&](float t, float p) -> Vec3 {
        return {r * std::sin(t) * std::cos(p), r * std::cos(t), r * std::sin(t) * std::sin(p)};
    };
    auto push = [&](Vec3 p) { pushVertexColor(v, p.x, p.y, p.z, 1.0f, 1.0f, 1.0f); };
    for (int st = 0; st < stacks; ++st) {
        const float t0 = kPi * st / stacks, t1 = kPi * (st + 1) / stacks;
        for (int sl = 0; sl < slices; ++sl) {
            const float p0 = 2 * kPi * sl / slices, p1 = 2 * kPi * (sl + 1) / slices;
            Vec3 v00 = at(t0, p0), v01 = at(t0, p1), v10 = at(t1, p0), v11 = at(t1, p1);
            push(v00); push(v10); push(v11);
            push(v00); push(v11); push(v01);
        }
    }
    return v;
}

// Unit-radius line sphere (three great-circle rings) drawn scaled to a light's
// radius to show its reach. White vertex colors; tinted at draw time.
std::vector<float> unitWireSphere() {
    std::vector<float> v;
    const int seg = 32;
    auto ring = [&](int axis) {
        for (int i = 0; i < seg; ++i) {
            const float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
            const float c0 = std::cos(a0), s0 = std::sin(a0);
            const float c1 = std::cos(a1), s1 = std::sin(a1);
            Vec3 p0, p1;
            if (axis == 0) p0 = {0, c0, s0}, p1 = {0, c1, s1};        // YZ plane
            else if (axis == 1) p0 = {c0, 0, s0}, p1 = {c1, 0, s1};   // XZ plane
            else p0 = {c0, s0, 0}, p1 = {c1, s1, 0};                  // XY plane
            pushVertexColor(v, p0.x, p0.y, p0.z, 1, 1, 1);
            pushVertexColor(v, p1.x, p1.y, p1.z, 1, 1, 1);
        }
    };
    ring(0); ring(1); ring(2);
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------

Viewport::Mesh Viewport::uploadMesh(const std::vector<float>& interleaved) {
    Mesh m;
    m.vertexCount = (int)(interleaved.size() / 8);
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(interleaved.size() * sizeof(float)),
                 interleaved.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void*)(6 * sizeof(float)));
    glBindVertexArray(0);
    return m;
}

void Viewport::destroyMesh(Mesh& m) {
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    m = Mesh{};
}

bool Viewport::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, VS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "program link error: %s\n", log);
        return false;
    }
    uMvp_ = glGetUniformLocation(program_, "uMvp");
    uTint_ = glGetUniformLocation(program_, "uTint");
    uUseTex_ = glGetUniformLocation(program_, "uUseTex");
    uModel_ = glGetUniformLocation(program_, "uModel");
    uLit_ = glGetUniformLocation(program_, "uLit");
    uLightCount_ = glGetUniformLocation(program_, "uLightCount");
    uLightPos_ = glGetUniformLocation(program_, "uLightPos");
    uLightCol_ = glGetUniformLocation(program_, "uLightCol");

    buildTerrainMesh();
    buildPrimitiveMeshes();
    return true;
}

void Viewport::shutdown() {
    if (program_) glDeleteProgram(program_);
    destroyMesh(terrain_mesh_);
    destroyMesh(lines_);
    destroyMesh(box_);
    destroyMesh(sphere_);
    destroyMesh(cylinder_);
    destroyMesh(cone_);
    destroyMesh(spawnMarker_);
    destroyMesh(playerMarker_);
    destroyMesh(wireCube_);
    destroyMesh(lightGizmo_);
    destroyMesh(wireSphere_);
    destroyMesh(skyQuad_);
    clearModelCache();
    clearTexCache();
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (colorTex_) glDeleteTextures(1, &colorTex_);
    if (depthRbo_) glDeleteRenderbuffers(1, &depthRbo_);
}

void Viewport::setTerrain(const TerrainConfig& terrain, int maxCells,
                          const std::vector<float>& heights, int hmW, int hmD) {
    const bool sameTerrain = terrain_.width == terrain.width &&
                             terrain_.depth == terrain.depth && maxCells_ == maxCells;
    terrain_ = terrain;
    maxCells_ = maxCells < 1 ? 1 : maxCells;
    heights_ = heights;
    hmW_ = hmW;
    hmD_ = hmD;
    if (!sameTerrain) {
        float diag =
            (float)(terrain_.width > terrain_.depth ? terrain_.width : terrain_.depth);
        distance_ = diag * 1.4f;
        target_[0] = target_[1] = target_[2] = 0.0f;  // re-center on the terrain
    }
    buildTerrainMesh();
}

float Viewport::terrainHeight(float x, float z) const {
    if (hmW_ < 2 || hmD_ < 2 || (int)heights_.size() != hmW_ * hmD_) return 0.0f;
    const float w = (float)terrain_.width, d = (float)terrain_.depth;
    float gx = (x + w * 0.5f) / w * (hmW_ - 1);
    float gz = (z + d * 0.5f) / d * (hmD_ - 1);
    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    if (gx > hmW_ - 1.001f) gx = hmW_ - 1.001f;
    if (gz > hmD_ - 1.001f) gz = hmD_ - 1.001f;
    const int ix = (int)gx, iz = (int)gz;
    const float fx = gx - ix, fz = gz - iz;
    auto h = [&](int a, int b) { return heights_[(size_t)b * hmW_ + a]; };
    const float top = h(ix, iz) * (1 - fx) + h(ix + 1, iz) * fx;
    const float bottom = h(ix, iz + 1) * (1 - fx) + h(ix + 1, iz + 1) * fx;
    return top * (1 - fz) + bottom * fz;
}

bool Viewport::terrainRaycast(float u, float v, float& outX, float& outZ) const {
    if (fbWidth_ < 1 || fbHeight_ < 1) return false;

    // Same camera ray construction as pick()
    const Vec3 tgt{target_[0], target_[1], target_[2]};
    const Vec3 eye{tgt.x + distance_ * std::cos(pitch_) * std::cos(yaw_),
                   tgt.y + distance_ * std::sin(pitch_),
                   tgt.z + distance_ * std::cos(pitch_) * std::sin(yaw_)};
    const Vec3 fwd = normalize(sub(tgt, eye));
    const Vec3 right = normalize(cross(fwd, {0, 1, 0}));
    const Vec3 up = cross(right, fwd);
    const float aspect = (float)fbWidth_ / (float)fbHeight_;
    const float th = std::tan(50.0f * kPi / 180.0f * 0.5f);
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = 1.0f - v * 2.0f;
    const Vec3 dir = normalize({fwd.x + right.x * ndcX * th * aspect + up.x * ndcY * th,
                                fwd.y + right.y * ndcX * th * aspect + up.y * ndcY * th,
                                fwd.z + right.z * ndcX * th * aspect + up.z * ndcY * th});

    // Raymarch the heightfield: find the first step below the surface, then
    // refine by bisection.
    const float maxDist = distance_ * 4.0f;
    const int steps = 400;
    const float dt = maxDist / steps;
    float prevT = 0.0f;
    float prevDelta = eye.y - terrainHeight(eye.x, eye.z);
    for (int i = 1; i <= steps; ++i) {
        const float t = i * dt;
        const float px = eye.x + dir.x * t, py = eye.y + dir.y * t,
                    pz = eye.z + dir.z * t;
        const float delta = py - terrainHeight(px, pz);
        if (delta <= 0.0f && prevDelta > 0.0f) {
            float lo = prevT, hi = t;
            for (int k = 0; k < 16; ++k) {
                const float mid = (lo + hi) * 0.5f;
                const float mx = eye.x + dir.x * mid, my = eye.y + dir.y * mid,
                            mz = eye.z + dir.z * mid;
                if (my - terrainHeight(mx, mz) > 0.0f) lo = mid;
                else hi = mid;
            }
            const float t2 = (lo + hi) * 0.5f;
            outX = eye.x + dir.x * t2;
            outZ = eye.z + dir.z * t2;
            // only hits on the terrain rectangle count
            return outX >= -terrain_.width * 0.5f && outX <= terrain_.width * 0.5f &&
                   outZ >= -terrain_.depth * 0.5f && outZ <= terrain_.depth * 0.5f;
        }
        prevT = t;
        prevDelta = delta;
    }
    return false;
}

void Viewport::buildPrimitiveMeshes() {
    destroyMesh(box_);
    destroyMesh(sphere_);
    destroyMesh(cylinder_);
    destroyMesh(cone_);
    destroyMesh(spawnMarker_);
    destroyMesh(playerMarker_);
    destroyMesh(wireCube_);
    destroyMesh(lightGizmo_);
    destroyMesh(wireSphere_);
    box_ = uploadMesh(unitBox());
    sphere_ = uploadMesh(unitSphere());
    cylinder_ = uploadMesh(unitCylinder());
    cone_ = uploadMesh(unitCone());
    spawnMarker_ = uploadMesh(unitSpawnMarker());
    playerMarker_ = uploadMesh(unitPlayerMarker());
    wireCube_ = uploadMesh(unitWireCube());
    lightGizmo_ = uploadMesh(unitLightBulb());
    wireSphere_ = uploadMesh(unitWireSphere());
}

void Viewport::buildTerrainMesh() {
    if (!program_) return;  // init() not called yet
    destroyMesh(terrain_mesh_);
    destroyMesh(lines_);

    const float w = (float)terrain_.width;
    const float d = (float)terrain_.depth;
    const float x0 = -w * 0.5f, z0 = -d * 0.5f;

    // Match the generated PS2 game: checker pattern, capped cell count,
    // vertex heights from the sculpted heightmap.
    const int cellsX = terrain_.width > maxCells_ ? maxCells_ : terrain_.width;
    const int cellsZ = terrain_.depth > maxCells_ ? maxCells_ : terrain_.depth;
    const float sx = w / cellsX, sz = d / cellsZ;

    const bool hasHeights =
        hmW_ == cellsX + 1 && hmD_ == cellsZ + 1 && (int)heights_.size() == hmW_ * hmD_;
    auto hAt = [&](int ix, int iz) {
        if (!hasHeights) return 0.0f;
        ix = ix < 0 ? 0 : ix > hmW_ - 1 ? hmW_ - 1 : ix;
        iz = iz < 0 ? 0 : iz > hmD_ - 1 ? hmD_ - 1 : iz;
        return heights_[(size_t)iz * hmW_ + ix];
    };
    auto shadeAt = [&](int ix, int iz) -> Vec3 {
        Vec3 n = {hAt(ix - 1, iz) - hAt(ix + 1, iz), 2.0f * (sx < sz ? sx : sz),
                  hAt(ix, iz - 1) - hAt(ix, iz + 1)};
        return shadeOf(normalize(n));
    };

    // Textured terrain modulates the texture with a neutral shade only.
    const bool texturedTerrain = !terrainTexture_.empty();
    const float cA[3] = {texturedTerrain ? 1.0f : 96 / 255.0f,
                         texturedTerrain ? 1.0f : 160 / 255.0f,
                         texturedTerrain ? 1.0f : 72 / 255.0f};
    const float cB[3] = {texturedTerrain ? 1.0f : 74 / 255.0f,
                         texturedTerrain ? 1.0f : 128 / 255.0f,
                         texturedTerrain ? 1.0f : 56 / 255.0f};
    const float ts = terrainTexScale_;

    std::vector<float> tri;
    tri.reserve((size_t)cellsX * cellsZ * 6 * 8);
    for (int z = 0; z < cellsZ; ++z) {
        for (int x = 0; x < cellsX; ++x) {
            const float* c = ((x + z) % 2 == 0) ? cA : cB;
            float ax = x0 + x * sx, az = z0 + z * sz;
            float bx = ax + sx, bz = az + sz;
            const float h00 = hAt(x, z), h10 = hAt(x + 1, z);
            const float h01 = hAt(x, z + 1), h11 = hAt(x + 1, z + 1);
            const Vec3 s00 = shadeAt(x, z), s10 = shadeAt(x + 1, z);
            const Vec3 s01 = shadeAt(x, z + 1), s11 = shadeAt(x + 1, z + 1);
            pushVertexColor(tri, ax, h00, az, c[0] * s00.x, c[1] * s00.y, c[2] * s00.z,
                            ax / ts, az / ts);
            pushVertexColor(tri, bx, h10, az, c[0] * s10.x, c[1] * s10.y, c[2] * s10.z,
                            bx / ts, az / ts);
            pushVertexColor(tri, ax, h01, bz, c[0] * s01.x, c[1] * s01.y, c[2] * s01.z,
                            ax / ts, bz / ts);
            pushVertexColor(tri, bx, h10, az, c[0] * s10.x, c[1] * s10.y, c[2] * s10.z,
                            bx / ts, az / ts);
            pushVertexColor(tri, bx, h11, bz, c[0] * s11.x, c[1] * s11.y, c[2] * s11.z,
                            bx / ts, bz / ts);
            pushVertexColor(tri, ax, h01, bz, c[0] * s01.x, c[1] * s01.y, c[2] * s01.z,
                            ax / ts, bz / ts);
        }
    }
    terrain_mesh_ = uploadMesh(tri);

    // Grid lines (cell borders, following the relief) + world axes
    std::vector<float> lines;
    const float gc = 0.15f;  // grid line color (dark)
    for (int x = 0; x <= cellsX; ++x) {
        const float px = x0 + x * sx;
        for (int z = 0; z < cellsZ; ++z) {
            pushVertexColor(lines, px, hAt(x, z) + 0.02f, z0 + z * sz, gc, gc, gc);
            pushVertexColor(lines, px, hAt(x, z + 1) + 0.02f, z0 + (z + 1) * sz, gc, gc, gc);
        }
    }
    for (int z = 0; z <= cellsZ; ++z) {
        const float pz = z0 + z * sz;
        for (int x = 0; x < cellsX; ++x) {
            pushVertexColor(lines, x0 + x * sx, hAt(x, z) + 0.02f, pz, gc, gc, gc);
            pushVertexColor(lines, x0 + (x + 1) * sx, hAt(x + 1, z) + 0.02f, pz, gc, gc, gc);
        }
    }
    // Axes: X red, Y green, Z blue (slightly above terrain)
    float axisLen = (w > d ? w : d) * 0.6f;
    pushVertexColor(lines, 0, 0.02f, 0, 0.9f, 0.2f, 0.2f);
    pushVertexColor(lines, axisLen, 0.02f, 0, 0.9f, 0.2f, 0.2f);
    pushVertexColor(lines, 0, 0.02f, 0, 0.2f, 0.9f, 0.2f);
    pushVertexColor(lines, 0, axisLen * 0.5f, 0, 0.2f, 0.9f, 0.2f);
    pushVertexColor(lines, 0, 0.02f, 0, 0.3f, 0.4f, 1.0f);
    pushVertexColor(lines, 0, 0.02f, axisLen, 0.3f, 0.4f, 1.0f);
    lines_ = uploadMesh(lines);
}

void Viewport::ensureFramebuffer(int width, int height) {
    if (fbo_ && width == fbWidth_ && height == fbHeight_) return;
    fbWidth_ = width;
    fbHeight_ = height;

    if (!fbo_) glGenFramebuffers(1, &fbo_);
    if (!colorTex_) glGenTextures(1, &colorTex_);
    if (!depthRbo_) glGenRenderbuffers(1, &depthRbo_);

    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "viewport framebuffer incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

namespace {

// Applies the inverse of the object's euler rotation (rotZ, rotY, rotX with
// negated angles - the reverse of the model matrix composition).
Vec3 rotateInverse(Vec3 v, const float* rotDeg) {
    const float d2r = kPi / 180.0f;
    {
        const float c = std::cos(-rotDeg[2] * d2r), s = std::sin(-rotDeg[2] * d2r);
        const float x = v.x * c - v.y * s, y = v.x * s + v.y * c;
        v.x = x, v.y = y;
    }
    {
        const float c = std::cos(-rotDeg[1] * d2r), s = std::sin(-rotDeg[1] * d2r);
        const float x = v.x * c + v.z * s, z = -v.x * s + v.z * c;
        v.x = x, v.z = z;
    }
    {
        const float c = std::cos(-rotDeg[0] * d2r), s = std::sin(-rotDeg[0] * d2r);
        const float y = v.y * c - v.z * s, z = v.y * s + v.z * c;
        v.y = y, v.z = z;
    }
    return v;
}

// Ray vs unit AABB [-0.5, 0.5]^3 slab test; returns hit distance or -1.
float rayUnitBox(Vec3 o, Vec3 d) {
    float t0 = 0.0001f, t1 = 1e9f;
    const float* op = &o.x;
    const float* dp = &d.x;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(dp[axis]) < 1e-8f) {
            if (op[axis] < -0.5f || op[axis] > 0.5f) return -1.0f;
            continue;
        }
        float ta = (-0.5f - op[axis]) / dp[axis];
        float tb = (0.5f - op[axis]) / dp[axis];
        if (ta > tb) std::swap(ta, tb);
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return -1.0f;
    }
    return t0;
}

}  // namespace

int Viewport::pick(float u, float v, const std::vector<SceneObject>& objects) const {
    if (fbWidth_ < 1 || fbHeight_ < 1) return -1;

    // Camera ray through the pixel (same camera setup as render())
    const Vec3 tgt{target_[0], target_[1], target_[2]};
    const Vec3 eye{tgt.x + distance_ * std::cos(pitch_) * std::cos(yaw_),
                   tgt.y + distance_ * std::sin(pitch_),
                   tgt.z + distance_ * std::cos(pitch_) * std::sin(yaw_)};
    const Vec3 fwd = normalize(sub(tgt, eye));
    const Vec3 right = normalize(cross(fwd, {0, 1, 0}));
    const Vec3 up = cross(right, fwd);
    const float aspect = (float)fbWidth_ / (float)fbHeight_;
    const float th = std::tan(50.0f * kPi / 180.0f * 0.5f);
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = 1.0f - v * 2.0f;
    const Vec3 dir = normalize({fwd.x + right.x * ndcX * th * aspect + up.x * ndcY * th,
                                fwd.y + right.y * ndcX * th * aspect + up.y * ndcY * th,
                                fwd.z + right.z * ndcX * th * aspect + up.z * ndcY * th});

    int best = -1;
    float bestT = 1e9f;
    for (size_t i = 0; i < objects.size(); ++i) {
        const SceneObject& o = objects[i];
        // Transform the ray into the object's unit-box space
        Vec3 lo = rotateInverse(sub(eye, {o.position[0], o.position[1], o.position[2]}),
                                o.rotation);
        Vec3 ld = rotateInverse(dir, o.rotation);
        lo = {lo.x / o.scale[0], lo.y / o.scale[1], lo.z / o.scale[2]};
        ld = {ld.x / o.scale[0], ld.y / o.scale[1], ld.z / o.scale[2]};

        const float t = rayUnitBox(lo, ld);
        if (t > 0.0f && t < bestT) {
            bestT = t;
            best = (int)i;
        }
    }
    return best;
}

void Viewport::setLighting(const float* dir, float ambient, float diffuse,
                           const float* color, float brightness) {
    float lx = dir[0], ly = dir[1], lz = dir[2];
    const float len = std::sqrt(lx * lx + ly * ly + lz * lz);
    if (len > 1e-5f) lx /= len, ly /= len, lz /= len;
    else lx = 0, ly = 1, lz = 0;
    gLightDir[0] = lx, gLightDir[1] = ly, gLightDir[2] = lz;
    gAmbient = ambient;
    gDiffuse = diffuse;
    gLightColor[0] = color[0], gLightColor[1] = color[1], gLightColor[2] = color[2];
    gBrightness = brightness;
    if (program_) {
        buildPrimitiveMeshes();  // shade is baked into the unit meshes
        buildTerrainMesh();
        clearModelCache();  // model shading is baked too
    }
}

void Viewport::setProjectDir(const std::string& dir) {
    if (projectDir_ == dir) return;
    projectDir_ = dir;
    clearModelCache();
    clearTexCache();
}

void Viewport::setTerrainTexture(const std::string& relPath, float scale) {
    if (terrainTexture_ == relPath && terrainTexScale_ == scale) return;
    terrainTexture_ = relPath;
    terrainTexScale_ = scale < 0.25f ? 0.25f : scale;
    if (program_) buildTerrainMesh();
}

void Viewport::clearTexCache() {
    for (auto& [path, tex] : texCache_)
        if (tex) glDeleteTextures(1, &tex);
    texCache_.clear();
}

uint32_t Viewport::glTexture(const std::string& relPath) {
    if (relPath.empty()) return 0;
    auto it = texCache_.find(relPath);
    if (it != texCache_.end()) return it->second;

    GLuint tex = 0;
    const std::string full = (std::filesystem::path(projectDir_) / relPath).string();
    int w = 0, h = 0, comp = 0;
    if (unsigned char* pixels = stbi_load(full.c_str(), &w, &h, &comp, 4)) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        stbi_image_free(pixels);
    }
    texCache_[relPath] = tex;  // 0 is cached too (missing/unreadable)
    return tex;
}

void Viewport::clearModelCache() {
    for (auto& [path, mesh] : modelCache_) destroyMesh(mesh);
    modelCache_.clear();
}

const Viewport::Mesh* Viewport::modelMesh(const std::string& relPath) {
    if (relPath.empty()) return nullptr;
    auto it = modelCache_.find(relPath);
    if (it != modelCache_.end()) return it->second.vao ? &it->second : nullptr;

    std::vector<float> posNormalUv;
    Mesh mesh;  // stays empty on failure - negative result is cached too
    if (objparser::load((std::filesystem::path(projectDir_) / relPath).string(),
                        posNormalUv)) {
        std::vector<float> interleaved;
        interleaved.reserve(posNormalUv.size());
        for (size_t i = 0; i + 7 < posNormalUv.size(); i += 8) {
            const Vec3 s =
                shadeOf({posNormalUv[i + 3], posNormalUv[i + 4], posNormalUv[i + 5]});
            interleaved.insert(interleaved.end(),
                               {posNormalUv[i], posNormalUv[i + 1], posNormalUv[i + 2], s.x,
                                s.y, s.z, posNormalUv[i + 6], posNormalUv[i + 7]});
        }
        mesh = uploadMesh(interleaved);
    }
    modelCache_[relPath] = mesh;
    return mesh.vao ? &modelCache_[relPath] : nullptr;
}

void Viewport::setSky(const float* horizonRgb, const float* topRgb, bool gradient) {
    for (int i = 0; i < 3; ++i) {
        sky_[i] = horizonRgb[i];
        skyTop_[i] = topRgb[i];
    }
    skyGradient_ = gradient;
    skyQuadDirty_ = true;
}

void Viewport::orbit(float dx, float dy) {
    yaw_ += dx * 0.01f;
    pitch_ += dy * 0.01f;
    if (pitch_ < 0.05f) pitch_ = 0.05f;
    if (pitch_ > 1.5f) pitch_ = 1.5f;
}

void Viewport::zoom(float wheel) {
    distance_ *= (wheel > 0 ? 0.9f : 1.1f);
    if (distance_ < 2.0f) distance_ = 2.0f;
    if (distance_ > 2000.0f) distance_ = 2000.0f;
}

void Viewport::pan(float dx, float dy) {
    // Slide the orbit target in the view plane; speed scales with distance
    // so a pixel of drag covers the same fraction of the screen at any zoom.
    const Vec3 eye{distance_ * std::cos(pitch_) * std::cos(yaw_),
                   distance_ * std::sin(pitch_),
                   distance_ * std::cos(pitch_) * std::sin(yaw_)};
    const Vec3 fwd = normalize(sub({0, 0, 0}, eye));
    const Vec3 right = normalize(cross(fwd, {0, 1, 0}));
    const Vec3 up = cross(right, fwd);
    const float s = distance_ * 0.0016f;
    target_[0] += (-right.x * dx + up.x * dy) * s;
    target_[1] += (-right.y * dx + up.y * dy) * s;
    target_[2] += (-right.z * dx + up.z * dy) * s;
}

void Viewport::fly(float forward, float strafe, float dt) {
    // WASD: move the orbit target on the horizontal plane along the camera
    // heading. Speed scales with zoom so travel feels constant on screen.
    if (forward == 0.0f && strafe == 0.0f) return;
    const Vec3 fwdH{-std::cos(yaw_), 0.0f, -std::sin(yaw_)};  // toward the scene
    const Vec3 rightH{-fwdH.z, 0.0f, fwdH.x};
    const float s = distance_ * 0.9f * dt;
    target_[0] += (fwdH.x * forward + rightH.x * strafe) * s;
    target_[2] += (fwdH.z * forward + rightH.z * strafe) * s;
}

uint32_t Viewport::render(int width, int height, const std::vector<SceneObject>& objects,
                          int selectedIndex) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    ensureFramebuffer(width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width, height);
    glClearColor(sky_[0], sky_[1], sky_[2], 1.0f);  // sky, matches the PS2 clear color
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Gradient sky (matches the PS2 sky dome): fullscreen quad, no depth
    if (skyGradient_) {
        if (skyQuadDirty_) {
            skyQuadDirty_ = false;
            destroyMesh(skyQuad_);
            std::vector<float> q;
            pushVertexColor(q, -1, -1, 0, sky_[0], sky_[1], sky_[2]);
            pushVertexColor(q, 1, -1, 0, sky_[0], sky_[1], sky_[2]);
            pushVertexColor(q, 1, 1, 0, skyTop_[0], skyTop_[1], skyTop_[2]);
            pushVertexColor(q, -1, -1, 0, sky_[0], sky_[1], sky_[2]);
            pushVertexColor(q, 1, 1, 0, skyTop_[0], skyTop_[1], skyTop_[2]);
            pushVertexColor(q, -1, 1, 0, skyTop_[0], skyTop_[1], skyTop_[2]);
            skyQuad_ = uploadMesh(q);
        }
        glDisable(GL_DEPTH_TEST);
        glUseProgram(program_);
        Mat4 id = identity();
        glUniformMatrix4fv(uMvp_, 1, GL_FALSE, id.m);
        glUniform3f(uTint_, 1.0f, 1.0f, 1.0f);
        glUniform1i(uLit_, 0);
        glBindVertexArray(skyQuad_.vao);
        glDrawArrays(GL_TRIANGLES, 0, skyQuad_.vertexCount);
        glEnable(GL_DEPTH_TEST);
    }

    const Vec3 tgt{target_[0], target_[1], target_[2]};
    Vec3 eye{tgt.x + distance_ * std::cos(pitch_) * std::cos(yaw_),
             tgt.y + distance_ * std::sin(pitch_),
             tgt.z + distance_ * std::cos(pitch_) * std::sin(yaw_)};
    Mat4 view = lookAt(eye, tgt, {0, 1, 0});
    float diag = (float)(terrain_.width > terrain_.depth ? terrain_.width : terrain_.depth);
    Mat4 proj = perspective(50.0f * kPi / 180.0f, (float)width / (float)height, 0.1f,
                            diag * 10.0f + 100.0f);
    Mat4 viewProj = mul(proj, view);
    for (int i = 0; i < 16; ++i) {
        viewM_[i] = view.m[i];
        projM_[i] = proj.m[i];
    }

    glUseProgram(program_);

    // Point lights in the scene -> fragment shader uniforms (live preview of
    // what the game bakes into vertex colors; capped at the shader's 8).
    {
        float pos[8 * 4] = {};
        float col[8 * 4] = {};
        int count = 0;
        for (const SceneObject& o : objects) {
            if (o.type != PrimitiveType::PointLight || count >= 8) continue;
            pos[count * 4 + 0] = o.position[0];
            pos[count * 4 + 1] = o.position[1];
            pos[count * 4 + 2] = o.position[2];
            pos[count * 4 + 3] = o.lightRadius > 0.01f ? o.lightRadius : 0.01f;
            col[count * 4 + 0] = o.color[0];
            col[count * 4 + 1] = o.color[1];
            col[count * 4 + 2] = o.color[2];
            col[count * 4 + 3] = o.lightBright;
            ++count;
        }
        glUniform1i(uLightCount_, count);
        glUniform4fv(uLightPos_, 8, pos);
        glUniform4fv(uLightCol_, 8, col);
    }
    const Mat4 identityM = identity();

    auto draw = [&](const Mesh& mesh, GLenum mode, const Mat4& mvp, float r, float g,
                    float b, uint32_t texture = 0, const Mat4* model = nullptr) {
        glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp.m);
        glUniformMatrix4fv(uModel_, 1, GL_FALSE, model ? model->m : identityM.m);
        glUniform1i(uLit_, model ? 1 : 0);  // world matrix given = lit geometry
        glUniform3f(uTint_, r, g, b);
        glUniform1i(uUseTex_, texture ? 1 : 0);
        if (texture) glBindTexture(GL_TEXTURE_2D, texture);
        glBindVertexArray(mesh.vao);
        glDrawArrays(mode, 0, mesh.vertexCount);
    };

    auto meshFor = [&](const SceneObject& o) -> const Mesh* {
        switch (o.type) {
            case PrimitiveType::Sphere: return &sphere_;
            case PrimitiveType::Cylinder: return &cylinder_;
            case PrimitiveType::Cone: return &cone_;
            case PrimitiveType::SpawnPoint: return &spawnMarker_;
            case PrimitiveType::Player: return &playerMarker_;
            case PrimitiveType::Emitter: return &cone_;  // flame-ish marker
            case PrimitiveType::SoundEmitter: return &sphere_;  // speaker-ish marker
            case PrimitiveType::PointLight: return &lightGizmo_;  // glowing bulb
            case PrimitiveType::Model: {
                const Mesh* m = modelMesh(o.modelPath);
                return m ? m : &box_;  // missing model -> placeholder box
            }
            default: return &box_;
        }
    };

    // One pass over terrain + objects, filled or as wireframe.
    // tintScale darkens the overlay wires in SolidWireframe mode.
    auto scenePass = [&](bool asLines, float tintScale) {
        glPolygonMode(GL_FRONT_AND_BACK, asLines ? GL_LINE : GL_FILL);
        if (!asLines) {
            // push filled geometry back so wires and grid lines win the z-test
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
        }
        const uint32_t terrainTex =
            asLines ? 0 : glTexture(terrainTexture_);  // wire passes stay untextured
        draw(terrain_mesh_, GL_TRIANGLES, viewProj, tintScale, tintScale, tintScale,
             terrainTex, asLines ? nullptr : &identityM);
        for (const SceneObject& o : objects) {
            const Mat4 model = modelMatrix(o);
            const Mat4 mvp = mul(viewProj, model);
            const uint32_t tex = asLines ? 0 : glTexture(o.texturePath);
            // the bulb gizmo stays emissive - everything else receives light
            const bool lit = !asLines && o.type != PrimitiveType::PointLight;
            draw(*meshFor(o), GL_TRIANGLES, mvp, o.color[0] * tintScale,
                 o.color[1] * tintScale, o.color[2] * tintScale, tex,
                 lit ? &model : nullptr);
        }
        if (!asLines) glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    };

    switch (viewMode_) {
        case ViewMode::Wireframe: scenePass(true, 1.0f); break;
        case ViewMode::SolidWireframe:
            scenePass(false, 1.0f);
            scenePass(true, 0.15f);
            break;
        default: scenePass(false, 1.0f); break;
    }

    // Grid lines, axes and the selection outline are unaffected by view mode
    draw(lines_, GL_LINES, viewProj, 1.0f, 1.0f, 1.0f);

    // Point-light reach: a ring sphere at each light, scaled to its radius and
    // tinted with the light color (a rough preview of the lit volume).
    for (const SceneObject& o : objects) {
        if (o.type != PrimitiveType::PointLight) continue;
        const float r = o.lightRadius > 0.01f ? o.lightRadius : 0.01f;
        const Mat4 m = mul(translation(o.position[0], o.position[1], o.position[2]),
                           scaleM(r, r, r));
        draw(wireSphere_, GL_LINES, mul(viewProj, m), o.color[0], o.color[1], o.color[2]);
    }

    if (selectedIndex >= 0 && selectedIndex < (int)objects.size()) {
        const Mat4 mvp = mul(viewProj, modelMatrix(objects[selectedIndex]));
        draw(wireCube_, GL_LINES, mvp, 1.0f, 0.6f, 0.1f);
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return colorTex_;
}
