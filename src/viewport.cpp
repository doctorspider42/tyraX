#include "viewport.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

#include "gl_loader.h"

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
uniform mat4 uMvp;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* FS = R"(#version 330 core
in vec3 vColor;
uniform vec3 uTint;
out vec4 FragColor;
void main() { FragColor = vec4(vColor * uTint, 1.0); }
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

float shadeOf(Vec3 n) {
    float d = n.x * gLightDir[0] + n.y * gLightDir[1] + n.z * gLightDir[2];
    if (d < 0.0f) d = 0.0f;
    float s = gAmbient + gDiffuse * d;
    return s > 1.0f ? 1.0f : s;
}

void pushShaded(std::vector<float>& v, Vec3 p, Vec3 n) {
    const float s = shadeOf(n);
    v.insert(v.end(), {p.x, p.y, p.z, s, s, s});
}

void pushQuadShaded(std::vector<float>& v, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n) {
    pushShaded(v, a, n);
    pushShaded(v, b, n);
    pushShaded(v, c, n);
    pushShaded(v, a, n);
    pushShaded(v, c, n);
    pushShaded(v, d, n);
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
        for (int sl = 0; sl < slices; ++sl) {
            const float p0 = 2 * kPi * sl / slices, p1 = 2 * kPi * (sl + 1) / slices;
            Vec3 v00 = at(t0, p0), v01 = at(t0, p1), v10 = at(t1, p0), v11 = at(t1, p1);
            auto n = [](Vec3 p) -> Vec3 { return {p.x * 2, p.y * 2, p.z * 2}; };
            pushShaded(v, v00, n(v00));
            pushShaded(v, v10, n(v10));
            pushShaded(v, v11, n(v11));
            pushShaded(v, v00, n(v00));
            pushShaded(v, v11, n(v11));
            pushShaded(v, v01, n(v01));
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
        pushShaded(v, {x0, -h, z0}, n0);
        pushShaded(v, {x0, h, z0}, n0);
        pushShaded(v, {x1, h, z1}, n1);
        pushShaded(v, {x0, -h, z0}, n0);
        pushShaded(v, {x1, h, z1}, n1);
        pushShaded(v, {x1, -h, z1}, n1);
        pushShaded(v, {0, h, 0}, {0, 1, 0});
        pushShaded(v, {x1, h, z1}, {0, 1, 0});
        pushShaded(v, {x0, h, z0}, {0, 1, 0});
        pushShaded(v, {0, -h, 0}, {0, -1, 0});
        pushShaded(v, {x0, -h, z0}, {0, -1, 0});
        pushShaded(v, {x1, -h, z1}, {0, -1, 0});
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
        pushShaded(v, {0, h, 0}, {nl * std::cos(am), ny, nl * std::sin(am)});
        pushShaded(v, {x1, -h, z1}, {nl * std::cos(a1), ny, nl * std::sin(a1)});
        pushShaded(v, {x0, -h, z0}, {nl * std::cos(a0), ny, nl * std::sin(a0)});
        pushShaded(v, {0, -h, 0}, {0, -1, 0});
        pushShaded(v, {x0, -h, z0}, {0, -1, 0});
        pushShaded(v, {x1, -h, z1}, {0, -1, 0});
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
                     float b) {
    v.insert(v.end(), {x, y, z, r, g, b});
}

}  // namespace

// ---------------------------------------------------------------------------

Viewport::Mesh Viewport::uploadMesh(const std::vector<float>& interleaved) {
    Mesh m;
    m.vertexCount = (int)(interleaved.size() / 6);
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(interleaved.size() * sizeof(float)),
                 interleaved.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
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
    destroyMesh(wireCube_);
    destroyMesh(skyQuad_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (colorTex_) glDeleteTextures(1, &colorTex_);
    if (depthRbo_) glDeleteRenderbuffers(1, &depthRbo_);
}

void Viewport::setTerrain(const TerrainConfig& terrain, int maxCells) {
    terrain_ = terrain;
    maxCells_ = maxCells < 1 ? 1 : maxCells;
    float diag = (float)(terrain_.width > terrain_.depth ? terrain_.width : terrain_.depth);
    distance_ = diag * 1.4f;
    buildTerrainMesh();
}

void Viewport::buildPrimitiveMeshes() {
    destroyMesh(box_);
    destroyMesh(sphere_);
    destroyMesh(cylinder_);
    destroyMesh(cone_);
    destroyMesh(spawnMarker_);
    destroyMesh(wireCube_);
    box_ = uploadMesh(unitBox());
    sphere_ = uploadMesh(unitSphere());
    cylinder_ = uploadMesh(unitCylinder());
    cone_ = uploadMesh(unitCone());
    spawnMarker_ = uploadMesh(unitSpawnMarker());
    wireCube_ = uploadMesh(unitWireCube());
}

void Viewport::buildTerrainMesh() {
    if (!program_) return;  // init() not called yet
    destroyMesh(terrain_mesh_);
    destroyMesh(lines_);

    const float w = (float)terrain_.width;
    const float d = (float)terrain_.depth;
    const float x0 = -w * 0.5f, z0 = -d * 0.5f;

    // Match the generated PS2 game: checker pattern, capped cell count.
    const int cellsX = terrain_.width > maxCells_ ? maxCells_ : terrain_.width;
    const int cellsZ = terrain_.depth > maxCells_ ? maxCells_ : terrain_.depth;
    const float sx = w / cellsX, sz = d / cellsZ;

    const float sUp = shadeOf({0, 1, 0});
    const float cA[3] = {96 / 255.0f * sUp, 160 / 255.0f * sUp, 72 / 255.0f * sUp};
    const float cB[3] = {74 / 255.0f * sUp, 128 / 255.0f * sUp, 56 / 255.0f * sUp};

    std::vector<float> tri;
    tri.reserve((size_t)cellsX * cellsZ * 6 * 6);
    for (int z = 0; z < cellsZ; ++z) {
        for (int x = 0; x < cellsX; ++x) {
            const float* c = ((x + z) % 2 == 0) ? cA : cB;
            float ax = x0 + x * sx, az = z0 + z * sz;
            float bx = ax + sx, bz = az + sz;
            pushVertexColor(tri, ax, 0, az, c[0], c[1], c[2]);
            pushVertexColor(tri, bx, 0, az, c[0], c[1], c[2]);
            pushVertexColor(tri, ax, 0, bz, c[0], c[1], c[2]);
            pushVertexColor(tri, bx, 0, az, c[0], c[1], c[2]);
            pushVertexColor(tri, bx, 0, bz, c[0], c[1], c[2]);
            pushVertexColor(tri, ax, 0, bz, c[0], c[1], c[2]);
        }
    }
    terrain_mesh_ = uploadMesh(tri);

    // Grid lines (cell borders) + world axes
    std::vector<float> lines;
    const float gc = 0.15f;  // grid line color (dark)
    for (int x = 0; x <= cellsX; ++x) {
        float px = x0 + x * sx;
        pushVertexColor(lines, px, 0.01f, z0, gc, gc, gc);
        pushVertexColor(lines, px, 0.01f, z0 + d, gc, gc, gc);
    }
    for (int z = 0; z <= cellsZ; ++z) {
        float pz = z0 + z * sz;
        pushVertexColor(lines, x0, 0.01f, pz, gc, gc, gc);
        pushVertexColor(lines, x0 + w, 0.01f, pz, gc, gc, gc);
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
    const Vec3 eye{distance_ * std::cos(pitch_) * std::cos(yaw_), distance_ * std::sin(pitch_),
                   distance_ * std::cos(pitch_) * std::sin(yaw_)};
    const Vec3 fwd = normalize(sub({0, 0, 0}, eye));
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

void Viewport::setLighting(const float* dir, float ambient, float diffuse) {
    float lx = dir[0], ly = dir[1], lz = dir[2];
    const float len = std::sqrt(lx * lx + ly * ly + lz * lz);
    if (len > 1e-5f) lx /= len, ly /= len, lz /= len;
    else lx = 0, ly = 1, lz = 0;
    gLightDir[0] = lx, gLightDir[1] = ly, gLightDir[2] = lz;
    gAmbient = ambient;
    gDiffuse = diffuse;
    if (program_) {
        buildPrimitiveMeshes();  // shade is baked into the unit meshes
        buildTerrainMesh();
    }
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
        glBindVertexArray(skyQuad_.vao);
        glDrawArrays(GL_TRIANGLES, 0, skyQuad_.vertexCount);
        glEnable(GL_DEPTH_TEST);
    }

    Vec3 eye{distance_ * std::cos(pitch_) * std::cos(yaw_), distance_ * std::sin(pitch_),
             distance_ * std::cos(pitch_) * std::sin(yaw_)};
    Mat4 view = lookAt(eye, {0, 0, 0}, {0, 1, 0});
    float diag = (float)(terrain_.width > terrain_.depth ? terrain_.width : terrain_.depth);
    Mat4 proj = perspective(50.0f * kPi / 180.0f, (float)width / (float)height, 0.1f,
                            diag * 10.0f + 100.0f);
    Mat4 viewProj = mul(proj, view);
    for (int i = 0; i < 16; ++i) {
        viewM_[i] = view.m[i];
        projM_[i] = proj.m[i];
    }

    glUseProgram(program_);

    auto draw = [&](const Mesh& mesh, GLenum mode, const Mat4& mvp, float r, float g,
                    float b) {
        glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp.m);
        glUniform3f(uTint_, r, g, b);
        glBindVertexArray(mesh.vao);
        glDrawArrays(mode, 0, mesh.vertexCount);
    };

    auto meshFor = [&](const SceneObject& o) -> const Mesh* {
        switch (o.type) {
            case PrimitiveType::Sphere: return &sphere_;
            case PrimitiveType::Cylinder: return &cylinder_;
            case PrimitiveType::Cone: return &cone_;
            case PrimitiveType::SpawnPoint: return &spawnMarker_;
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
        draw(terrain_mesh_, GL_TRIANGLES, viewProj, tintScale, tintScale, tintScale);
        for (const SceneObject& o : objects) {
            const Mat4 mvp = mul(viewProj, modelMatrix(o));
            draw(*meshFor(o), GL_TRIANGLES, mvp, o.color[0] * tintScale,
                 o.color[1] * tintScale, o.color[2] * tintScale);
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
    if (selectedIndex >= 0 && selectedIndex < (int)objects.size()) {
        const Mat4 mvp = mul(viewProj, modelMatrix(objects[selectedIndex]));
        draw(wireCube_, GL_LINES, mvp, 1.0f, 0.6f, 0.1f);
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return colorTex_;
}
