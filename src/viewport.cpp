#include "viewport.hpp"

#include "fbxparser.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>

#include <filesystem>

#include "gl_loader.h"
#include "objparser.hpp"
#include "primmesh.hpp"
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
// (+ the animated-model content-forward correction between scale and rotation,
// mirroring the game's modelYaw pre-rotation - keep the two in sync).
Mat4 modelMatrix(const SceneObject& o) {
    const float d2r = kPi / 180.0f;
    Mat4 m = scaleM(o.scale[0], o.scale[1], o.scale[2]);
    if (o.modelYawOffset != 0.0f && isAnimatedModelPath(o.modelPath))
        m = mul(rotY(o.modelYawOffset * d2r), m);  // avatars included
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
uniform int uAlpha;              // 1: honor texture alpha (decal cutout + blend)
uniform float uOpacity;          // constant alpha multiplier (mirror glass)
uniform int uLit;                // 0: lines/markers/sky - skip point lights
uniform int uLightCount;
uniform vec4 uLightPos[8];       // xyz = world position, w = radius
uniform vec4 uLightCol[8];       // rgb = color, w = brightness
uniform int uFogOn;              // GS hardware fog preview (lit geometry only)
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uFogEye;            // camera position (world) - also flashlight
uniform vec3 uFogFwd;            // camera forward (world, normalized)
uniform int uFlashOn;            // camera flashlight preview (VU1 twin)
uniform vec3 uFlashCol;
uniform float uFlashInvR2;       // 1/range^2
uniform float uFlashCut2;        // cos^2(half-angle)
uniform float uFlashSoft;        // softness/(range^2*(1-cos^2))
uniform int uReflOn;             // refl pass: 0 off, 1 sphere map, 2 live sky
uniform sampler2D uRefl;         // sphere map, texture unit 1
uniform float uReflStrength;     // additive gain, 1.0 = full chrome
uniform vec3 uReflSkyHorizon;    // "@sky" dynamic env: scene sky colors
uniform vec3 uReflSkyTop;
uniform int uReflRounded;        // refl -rounded: centroid-radial normals
uniform vec3 uReflCenter;        // world-space centroid for the rounded mode
out vec4 FragColor;
void main() {
    vec4 texel = uUseTex != 0 ? texture(uTex, vUV) : vec4(1.0);
    vec3 tex = texel.rgb;
    // Decals honor the texture's alpha (mirrors the PS2 alpha test + blend);
    // fully transparent texels are dropped so the surface behind shows through.
    float a = uAlpha != 0 ? texel.a : 1.0;
    if (uAlpha != 0 && a < 0.02) discard;
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
    if (uFlashOn != 0 && uLit != 0) {
        // Camera flashlight - the exact per-vertex formula the PS2 runs on
        // VU1 (CalculateTyraSpotLight): cone + distance falloff, no N.L.
        vec3 d = vWorld - uFogEye;
        float dist2 = dot(d, d);
        float t = max(dot(d, uFogFwd), 0.0);
        float cone = clamp((t * t - uFlashCut2 * dist2) * uFlashSoft, 0.0, 1.0);
        float axial = clamp(1.0 - dist2 * uFlashInvR2, 0.0, 1.0);
        shade = min(shade + uFlashCol * (cone * axial), vec3(1.0));
    }
    vec3 color = shade * uTint * tex;
    if (uFogOn != 0 && uLit != 0) {
        // View-plane distance, same metric as the PS2 (clip-space W); the
        // sky is excluded like the game's fogDisabled sky dome bag.
        float viewDist = dot(vWorld - uFogEye, uFogFwd);
        float f = clamp((uFogEnd - viewDist) / (uFogEnd - uFogStart), 0.0, 1.0);
        color = mix(uFogColor, color, f);
    }
    if (uReflOn != 0) {
        // Spherical environment map (refl): matcap UVs from the camera-space
        // normal, added on top - the GL twin of the PS2's additive second
        // pass (Cs*FIX + Cd; that bag is fogDisabled, hence after the fog
        // mix). Flat normals from derivatives, exactly like the PS2 pass
        // built on the loader's per-face normals - keep the two in sync.
        // "-rounded": normals radiate from the part centroid instead, so a
        // flat face sweeps a gradient of the map (PS2 twin: the rebuild
        // overwrites envNormals with normalize(vertex - centroid)).
        vec3 n = uReflRounded != 0
            ? normalize(vWorld - uReflCenter)
            : normalize(cross(dFdx(vWorld), dFdy(vWorld)));
        vec3 r = normalize(cross(uFogFwd, vec3(0.0, 1.0, 0.0)));
        vec3 u = cross(r, uFogFwd);
        vec2 st = vec2(0.5 + 0.5 * dot(n, r), 0.5 - 0.5 * dot(n, u));
        // "@sky" dynamic mode: approximate the PS2's per-frame sky-dome
        // render with the analytic horizon/zenith gradient (st.y 0 = up).
        vec3 env = uReflOn == 2
            ? mix(uReflSkyHorizon, uReflSkyTop,
                  clamp(1.0 - 2.0 * st.y, 0.0, 1.0))
            : texture(uRefl, st).rgb;
        color += uReflStrength * env;
    }
    // Decals carry the texture's alpha (cutout above + blend here), mirror
    // glass a constant opacity; everything else outputs opaque.
    FragColor = vec4(color, a * uOpacity);
}
)";

// Color grading post pass: the editor twin of the PS2 GS grading sprites
// (RendererCorePostFx::gradingQuads). Works on 0..255 integers with a clamp
// after every step, exactly like the GS blender, so the preview and the
// console output match. Fullscreen triangle from gl_VertexID (no VBO).
const char* GRADE_VS = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* GRADE_FS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec3 uGain;      // 0..255, 128 = 1x (GS: Cd * FIX >> 7)
uniform vec3 uLiftPos;   // 0..255 added
uniform vec3 uLiftNeg;   // 0..255 subtracted
uniform vec3 uMixColor;  // 0..255 mix target
uniform float uMixAmt;   // 0..128, 128 = full replace
out vec4 FragColor;
void main() {
    vec3 c = floor(texture(uSrc, vUV).rgb * 255.0 + 0.5);
    c = clamp(floor(c * uGain / 128.0), 0.0, 255.0);
    c = clamp(c + uLiftPos, 0.0, 255.0);
    c = clamp(c - uLiftNeg, 0.0, 255.0);
    c = clamp(c + floor((uMixColor - c) * uMixAmt / 128.0), 0.0, 255.0);
    FragColor = vec4(c / 255.0, 1.0);
}
)";

// Particle-preview shader: unlit, per-vertex RGBA (alpha-blended quads),
// optional texture modulation - matches how the PS2 draws emitter quads.
const char* PART_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
uniform mat4 uMvp;
out vec4 vColor;
out vec2 vUV;
void main() {
    vColor = aColor;
    vUV = aUV;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* PART_FS = R"(#version 330 core
in vec4 vColor;
in vec2 vUV;
uniform int uUseTex;
uniform sampler2D uTex;
out vec4 FragColor;
void main() {
    vec4 tex = uUseTex != 0 ? texture(uTex, vUV) : vec4(1.0);
    FragColor = vColor * tex;
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

// The box/sphere/cylinder/cone/plane tessellation now lives in primmesh (shared
// with the decal projector so decals conform to exactly this geometry). primmesh
// emits raw pos+normal+uv; bake the directional shade into the color slot here
// (shadeOf reads the vertex normal) to get the pos+color+uv layout the GL shader
// wants. Geometry and UVs are byte-identical to the old local generators; only
// the middle three floats go normal -> shade.
std::vector<float> shadedMesh(std::vector<float> m) {
    for (size_t i = 0; i + 7 < m.size(); i += 8) {
        const Vec3 s = shadeOf({m[i + 3], m[i + 4], m[i + 5]});
        m[i + 3] = s.x;
        m[i + 4] = s.y;
        m[i + 5] = s.z;
    }
    return m;
}
std::vector<float> unitBox(int detail = kDefaultBoxDetail) {
    return shadedMesh(primmesh::unitBox(detail));
}
std::vector<float> unitSphere(int detail = kDefaultPrimDetail) {
    return shadedMesh(primmesh::unitSphere(detail));
}
std::vector<float> unitCylinder(int detail = kDefaultPrimDetail) {
    return shadedMesh(primmesh::unitCylinder(detail));
}
std::vector<float> unitCone(int detail = kDefaultPrimDetail) {
    return shadedMesh(primmesh::unitCone(detail));
}
std::vector<float> unitPlane() { return shadedMesh(primmesh::unitPlane()); }

// Decal quad: XY plane facing +Z, nudged +Z by 0.02 (matches addDecal in the
// codegen). U runs with local -X (slide-projector convention) so a texture reads
// correctly - not mirrored - viewed from the +Z front; V with +Y. Same as the
// game's addDecal and the projected decal, so the preview matches the console.
std::vector<float> unitDecal() {
    std::vector<float> v;
    const float h = 0.5f, z = 0.02f;
    const Vec3 n = {0, 0, 1};
    pushShaded(v, {-h, -h, z}, n, 1, 0);
    pushShaded(v, {h, -h, z}, n, 0, 0);
    pushShaded(v, {h, h, z}, n, 0, 1);
    pushShaded(v, {-h, -h, z}, n, 1, 0);
    pushShaded(v, {h, h, z}, n, 0, 1);
    pushShaded(v, {-h, h, z}, n, 1, 1);
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

// Camera entity marker: a classic film-camera body with the lens barrel
// pointing +Z (the shot direction - same convention as spawn/player markers).
std::vector<float> unitCameraBody() {
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
    cuboid({0, 0, -0.08f}, {0.20f, 0.15f, 0.24f});      // body
    cuboid({0, 0, 0.24f}, {0.08f, 0.08f, 0.10f});       // lens barrel (+Z)
    cuboid({-0.11f, 0.24f, -0.12f}, {0.03f, 0.09f, 0.09f});  // film reels
    cuboid({0.11f, 0.24f, -0.12f}, {0.03f, 0.09f, 0.09f});
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
            v.insert(v.end(), {c[edge[k]][0], c[edge[k]][1], c[edge[k]][2], 1.0f, 1.0f,
                               1.0f, 0.0f, 0.0f});  // uploadMesh expects pos+color+uv
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

// Camera FOV frustum wireframe: apex at the origin opening toward +Z, built
// for a 45-degree half-angle at 4:3. Drawn scaled by tan(fov/2) in X/Y and by
// the display length in Z, so one mesh previews any FOV. White vertex colors;
// tinted at draw time.
std::vector<float> unitCameraFrustum() {
    std::vector<float> v;
    const float a = 4.0f / 3.0f;  // the PS2 output aspect
    const float c[4][3] = {{-a, -1, 1}, {a, -1, 1}, {a, 1, 1}, {-a, 1, 1}};
    auto line = [&](const float* p0, const float* p1) {
        pushVertexColor(v, p0[0], p0[1], p0[2], 1, 1, 1);
        pushVertexColor(v, p1[0], p1[1], p1[2], 1, 1, 1);
    };
    const float apex[3] = {0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        line(apex, c[i]);              // edges from the lens
        line(c[i], c[(i + 1) % 4]);    // far rectangle
    }
    // a small "up" tick on the far top edge so roll reads at a glance
    const float t0[3] = {0, 1, 1}, t1[3] = {0, 1.25f, 1};
    line(t0, t1);
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

// The particle-shader layout (pos3 + rgba4 + uv2): terrain layer passes carry
// the painted weight in the vertex alpha, exactly like the PS2's color stream.
Viewport::Mesh Viewport::uploadMesh9(const std::vector<float>& interleaved) {
    Mesh m;
    m.vertexCount = (int)(interleaved.size() / 9);
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(interleaved.size() * sizeof(float)),
                 interleaved.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                          (void*)(7 * sizeof(float)));
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
    uAlpha_ = glGetUniformLocation(program_, "uAlpha");
    uOpacity_ = glGetUniformLocation(program_, "uOpacity");
    uModel_ = glGetUniformLocation(program_, "uModel");
    uLit_ = glGetUniformLocation(program_, "uLit");
    uLightCount_ = glGetUniformLocation(program_, "uLightCount");
    uLightPos_ = glGetUniformLocation(program_, "uLightPos");
    uLightCol_ = glGetUniformLocation(program_, "uLightCol");
    uFogOn_ = glGetUniformLocation(program_, "uFogOn");
    uFogColor_ = glGetUniformLocation(program_, "uFogColor");
    uFogStart_ = glGetUniformLocation(program_, "uFogStart");
    uFogEnd_ = glGetUniformLocation(program_, "uFogEnd");
    uFogEye_ = glGetUniformLocation(program_, "uFogEye");
    uFogFwd_ = glGetUniformLocation(program_, "uFogFwd");
    uFlashOn_ = glGetUniformLocation(program_, "uFlashOn");
    uFlashCol_ = glGetUniformLocation(program_, "uFlashCol");
    uFlashInvR2_ = glGetUniformLocation(program_, "uFlashInvR2");
    uFlashCut2_ = glGetUniformLocation(program_, "uFlashCut2");
    uFlashSoft_ = glGetUniformLocation(program_, "uFlashSoft");
    uReflOn_ = glGetUniformLocation(program_, "uReflOn");
    uRefl_ = glGetUniformLocation(program_, "uRefl");
    uReflStrength_ = glGetUniformLocation(program_, "uReflStrength");
    uReflSkyHorizon_ = glGetUniformLocation(program_, "uReflSkyHorizon");
    uReflSkyTop_ = glGetUniformLocation(program_, "uReflSkyTop");
    uReflRounded_ = glGetUniformLocation(program_, "uReflRounded");
    uReflCenter_ = glGetUniformLocation(program_, "uReflCenter");
    glUseProgram(program_);
    glUniform1i(uRefl_, 1);  // sphere map lives on texture unit 1
    glUseProgram(0);

    GLuint gvs = compile(GL_VERTEX_SHADER, GRADE_VS);
    GLuint gfs = compile(GL_FRAGMENT_SHADER, GRADE_FS);
    if (!gvs || !gfs) return false;
    gradeProgram_ = glCreateProgram();
    glAttachShader(gradeProgram_, gvs);
    glAttachShader(gradeProgram_, gfs);
    glLinkProgram(gradeProgram_);
    glDeleteShader(gvs);
    glDeleteShader(gfs);
    glGetProgramiv(gradeProgram_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(gradeProgram_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "grading program link error: %s\n", log);
        return false;
    }
    uGradeSrc_ = glGetUniformLocation(gradeProgram_, "uSrc");
    uGradeGain_ = glGetUniformLocation(gradeProgram_, "uGain");
    uGradeLiftPos_ = glGetUniformLocation(gradeProgram_, "uLiftPos");
    uGradeLiftNeg_ = glGetUniformLocation(gradeProgram_, "uLiftNeg");
    uGradeMixCol_ = glGetUniformLocation(gradeProgram_, "uMixColor");
    uGradeMixAmt_ = glGetUniformLocation(gradeProgram_, "uMixAmt");
    glGenVertexArrays(1, &gradeVao_);  // empty VAO; vertices from gl_VertexID

    // Particle-preview program + shared dynamic buffer (pos3 + rgba4 + uv2)
    GLuint pvs = compile(GL_VERTEX_SHADER, PART_VS);
    GLuint pfs = compile(GL_FRAGMENT_SHADER, PART_FS);
    if (!pvs || !pfs) return false;
    particleProgram_ = glCreateProgram();
    glAttachShader(particleProgram_, pvs);
    glAttachShader(particleProgram_, pfs);
    glLinkProgram(particleProgram_);
    glDeleteShader(pvs);
    glDeleteShader(pfs);
    glGetProgramiv(particleProgram_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(particleProgram_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "particle program link error: %s\n", log);
        return false;
    }
    uPartMvp_ = glGetUniformLocation(particleProgram_, "uMvp");
    uPartUseTex_ = glGetUniformLocation(particleProgram_, "uUseTex");
    glGenVertexArrays(1, &particleVao_);
    glGenBuffers(1, &particleVbo_);
    glBindVertexArray(particleVao_);
    glBindBuffer(GL_ARRAY_BUFFER, particleVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                          (void*)(7 * sizeof(float)));
    glBindVertexArray(0);

    buildTerrainMesh();
    buildPrimitiveMeshes();
    return true;
}

void Viewport::shutdown() {
    if (program_) glDeleteProgram(program_);
    if (particleProgram_) glDeleteProgram(particleProgram_);
    if (particleVbo_) glDeleteBuffers(1, &particleVbo_);
    if (particleVao_) glDeleteVertexArrays(1, &particleVao_);
    for (Mesh& m : terrainChunkMeshes_) destroyMesh(m);
    terrainChunkMeshes_.clear();
    for (Mesh& m : terrainLineMeshes_) destroyMesh(m);
    terrainLineMeshes_.clear();
    for (Mesh& m : terrainLayerMeshes_) destroyMesh(m);
    terrainLayerMeshes_.clear();
    destroyMesh(navOverlayMesh_);
    destroyMesh(axes_);
    destroyMesh(box_);
    destroyMesh(sphere_);
    destroyMesh(cylinder_);
    destroyMesh(cone_);
    destroyMesh(plane_);
    destroyMesh(decal_);
    destroyMesh(spawnMarker_);
    destroyMesh(playerMarker_);
    destroyMesh(wireCube_);
    destroyMesh(lightGizmo_);
    destroyMesh(wireSphere_);
    destroyMesh(cameraBody_);
    destroyMesh(cameraFrustum_);
    destroyMesh(segment_);
    destroyMesh(portalArrow_);
    clearPrimMeshCache();
    destroyMesh(skyQuad_);
    destroyMesh(prevBg_);
    destroyMesh(prevFloor_);
    clearModelCache();
    clearTexCache();
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (colorTex_) glDeleteTextures(1, &colorTex_);
    if (depthRbo_) glDeleteRenderbuffers(1, &depthRbo_);
    if (prevFbo_) glDeleteFramebuffers(1, &prevFbo_);
    if (prevTex_) glDeleteTextures(1, &prevTex_);
    if (prevDepth_) glDeleteRenderbuffers(1, &prevDepth_);
    if (gradeProgram_) glDeleteProgram(gradeProgram_);
    if (gradeFbo_) glDeleteFramebuffers(1, &gradeFbo_);
    if (gradeTex_) glDeleteTextures(1, &gradeTex_);
    if (gradeVao_) glDeleteVertexArrays(1, &gradeVao_);
}

void Viewport::setGrading(bool enabled, const CompiledGrading& g) {
    gradingOn_ = enabled && !g.neutral();
    grading_ = g;
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
    destroyMesh(plane_);
    destroyMesh(decal_);
    destroyMesh(spawnMarker_);
    destroyMesh(playerMarker_);
    destroyMesh(wireCube_);
    destroyMesh(lightGizmo_);
    destroyMesh(wireSphere_);
    destroyMesh(cameraBody_);
    destroyMesh(cameraFrustum_);
    destroyMesh(segment_);
    destroyMesh(portalArrow_);
    box_ = uploadMesh(unitBox());
    sphere_ = uploadMesh(unitSphere());
    cylinder_ = uploadMesh(unitCylinder());
    cone_ = uploadMesh(unitCone());
    plane_ = uploadMesh(unitPlane());
    decal_ = uploadMesh(unitDecal());
    spawnMarker_ = uploadMesh(unitSpawnMarker());
    playerMarker_ = uploadMesh(unitPlayerMarker());
    wireCube_ = uploadMesh(unitWireCube());
    lightGizmo_ = uploadMesh(unitLightBulb());
    wireSphere_ = uploadMesh(unitWireSphere());
    cameraBody_ = uploadMesh(unitCameraBody());
    cameraFrustum_ = uploadMesh(unitCameraFrustum());
    {
        // unit +Z segment - stretched onto arbitrary endpoints for the
        // portal link line (only the third matrix column + translation
        // matter: the two vertices sit at z=0 and z=1).
        std::vector<float> seg;
        pushVertexColor(seg, 0, 0, 0, 1.0f, 1.0f, 1.0f);
        pushVertexColor(seg, 0, 0, 1.0f, 1.0f, 1.0f, 1.0f);
        segment_ = uploadMesh(seg);
    }
    {
        // +Z arrow (shaft + 4 head barbs): marks the portal's ENTRY side -
        // the front face that shows the view and accepts the crossing.
        std::vector<float> ar;
        pushVertexColor(ar, 0, 0, 0, 1.0f, 1.0f, 1.0f);
        pushVertexColor(ar, 0, 0, 1.0f, 1.0f, 1.0f, 1.0f);
        const float b = 0.12f, zb = 0.78f;
        pushVertexColor(ar, 0, 0, 1.0f, 1.0f, 1.0f, 1.0f);
        pushVertexColor(ar, b, 0, zb, 1.0f, 1.0f, 1.0f);
        pushVertexColor(ar, 0, 0, 1.0f, 1.0f, 1.0f, 1.0f);
        pushVertexColor(ar, -b, 0, zb, 1.0f, 1.0f, 1.0f);
        pushVertexColor(ar, 0, 0, 1.0f, 1.0f, 1.0f, 1.0f);
        pushVertexColor(ar, 0, b, zb, 1.0f, 1.0f, 1.0f);
        pushVertexColor(ar, 0, 0, 1.0f, 1.0f, 1.0f, 1.0f);
        pushVertexColor(ar, 0, -b, zb, 1.0f, 1.0f, 1.0f);
        portalArrow_ = uploadMesh(ar);
    }
}

const Viewport::Mesh& Viewport::primMesh(PrimitiveType type, int detail) {
    // SavePoint draws as a Box (same tessellation family, shared mesh cache).
    if (type == PrimitiveType::SavePoint) type = PrimitiveType::Box;
    const int d = clampPrimDetail(type, detail);
    std::map<int, Mesh>* cache;
    switch (type) {
        case PrimitiveType::Box: cache = &boxMeshes_; break;
        case PrimitiveType::Sphere: cache = &sphereMeshes_; break;
        case PrimitiveType::Cylinder: cache = &cylinderMeshes_; break;
        case PrimitiveType::Cone: cache = &coneMeshes_; break;
        default: return box_;
    }
    if (auto it = cache->find(d); it != cache->end()) return it->second;
    const Mesh m = type == PrimitiveType::Box        ? uploadMesh(unitBox(d))
                   : type == PrimitiveType::Sphere   ? uploadMesh(unitSphere(d))
                   : type == PrimitiveType::Cylinder ? uploadMesh(unitCylinder(d))
                                                     : uploadMesh(unitCone(d));
    return cache->emplace(d, m).first->second;
}

void Viewport::clearPrimMeshCache() {
    for (std::map<int, Mesh>* c :
         {&boxMeshes_, &sphereMeshes_, &cylinderMeshes_, &coneMeshes_}) {
        for (auto& [detail, m] : *c) destroyMesh(m);
        c->clear();
    }
}

void Viewport::buildTerrainMesh() {
    if (!program_) return;  // init() not called yet
    for (Mesh& m : terrainChunkMeshes_) destroyMesh(m);
    for (Mesh& m : terrainLineMeshes_) destroyMesh(m);
    for (Mesh& m : terrainLayerMeshes_) destroyMesh(m);
    destroyMesh(axes_);

    // Match the generated PS2 game: checker pattern, capped cell count,
    // vertex heights from the sculpted heightmap. Built per chunk so a
    // sculpt stroke rebuilds only the chunks under the brush.
    tcCellsX_ = terrain_.width > maxCells_ ? maxCells_ : terrain_.width;
    tcCellsZ_ = terrain_.depth > maxCells_ ? maxCells_ : terrain_.depth;
    tcChunksX_ = (tcCellsX_ + kTerrainChunkCells - 1) / kTerrainChunkCells;
    tcChunksZ_ = (tcCellsZ_ + kTerrainChunkCells - 1) / kTerrainChunkCells;
    terrainChunkMeshes_.assign((size_t)tcChunksX_ * tcChunksZ_, Mesh());
    terrainLineMeshes_.assign((size_t)tcChunksX_ * tcChunksZ_, Mesh());
    terrainLayerMeshes_.assign(
        terrainLayers_.size() * (size_t)tcChunksX_ * tcChunksZ_, Mesh());
    for (int cz = 0; cz < tcChunksZ_; ++cz)
        for (int cx = 0; cx < tcChunksX_; ++cx) buildTerrainChunkMesh(cx, cz);

    // World axes: X red, Y green, Z blue (slightly above terrain)
    const float w = (float)terrain_.width, d = (float)terrain_.depth;
    std::vector<float> lines;
    float axisLen = (w > d ? w : d) * 0.6f;
    pushVertexColor(lines, 0, 0.02f, 0, 0.9f, 0.2f, 0.2f);
    pushVertexColor(lines, axisLen, 0.02f, 0, 0.9f, 0.2f, 0.2f);
    pushVertexColor(lines, 0, 0.02f, 0, 0.2f, 0.9f, 0.2f);
    pushVertexColor(lines, 0, axisLen * 0.5f, 0, 0.2f, 0.9f, 0.2f);
    pushVertexColor(lines, 0, 0.02f, 0, 0.3f, 0.4f, 1.0f);
    pushVertexColor(lines, 0, 0.02f, axisLen, 0.3f, 0.4f, 1.0f);
    axes_ = uploadMesh(lines);
}

// Macro ground variation: deterministic world-position value noise (two
// smoothstepped octaves) multiplied into the vertex shade. Twin of
// tintHash/tintValue/tintNoise2 in the generated game (templates.cpp,
// above buildTerrainChunk) - keep the formulas in sync.
static float tintHash(int ix, int iz) {
    unsigned h = (unsigned)ix * 73856093u ^ (unsigned)iz * 19349663u;
    h ^= h >> 13;
    h *= 0x85EBCA6Bu;
    h ^= h >> 16;
    return (float)(h & 0xFFFFu) / 65535.0f;
}
static float tintValue(float x, float z, float scale) {
    const float gx = x / scale, gz = z / scale;
    const float fxf = std::floor(gx), fzf = std::floor(gz);
    const int ix = (int)fxf, iz = (int)fzf;
    float fx = gx - fxf, fz = gz - fzf;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    const float a = tintHash(ix, iz), b = tintHash(ix + 1, iz);
    const float c = tintHash(ix, iz + 1), d = tintHash(ix + 1, iz + 1);
    return (a * (1.0f - fx) + b * fx) * (1.0f - fz) +
           (c * (1.0f - fx) + d * fx) * fz;
}
static float tintNoise2(float x, float z, float scale) {
    return tintValue(x, z, scale) * 0.7f +
           tintValue(x + 191.0f, z - 353.0f, scale * 0.37f) * 0.3f;
}

// One terrain chunk (kTerrainChunkCells^2 cells at most): triangles + grid
// lines. The vertex emission matches the generated PS2 game exactly - when
// changing the shading here, change buildTerrainChunk in templates.cpp too.
void Viewport::buildTerrainChunkMesh(int cx, int cz) {
    const int ci = cz * tcChunksX_ + cx;
    destroyMesh(terrainChunkMeshes_[ci]);
    destroyMesh(terrainLineMeshes_[ci]);

    const float w = (float)terrain_.width;
    const float d = (float)terrain_.depth;
    const float x0 = -w * 0.5f, z0 = -d * 0.5f;
    const int cellsX = tcCellsX_, cellsZ = tcCellsZ_;
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
        Vec3 s = shadeOf(normalize(n));
        // Macro ground variation - the game applies the same tint in
        // buildTerrainChunk (base + layer passes shade through one place).
        if (tintVariation_ > 0.0f) {
            const float tv =
                1.0f + tintVariation_ * (tintNoise2(x0 + ix * sx, z0 + iz * sz,
                                                    tintScale_) -
                                         0.5f);
            s.x = std::min(s.x * tv, 1.0f);
            s.y = std::min(s.y * tv, 1.0f);
            s.z = std::min(s.z * tv, 1.0f);
        }
        return s;
    };

    // With a material every cell takes its Kd tint (the shader modulates the
    // texture by it when one is bound; a flat color otherwise). Without a
    // material the terrain falls back to the two-green checker.
    // With a material every cell takes its Kd tint (the shader modulates the
    // texture by it when one is bound; a flat color otherwise). Without a
    // material the terrain falls back to the two-green checker.
    const float cA[3] = {terrainHasMaterial_ ? terrainKd_[0] : 96 / 255.0f,
                         terrainHasMaterial_ ? terrainKd_[1] : 160 / 255.0f,
                         terrainHasMaterial_ ? terrainKd_[2] : 72 / 255.0f};
    const float cB[3] = {terrainHasMaterial_ ? terrainKd_[0] : 74 / 255.0f,
                         terrainHasMaterial_ ? terrainKd_[1] : 128 / 255.0f,
                         terrainHasMaterial_ ? terrainKd_[2] : 56 / 255.0f};
    // Texture tiling from the material's map_Kd "-s": UV repeats per world
    // unit, per axis (u across X, v across Z). Matches the game's st() lambda.
    const float tu = terrainTile_[0], tv = terrainTile_[1];
    auto uvU = [&](float wx) { return wx * tu; };
    auto uvV = [&](float wz) { return wz * tv; };

    const int gx0 = cx * kTerrainChunkCells;
    const int gz0 = cz * kTerrainChunkCells;
    const int gx1 = gx0 + kTerrainChunkCells > cellsX ? cellsX : gx0 + kTerrainChunkCells;
    const int gz1 = gz0 + kTerrainChunkCells > cellsZ ? cellsZ : gz0 + kTerrainChunkCells;

    std::vector<float> tri;
    tri.reserve((size_t)(gx1 - gx0) * (gz1 - gz0) * 6 * 8);
    for (int z = gz0; z < gz1; ++z) {
        for (int x = gx0; x < gx1; ++x) {
            const float* c = ((x + z) % 2 == 0) ? cA : cB;
            float ax = x0 + x * sx, az = z0 + z * sz;
            float bx = ax + sx, bz = az + sz;
            const float h00 = hAt(x, z), h10 = hAt(x + 1, z);
            const float h01 = hAt(x, z + 1), h11 = hAt(x + 1, z + 1);
            const Vec3 s00 = shadeAt(x, z), s10 = shadeAt(x + 1, z);
            const Vec3 s01 = shadeAt(x, z + 1), s11 = shadeAt(x + 1, z + 1);
            pushVertexColor(tri, ax, h00, az, c[0] * s00.x, c[1] * s00.y, c[2] * s00.z,
                            uvU(ax), uvV(az));
            pushVertexColor(tri, bx, h10, az, c[0] * s10.x, c[1] * s10.y, c[2] * s10.z,
                            uvU(bx), uvV(az));
            pushVertexColor(tri, ax, h01, bz, c[0] * s01.x, c[1] * s01.y, c[2] * s01.z,
                            uvU(ax), uvV(bz));
            pushVertexColor(tri, bx, h10, az, c[0] * s10.x, c[1] * s10.y, c[2] * s10.z,
                            uvU(bx), uvV(az));
            pushVertexColor(tri, bx, h11, bz, c[0] * s11.x, c[1] * s11.y, c[2] * s11.z,
                            uvU(bx), uvV(bz));
            pushVertexColor(tri, ax, h01, bz, c[0] * s01.x, c[1] * s01.y, c[2] * s01.z,
                            uvU(ax), uvV(bz));
        }
    }
    terrainChunkMeshes_[ci] = uploadMesh(tri);

    // Grid lines (cell borders, following the relief). Shared border lines
    // belong to the chunk on their -X/-Z side; the last chunk per axis also
    // emits the map's outer edge. Above kTerrainFullGridCells total cells the
    // per-cell grid is solid noise (and tens of MB) - draw chunk borders only.
    const int stride =
        (long long)cellsX * cellsZ <= kTerrainFullGridCells ? 1 : kTerrainChunkCells;
    std::vector<float> lines;
    const float gc = 0.15f;  // grid line color (dark)
    for (int x = gx0; x <= gx1; ++x) {
        if (x % stride != 0 && x != cellsX) continue;
        if (x == gx1 && x != cellsX) continue;  // next chunk draws this border
        const float px = x0 + x * sx;
        for (int z = gz0; z < gz1; ++z) {
            pushVertexColor(lines, px, hAt(x, z) + 0.02f, z0 + z * sz, gc, gc, gc);
            pushVertexColor(lines, px, hAt(x, z + 1) + 0.02f, z0 + (z + 1) * sz, gc, gc,
                            gc);
        }
    }
    for (int z = gz0; z <= gz1; ++z) {
        if (z % stride != 0 && z != cellsZ) continue;
        if (z == gz1 && z != cellsZ) continue;  // next chunk draws this border
        const float pz = z0 + z * sz;
        for (int x = gx0; x < gx1; ++x) {
            pushVertexColor(lines, x0 + x * sx, hAt(x, z) + 0.02f, pz, gc, gc, gc);
            pushVertexColor(lines, x0 + (x + 1) * sx, hAt(x + 1, z) + 0.02f, pz, gc, gc,
                            gc);
        }
    }
    terrainLineMeshes_[ci] = uploadMesh(lines);

    // Painted-layer passes (docs/terrain-painting.md): per layer with any
    // weight on this chunk, the same triangles again - tiled layer UVs,
    // shade-lit tint, vertex alpha = the painted weight. The GS twin builds
    // identical passes in buildTerrainChunk (templates.cpp); keep them in sync.
    const int layerN = (int)terrainLayers_.size();
    const int vw = cellsX + 1, vd = cellsZ + 1;  // weights live on the vertices
    const bool haveSplat =
        layerN > 0 && (int)splat_.size() == vw * vd * layerN;
    for (int l = 0; l < layerN; ++l) {
        Mesh& lm =
            terrainLayerMeshes_[(size_t)l * tcChunksX_ * tcChunksZ_ + ci];
        destroyMesh(lm);
        if (!haveSplat) continue;
        auto wAt = [&](int ix, int iz) {
            ix = ix < 0 ? 0 : ix > vw - 1 ? vw - 1 : ix;
            iz = iz < 0 ? 0 : iz > vd - 1 ? vd - 1 : iz;
            return splat_[((size_t)iz * vw + ix) * layerN + l] / 255.0f;
        };
        bool any = false;
        for (int z = gz0; z <= gz1 && !any; ++z)
            for (int x = gx0; x <= gx1; ++x)
                if (wAt(x, z) > 0.0f) {
                    any = true;
                    break;
                }
        if (!any) continue;  // empty Mesh = no pass for this layer here

        const TerrainLayerDraw& L = terrainLayers_[l];
        std::vector<float> lt;
        lt.reserve((size_t)(gx1 - gx0) * (gz1 - gz0) * 6 * 9);
        auto pushL = [&](float px, float pz, float h, const Vec3& s, float wgt) {
            lt.push_back(px);
            lt.push_back(h);
            lt.push_back(pz);
            lt.push_back(L.kd[0] * s.x);
            lt.push_back(L.kd[1] * s.y);
            lt.push_back(L.kd[2] * s.z);
            lt.push_back(wgt);
            lt.push_back(px * L.tile[0]);
            lt.push_back(pz * L.tile[1]);
        };
        for (int z = gz0; z < gz1; ++z) {
            for (int x = gx0; x < gx1; ++x) {
                const float ax = x0 + x * sx, az = z0 + z * sz;
                const float bx = ax + sx, bz = az + sz;
                const float h00 = hAt(x, z), h10 = hAt(x + 1, z);
                const float h01 = hAt(x, z + 1), h11 = hAt(x + 1, z + 1);
                const Vec3 s00 = shadeAt(x, z), s10 = shadeAt(x + 1, z);
                const Vec3 s01 = shadeAt(x, z + 1), s11 = shadeAt(x + 1, z + 1);
                const float w00 = wAt(x, z), w10 = wAt(x + 1, z);
                const float w01 = wAt(x, z + 1), w11 = wAt(x + 1, z + 1);
                pushL(ax, az, h00, s00, w00);
                pushL(bx, az, h10, s10, w10);
                pushL(ax, bz, h01, s01, w01);
                pushL(bx, az, h10, s10, w10);
                pushL(bx, bz, h11, s11, w11);
                pushL(ax, bz, h01, s01, w01);
            }
        }
        lm = uploadMesh9(lt);
    }
}

void Viewport::updateTerrainRegion(const std::vector<float>& heights, float worldX,
                                   float worldZ, float radius) {
    if ((int)heights.size() != hmW_ * hmD_ || terrainChunkMeshes_.empty()) {
        heights_ = heights;
        buildTerrainMesh();  // dims changed - fall back to the full rebuild
        return;
    }
    heights_ = heights;

    // Cells whose vertices (or shading neighbors: +-1 vertex) the brush
    // circle touched, padded one cell outward, mapped to chunk range.
    const float w = (float)terrain_.width, d = (float)terrain_.depth;
    const float sx = w / tcCellsX_, sz = d / tcCellsZ_;
    const int minCellX = (int)((worldX - radius + w * 0.5f) / sx) - 2;
    const int maxCellX = (int)((worldX + radius + w * 0.5f) / sx) + 2;
    const int minCellZ = (int)((worldZ - radius + d * 0.5f) / sz) - 2;
    const int maxCellZ = (int)((worldZ + radius + d * 0.5f) / sz) + 2;
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    const int c0x = clampi(minCellX / kTerrainChunkCells, 0, tcChunksX_ - 1);
    const int c1x = clampi(maxCellX / kTerrainChunkCells, 0, tcChunksX_ - 1);
    const int c0z = clampi(minCellZ / kTerrainChunkCells, 0, tcChunksZ_ - 1);
    const int c1z = clampi(maxCellZ / kTerrainChunkCells, 0, tcChunksZ_ - 1);
    for (int cz = c0z; cz <= c1z; ++cz)
        for (int cx = c0x; cx <= c1x; ++cx) buildTerrainChunkMesh(cx, cz);
}

void Viewport::ensureFramebuffer(int width, int height) {
    if (fbo_ && width == fbWidth_ && height == fbHeight_) return;
    fbWidth_ = width;
    fbHeight_ = height;

    if (!fbo_) glGenFramebuffers(1, &fbo_);
    if (!colorTex_) glGenTextures(1, &colorTex_);
    if (!depthRbo_) glGenRenderbuffers(1, &depthRbo_);
    if (!gradeFbo_) glGenFramebuffers(1, &gradeFbo_);
    if (!gradeTex_) glGenTextures(1, &gradeTex_);

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

    // Grading post-pass target (colorTex_ -> gradeTex_, no depth needed)
    glBindTexture(GL_TEXTURE_2D, gradeTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, gradeFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gradeTex_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "grading framebuffer incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Material Editor preview target. Separate from fbo_ - render() resizes that
// one to the viewport window every frame, and both draw within one UI frame.
void Viewport::ensurePreviewFramebuffer(int width, int height) {
    if (prevFbo_ && width == prevW_ && height == prevH_) return;
    prevW_ = width;
    prevH_ = height;

    if (!prevFbo_) glGenFramebuffers(1, &prevFbo_);
    if (!prevTex_) glGenTextures(1, &prevTex_);
    if (!prevDepth_) glGenRenderbuffers(1, &prevDepth_);

    glBindTexture(GL_TEXTURE_2D, prevTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindRenderbuffer(GL_RENDERBUFFER, prevDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, prevTex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                              prevDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "material preview framebuffer incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

namespace {

// Forward euler rotation (X, then Y, then Z) - matches the generated game's
// rotated() and the model matrix composition.
Vec3 rotateEuler(Vec3 v, const float* rotDeg) {
    const float d2r = kPi / 180.0f;
    {
        const float c = std::cos(rotDeg[0] * d2r), s = std::sin(rotDeg[0] * d2r);
        const float y = v.y * c - v.z * s, z = v.y * s + v.z * c;
        v.y = y, v.z = z;
    }
    {
        const float c = std::cos(rotDeg[1] * d2r), s = std::sin(rotDeg[1] * d2r);
        const float x = v.x * c + v.z * s, z = -v.x * s + v.z * c;
        v.x = x, v.z = z;
    }
    {
        const float c = std::cos(rotDeg[2] * d2r), s = std::sin(rotDeg[2] * d2r);
        const float x = v.x * c - v.y * s, y = v.x * s + v.y * c;
        v.x = x, v.y = y;
    }
    return v;
}

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
        if (hiddenAt(i)) continue;  // hidden layers are unclickable
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

void Viewport::setFog(bool enabled, const float* rgb, float start, float end) {
    fogOn_ = enabled;
    fogColor_[0] = rgb[0], fogColor_[1] = rgb[1], fogColor_[2] = rgb[2];
    fogStart_ = start;
    fogEnd_ = end;
}

void Viewport::setFlashlight(bool enabled, const float* rgb, float range,
                             float halfAngleDeg) {
    flashOn_ = enabled;
    flashColor_[0] = rgb[0], flashColor_[1] = rgb[1], flashColor_[2] = rgb[2];
    flashRange_ = range < 1.0f ? 1.0f : range;
    flashAngle_ = halfAngleDeg < 2.0f ? 2.0f : (halfAngleDeg > 80.0f ? 80.0f : halfAngleDeg);
}

void Viewport::setProjectDir(const std::string& dir) {
    if (projectDir_ == dir) return;
    projectDir_ = dir;
    clearModelCache();
    clearTexCache();
}

void Viewport::setProjectedDecals(
    const std::map<std::string, std::vector<float>>& meshes, uint64_t version) {
    if (projectedDecalHasVersion_ && version == projectedDecalVersion_) return;
    projectedDecalVersion_ = version;
    projectedDecalHasVersion_ = true;
    for (auto& [id, m] : projectedDecalMeshes_) destroyMesh(m);
    projectedDecalMeshes_.clear();
    // decalproj emits pos3+uv2; expand to the pos3+color3+uv2 GL layout with a
    // white color (decals draw unlit - the object tint is applied at draw time).
    // UVs are used as-is: the viewport and the baked game mesh both draw the
    // same world-space geometry with the same UVs, so the preview matches PS2.
    for (const auto& [id, verts] : meshes) {
        if (verts.empty()) continue;
        std::vector<float> interleaved;
        interleaved.reserve(verts.size() / 5 * 8);
        for (size_t i = 0; i + 4 < verts.size(); i += 5) {
            interleaved.insert(interleaved.end(),
                               {verts[i], verts[i + 1], verts[i + 2], 1.0f, 1.0f,
                                1.0f, verts[i + 3], verts[i + 4]});
        }
        projectedDecalMeshes_[id] = uploadMesh(interleaved);
    }
}

void Viewport::setNavOverlay(const navmesh::NavGrid* grid, uint64_t version) {
    if (!grid) {
        navOverlayOn_ = false;
        return;
    }
    navOverlayOn_ = true;
    if (navOverlayHasVersion_ && version == navOverlayVersion_) return;
    navOverlayVersion_ = version;
    navOverlayHasVersion_ = true;
    destroyMesh(navOverlayMesh_);
    // One inset quad per walkable cell (the gaps read as a grid), corners
    // draped on the terrain surface and lifted a touch so slopes don't
    // z-fight the overlay through the terrain triangles.
    const float lift = 0.15f;
    const float insetX = grid->cellW * 0.06f;
    const float insetZ = grid->cellD * 0.06f;
    std::vector<float> v;
    for (int z = 0; z < grid->d; ++z)
        for (int x = 0; x < grid->w; ++x) {
            if (!grid->walkable[(size_t)z * grid->w + x]) continue;
            const float x0 = grid->originX + x * grid->cellW + insetX;
            const float x1 = grid->originX + (x + 1) * grid->cellW - insetX;
            const float z0 = grid->originZ + z * grid->cellD + insetZ;
            const float z1 = grid->originZ + (z + 1) * grid->cellD - insetZ;
            auto put = [&](float wx, float wz) {
                v.insert(v.end(), {wx, terrainHeight(wx, wz) + lift, wz, 0.25f,
                                   0.85f, 0.4f, 0.0f, 0.0f});
            };
            put(x0, z0); put(x1, z0); put(x1, z1);
            put(x0, z0); put(x1, z1); put(x0, z1);
        }
    navOverlayMesh_ = uploadMesh(v);
}

void Viewport::setTerrainMaterial(const std::string& texRelPath, const float kd[3],
                                  bool hasMaterial, const float tile[2]) {
    if (terrainTexture_ == texRelPath && terrainTile_[0] == tile[0] &&
        terrainTile_[1] == tile[1] && terrainHasMaterial_ == hasMaterial &&
        terrainKd_[0] == kd[0] && terrainKd_[1] == kd[1] && terrainKd_[2] == kd[2])
        return;
    terrainTexture_ = texRelPath;
    terrainTile_[0] = tile[0], terrainTile_[1] = tile[1];
    terrainHasMaterial_ = hasMaterial;
    terrainKd_[0] = kd[0], terrainKd_[1] = kd[1], terrainKd_[2] = kd[2];
    if (program_) buildTerrainMesh();
}

void Viewport::setTerrainLayers(const std::vector<TerrainLayerDraw>& layers,
                                const std::vector<uint8_t>& weights) {
    // No cheap equality check: callers send this on layer edits and scene
    // switches, both of which want the rebuild anyway.
    terrainLayers_ = layers;
    splat_ = weights;
    if (program_) buildTerrainMesh();
}

void Viewport::setTerrainTint(float variation, float scaleWorld) {
    if (scaleWorld < 1.0f) scaleWorld = 1.0f;
    if (tintVariation_ == variation && tintScale_ == scaleWorld) return;
    tintVariation_ = variation;
    tintScale_ = scaleWorld;
    if (program_) buildTerrainMesh();
}

void Viewport::updateSplatRegion(const std::vector<uint8_t>& weights, float worldX,
                                 float worldZ, float radius) {
    splat_ = weights;
    if (terrainChunkMeshes_.empty()) {
        buildTerrainMesh();
        return;
    }
    // Same brush-to-chunk mapping as updateTerrainRegion (heights sculpting).
    const float w = (float)terrain_.width, d = (float)terrain_.depth;
    const float sx = w / tcCellsX_, sz = d / tcCellsZ_;
    const int minCellX = (int)((worldX - radius + w * 0.5f) / sx) - 2;
    const int maxCellX = (int)((worldX + radius + w * 0.5f) / sx) + 2;
    const int minCellZ = (int)((worldZ - radius + d * 0.5f) / sz) - 2;
    const int maxCellZ = (int)((worldZ + radius + d * 0.5f) / sz) + 2;
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    const int c0x = clampi(minCellX / kTerrainChunkCells, 0, tcChunksX_ - 1);
    const int c1x = clampi(maxCellX / kTerrainChunkCells, 0, tcChunksX_ - 1);
    const int c0z = clampi(minCellZ / kTerrainChunkCells, 0, tcChunksZ_ - 1);
    const int c1z = clampi(maxCellZ / kTerrainChunkCells, 0, tcChunksZ_ - 1);
    for (int cz = c0z; cz <= c1z; ++cz)
        for (int cx = c0x; cx <= c1x; ++cx) buildTerrainChunkMesh(cx, cz);
}

void Viewport::setUsableHighlight(bool enabled, const float* rgb) {
    usableHighlight_ = enabled;
    for (int i = 0; i < 3; ++i) usableHighlightCol_[i] = rgb[i];
}

void Viewport::clearTexCache() {
    for (auto& [path, tex] : texCache_)
        if (tex) glDeleteTextures(1, &tex);
    texCache_.clear();
}

void Viewport::invalidateAssets() {
    clearModelCache();  // also drops materialCache_
    clearTexCache();
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
    for (auto& [path, draw] : modelCache_)
        for (auto& part : draw.parts) destroyMesh(part.mesh);
    modelCache_.clear();
    materialCache_.clear();  // GL textures are owned by texCache_
    for (auto& [path, draw] : animModelCache_)
        for (auto& part : draw.parts) {
            destroyMesh(part.mesh);
            if (part.tex) glDeleteTextures(1, &part.tex);  // embedded, not texCache_
        }
    animModelCache_.clear();
    clearMatPrevModel();  // same disk-derived sources (obj + mtl)
}

void Viewport::clearMatPrevModel() {
    for (MatPrevPart& part : matPrevModel_.parts) destroyMesh(part.mesh);
    matPrevModel_ = MatPrevModel{};
    matPrevPick_.valid = false;
}

// The material preview's model slot (single entry - the editor shows one
// model at a time). Kd stays out of the vertex colors on purpose, see the
// struct comment.
const Viewport::MatPrevModel* Viewport::matPrevModelDraw(
    const std::string& modelRel, const std::string& mtlRel) {
    const std::string key = modelRel + "|" + mtlRel;
    if (matPrevModel_.key == key) return &matPrevModel_;

    clearMatPrevModel();
    matPrevModel_.key = key;

    objparser::Model model;
    if (!objparser::load(
            (std::filesystem::path(projectDir_) / modelRel).string(), model,
            mtlRel.empty() ? ""
                           : (std::filesystem::path(projectDir_) / mtlRel).string()))
        return &matPrevModel_;  // !ok - negative result cached until invalidated

    // map_Kd paths resolve relative to the file that defined them (same rule
    // as modelDraw): the override .mtl when assigned, the model otherwise.
    const std::filesystem::path texDir =
        std::filesystem::path(mtlRel.empty() ? modelRel : mtlRel).parent_path();
    for (const objparser::Submesh& sub : model.submeshes) {
        MatPrevPart part;
        part.material = sub.material;
        part.kd[0] = sub.kd[0], part.kd[1] = sub.kd[1], part.kd[2] = sub.kd[2];
        if (!sub.texture.empty())
            part.texRel = (texDir / sub.texture).generic_string();
        if (!sub.refl.empty()) {
            if (sub.refl == "@sky")
                part.reflSky = true;
            else
                part.reflRel = (texDir / sub.refl).generic_string();
            part.reflStrength = sub.reflStrength;
            part.reflRounded = sub.reflRounded;
            // Part centroid - the origin the rounded env normals radiate
            // from (twin of the PS2 rebuild's centroid).
            const size_t n = sub.verts.size() / 8;
            for (size_t i = 0; i + 7 < sub.verts.size(); i += 8) {
                part.centroid[0] += sub.verts[i];
                part.centroid[1] += sub.verts[i + 1];
                part.centroid[2] += sub.verts[i + 2];
            }
            if (n > 0)
                for (float& c : part.centroid) c /= (float)n;
        }
        std::vector<float> interleaved;
        interleaved.reserve(sub.verts.size());
        part.tris.reserve(sub.verts.size() / 8 * 5);
        for (size_t i = 0; i + 7 < sub.verts.size(); i += 8) {
            const Vec3 s =
                shadeOf({sub.verts[i + 3], sub.verts[i + 4], sub.verts[i + 5]});
            interleaved.insert(interleaved.end(),
                               {sub.verts[i], sub.verts[i + 1], sub.verts[i + 2],
                                s.x, s.y, s.z, sub.verts[i + 6], sub.verts[i + 7]});
            part.tris.insert(part.tris.end(),
                             {sub.verts[i], sub.verts[i + 1], sub.verts[i + 2],
                              sub.verts[i + 6], sub.verts[i + 7]});
        }
        part.mesh = uploadMesh(interleaved);
        matPrevModel_.parts.push_back(std::move(part));
    }
    matPrevModel_.ok = !matPrevModel_.parts.empty();
    for (int i = 0; i < 3; ++i)
        matPrevModel_.center[i] = (model.min[i] + model.max[i]) * 0.5f;
    matPrevModel_.minY = model.min[1];
    const float dx = model.max[0] - model.min[0];
    const float dy = model.max[1] - model.min[1];
    const float dz = model.max[2] - model.min[2];
    matPrevModel_.radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    if (matPrevModel_.radius < 0.01f) matPrevModel_.radius = 0.01f;
    return &matPrevModel_;
}

void Viewport::updateTexturePixels(const std::string& relPath, int w, int h,
                                   const unsigned char* rgba) {
    if (relPath.empty() || w < 1 || h < 1 || !rgba) return;
    uint32_t& tex = texCache_[relPath];
    if (!tex) {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        tex = t;
    } else {
        glBindTexture(GL_TEXTURE_2D, tex);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// First material of an assigned .mtl - the surface of a primitive.
const Viewport::MaterialDraw* Viewport::materialDraw(const std::string& relPath) {
    if (relPath.empty()) return nullptr;
    auto it = materialCache_.find(relPath);
    if (it != materialCache_.end()) return &it->second;

    MaterialDraw draw;  // stays white on failure - plain object color
    std::vector<objparser::MtlMaterial> materials;
    if (objparser::loadMtl((std::filesystem::path(projectDir_) / relPath).string(),
                           materials)) {
        const objparser::MtlMaterial& m = materials.front();
        draw.kd[0] = m.kd[0];
        draw.kd[1] = m.kd[1];
        draw.kd[2] = m.kd[2];
        if (!m.texture.empty())
            draw.tex = glTexture((std::filesystem::path(relPath).parent_path() /
                                  m.texture)
                                     .generic_string());
        if (!m.refl.empty()) {
            if (m.refl == "@sky")
                draw.reflSky = true;
            else
                draw.reflTex = glTexture(
                    (std::filesystem::path(relPath).parent_path() / m.refl)
                        .generic_string());
            draw.reflStrength = m.reflStrength;
            draw.reflRounded = m.reflRounded;
        }
    }
    return &materialCache_.emplace(relPath, draw).first->second;
}

const Viewport::ModelDraw* Viewport::modelDraw(const std::string& relPath,
                                               const std::string& materialRel) {
    if (relPath.empty()) return nullptr;
    const std::string key = relPath + "|" + materialRel;
    auto it = modelCache_.find(key);
    if (it != modelCache_.end()) return it->second.parts.empty() ? nullptr : &it->second;

    objparser::Model model;
    ModelDraw draw;  // stays empty on failure - negative result is cached too
    if (objparser::load(
            (std::filesystem::path(projectDir_) / relPath).string(), model,
            materialRel.empty()
                ? ""
                : (std::filesystem::path(projectDir_) / materialRel).string())) {
        // map_Kd paths resolve relative to the file that defined them: the
        // override .mtl when one is assigned, the model otherwise
        const std::filesystem::path modelDir =
            std::filesystem::path(materialRel.empty() ? relPath : materialRel)
                .parent_path();
        for (const objparser::Submesh& sub : model.submeshes) {
            std::vector<float> interleaved;
            interleaved.reserve(sub.verts.size());
            for (size_t i = 0; i + 7 < sub.verts.size(); i += 8) {
                // Kd is baked into the vertex colors (the object color still
                // modulates on top via the tint uniform - same as the game).
                const Vec3 s =
                    shadeOf({sub.verts[i + 3], sub.verts[i + 4], sub.verts[i + 5]});
                interleaved.insert(
                    interleaved.end(),
                    {sub.verts[i], sub.verts[i + 1], sub.verts[i + 2],
                     s.x * sub.kd[0], s.y * sub.kd[1], s.z * sub.kd[2],
                     sub.verts[i + 6], sub.verts[i + 7]});
            }
            ModelPart part;
            part.mesh = uploadMesh(interleaved);
            if (!sub.texture.empty())
                part.tex = glTexture((modelDir / sub.texture).generic_string());
            if (!sub.refl.empty()) {
                if (sub.refl == "@sky")
                    part.reflSky = true;
                else
                    part.reflTex =
                        glTexture((modelDir / sub.refl).generic_string());
                part.reflStrength = sub.reflStrength;
                part.reflRounded = sub.reflRounded;
                // Part centroid (model space) for the rounded env normals.
                const size_t n = sub.verts.size() / 8;
                for (size_t i = 0; i + 7 < sub.verts.size(); i += 8) {
                    part.centroid[0] += sub.verts[i];
                    part.centroid[1] += sub.verts[i + 1];
                    part.centroid[2] += sub.verts[i + 2];
                }
                if (n > 0)
                    for (float& c : part.centroid) c /= (float)n;
            }
            draw.parts.push_back(part);
        }
    }
    modelCache_[key] = draw;
    return modelCache_[key].parts.empty() ? nullptr : &modelCache_[key];
}

// Animated .glb model: bake once (CPU-side clips kept for the playback
// preview), upload frame 0 into dynamic per-part meshes, decode embedded
// textures. Failures cache as !ok and the caller falls back to the box.
Viewport::AnimModelDraw* Viewport::animModelDraw(const std::string& relPath) {
    if (relPath.empty()) return nullptr;
    auto it = animModelCache_.find(relPath);
    if (it != animModelCache_.end()) return &it->second;

    AnimModelDraw draw;
    std::string error;
    const std::string full = (std::filesystem::path(projectDir_) / relPath).string();
    if (animimport::bake(full, 12.0f, draw.baked, error)) {
        draw.ok = true;
        std::vector<uint32_t> imageTex(draw.baked.images.size(), 0);
        for (size_t i = 0; i < draw.baked.images.size(); ++i) {
            int w = 0, h = 0, comp = 0;
            unsigned char* pixels = stbi_load_from_memory(
                draw.baked.images[i].png.data(),
                (int)draw.baked.images[i].png.size(), &w, &h, &comp, 4);
            if (!pixels) continue;
            glGenTextures(1, &imageTex[i]);
            glBindTexture(GL_TEXTURE_2D, imageTex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, pixels);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            stbi_image_free(pixels);
        }
        for (size_t pi = 0; pi < draw.baked.parts.size(); ++pi) {
            const glbparser::Part& src = draw.baked.parts[pi];
            AnimModelDraw::Part part;
            if (src.image >= 0 && src.image < (int)imageTex.size())
                part.tex = imageTex[src.image];
            // frame-0 upload sizes the (dynamic) buffer; poses overwrite it
            std::vector<float> interleaved((size_t)src.vertexCount * 8, 0.0f);
            part.mesh = uploadMesh(interleaved);
            draw.parts.push_back(part);
        }
    }
    return &animModelCache_.emplace(relPath, std::move(draw)).first->second;
}

// Interpolates the object's current pose (start clip + preview clock, the
// same frame lerp the PS2 does on VU1) into the part VBOs.
void Viewport::updateAnimPose(AnimModelDraw& draw, const SceneObject& o) {
    const glbparser::Baked& b = draw.baked;
    if (b.clips.empty()) return;
    const glbparser::Clip* clip = &b.clips.front();
    if (!o.animClip.empty())
        for (const glbparser::Clip& c : b.clips)
            if (c.name == o.animClip) {
                clip = &c;
                break;
            }

    // Fractional frame inside the clip. The preview always loops - a frozen
    // one-shot tells the user nothing about the motion.
    float pos = 0.0f;
    if (o.animAutoplay && clip->frameCount > 1) {
        const float speed = o.animSpeed > 0.01f ? o.animSpeed : 1.0f;
        pos = std::fmod((float)(animClock_ * b.fps * speed), (float)clip->frameCount);
    }
    const int local0 = (int)pos;
    const float alpha = pos - (float)local0;
    const int f0 = clip->firstFrame + local0;
    // looping wraps last -> first, like the engine's loop state
    const int f1 = clip->firstFrame + (local0 + 1) % clip->frameCount;

    std::vector<float> interleaved;
    for (size_t pi = 0; pi < draw.parts.size() && pi < b.parts.size(); ++pi) {
        const glbparser::Part& src = b.parts[pi];
        const size_t stride = (size_t)src.vertexCount * 3;
        const float* p0 = &src.positions[f0 * stride];
        const float* p1 = &src.positions[f1 * stride];
        const float* n0 = &src.normals[f0 * stride];
        const float* n1 = &src.normals[f1 * stride];
        interleaved.clear();
        interleaved.reserve((size_t)src.vertexCount * 8);
        for (int v = 0; v < src.vertexCount; ++v) {
            const float px = p0[v * 3] + (p1[v * 3] - p0[v * 3]) * alpha;
            const float py = p0[v * 3 + 1] + (p1[v * 3 + 1] - p0[v * 3 + 1]) * alpha;
            const float pz = p0[v * 3 + 2] + (p1[v * 3 + 2] - p0[v * 3 + 2]) * alpha;
            const Vec3 n = {n0[v * 3] + (n1[v * 3] - n0[v * 3]) * alpha,
                            n0[v * 3 + 1] + (n1[v * 3 + 1] - n0[v * 3 + 1]) * alpha,
                            n0[v * 3 + 2] + (n1[v * 3 + 2] - n0[v * 3 + 2]) * alpha};
            const Vec3 s = shadeOf(n);
            const bool hasUv = src.uvs.size() >= (size_t)(v + 1) * 2;
            interleaved.insert(
                interleaved.end(),
                {px, py, pz, s.x * src.baseColor[0], s.y * src.baseColor[1],
                 s.z * src.baseColor[2], hasUv ? src.uvs[v * 2] : 0.0f,
                 hasUv ? src.uvs[v * 2 + 1] : 0.0f});
        }
        glBindBuffer(GL_ARRAY_BUFFER, draw.parts[pi].mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(interleaved.size() * sizeof(float)),
                     interleaved.data(), GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Viewport::setSky(const float* horizonRgb, const float* topRgb, bool gradient,
                      float zenithSize) {
    for (int i = 0; i < 3; ++i) {
        sky_[i] = horizonRgb[i];
        skyTop_[i] = topRgb[i];
    }
    skyGradient_ = gradient;
    skyZenithSize_ = zenithSize < 0.05f ? 0.05f : (zenithSize > 0.95f ? 0.95f : zenithSize);
    skyQuadDirty_ = true;
}

void Viewport::orbit(float dx, float dy) {
    yaw_ += dx * 0.01f;
    pitch_ += dy * 0.01f;
    if (pitch_ < 0.05f) pitch_ = 0.05f;
    if (pitch_ > 1.5f) pitch_ = 1.5f;
}

void Viewport::zoom(float wheel) {
    // Continuous dolly: each unit of wheel scales distance by 0.9, so the old
    // one-notch feel (0.9x) is preserved while fractional/scaled input (dolly
    // drag, sensitivity multipliers) moves proportionally.
    distance_ *= std::pow(0.9f, wheel);
    if (distance_ < 2.0f) distance_ = 2.0f;
    if (distance_ > 2000.0f) distance_ = 2000.0f;
}

void Viewport::setTarget(const float target[3]) {
    target_[0] = target[0];
    target_[1] = target[1];
    target_[2] = target[2];
}

void Viewport::resetView() {
    // Match the initial framing chosen when a terrain is first loaded
    // (see setTerrain): pivot on the world origin, distance from the terrain
    // diagonal, and the default orbit orientation.
    target_[0] = target_[1] = target_[2] = 0.0f;
    yaw_ = 0.8f;
    pitch_ = 0.6f;
    float diag =
        (float)(terrain_.width > terrain_.depth ? terrain_.width : terrain_.depth);
    distance_ = diag > 0.0f ? diag * 1.4f : 90.0f;
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
                          const std::vector<int>& selection, int primary) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    ensureFramebuffer(width, height);

    // Preview clock for animated models (wall time; the editor redraws
    // continuously, so clips play at their real speed).
    animClock_ = std::chrono::duration<double>(
                     std::chrono::steady_clock::now().time_since_epoch())
                     .count();

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width, height);
    glClearColor(sky_[0], sky_[1], sky_[2], 1.0f);  // sky, matches the PS2 clear color
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Gradient sky dome (mirrors the PS2 sky dome in templates.cpp): a unit
    // hemisphere, colored horizon->zenith linearly by elevation over the 90°
    // up-arc, so the full 180° horizon-through-zenith-to-horizon sweep reads as
    // one coherent dome. Rebuilt only when the colors change; drawn later with
    // the real camera so it tracks pitch (the clear color already fills below
    // the horizon with the horizon color).
    if (skyGradient_ && skyQuadDirty_) {
        skyQuadDirty_ = false;
        destroyMesh(skyQuad_);
        std::vector<float> q;
        const int stacks = 12, slices = 24;
        // Zenith-size bias: pow(t, exp), exp = (1-size)/size. size 0.5 => exp 1
        // (linear); larger size => smaller exp => zenith color reaches lower.
        const float zExp = (1.0f - skyZenithSize_) / skyZenithSize_;
        auto domeVert = [&](int stack, int slice) {
            const float lat = -0.05f + (kPi * 0.5f + 0.05f) * stack / stacks;
            const float lon = 2.0f * kPi * slice / slices;
            const float t = std::pow((float)stack / stacks, zExp);
            const float r = std::cos(lat);
            pushVertexColor(q, r * std::cos(lon), std::sin(lat), r * std::sin(lon),
                            sky_[0] + (skyTop_[0] - sky_[0]) * t,
                            sky_[1] + (skyTop_[1] - sky_[1]) * t,
                            sky_[2] + (skyTop_[2] - sky_[2]) * t);
        };
        for (int st = 0; st < stacks; ++st)
            for (int sl = 0; sl < slices; ++sl) {
                domeVert(st, sl);     domeVert(st + 1, sl); domeVert(st + 1, sl + 1);
                domeVert(st, sl); domeVert(st + 1, sl + 1); domeVert(st, sl + 1);
            }
        skyQuad_ = uploadMesh(q);
    }

    Vec3 tgt{target_[0], target_[1], target_[2]};
    Vec3 eye{tgt.x + distance_ * std::cos(pitch_) * std::cos(yaw_),
             tgt.y + distance_ * std::sin(pitch_),
             tgt.z + distance_ * std::cos(pitch_) * std::sin(yaw_)};
    float fovDeg = 50.0f;
    // Cutscene Director camera-track preview: fly the preview camera along the
    // sequence's keyframed eye/look-at (the same values the PS2 runtime uses).
    if (camOverride_) {
        eye = {camEye_[0], camEye_[1], camEye_[2]};
        tgt = {camTarget_[0], camTarget_[1], camTarget_[2]};
        fovDeg = camFov_;
    }
    Mat4 view = lookAt(eye, tgt, {0, 1, 0});
    float diag = (float)(terrain_.width > terrain_.depth ? terrain_.width : terrain_.depth);
    Mat4 proj = perspective(fovDeg * kPi / 180.0f, (float)width / (float)height, 0.1f,
                            diag * 10.0f + 100.0f);
    Mat4 viewProj = mul(proj, view);
    for (int i = 0; i < 16; ++i) {
        viewM_[i] = view.m[i];
        projM_[i] = proj.m[i];
    }

    glUseProgram(program_);
    // uOpacity persists across draws (the sky/outline sites below don't set
    // it) - reset the mirror pass's leftover so the frame starts opaque.
    glUniform1f(uOpacity_, 1.0f);

    // Sky dome: centered on the camera (an "infinite" sky) and scaled well
    // past the scene but inside the far plane, drawn first with no depth so
    // the scene paints over it. Colors interpolate by elevation (built above).
    if (skyGradient_ && skyQuad_.vertexCount) {
        const float skyR = diag * 8.0f + 80.0f;
        Mat4 skyModel =
            mul(translation(eye.x, eye.y, eye.z), scaleM(skyR, skyR, skyR));
        Mat4 skyMvp = mul(viewProj, skyModel);
        glDisable(GL_DEPTH_TEST);
        glUniformMatrix4fv(uMvp_, 1, GL_FALSE, skyMvp.m);
        glUniform3f(uTint_, 1.0f, 1.0f, 1.0f);
        glUniform1i(uLit_, 0);
        glBindVertexArray(skyQuad_.vao);
        glDrawArrays(GL_TRIANGLES, 0, skyQuad_.vertexCount);
        glEnable(GL_DEPTH_TEST);
    }

    // GS hardware fog preview: same coefficient the VU1 computes in-game.
    {
        Vec3 fwd{tgt.x - eye.x, tgt.y - eye.y, tgt.z - eye.z};
        float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
        if (len > 1e-5f) fwd = {fwd.x / len, fwd.y / len, fwd.z / len};
        glUniform1i(uFogOn_, fogOn_ ? 1 : 0);
        glUniform3f(uFogColor_, fogColor_[0], fogColor_[1], fogColor_[2]);
        glUniform1f(uFogStart_, fogStart_);
        glUniform1f(uFogEnd_, fogEnd_ > fogStart_ + 1.0f ? fogEnd_ : fogStart_ + 1.0f);
        glUniform3f(uFogEye_, eye.x, eye.y, eye.z);
        glUniform3f(uFogFwd_, fwd.x, fwd.y, fwd.z);

        // "@sky" reflective materials sample the live sky gradient
        glUniform3f(uReflSkyHorizon_, sky_[0], sky_[1], sky_[2]);
        glUniform3f(uReflSkyTop_, skyTop_[0], skyTop_[1], skyTop_[2]);

        // Camera flashlight preview (same constants the engine derives)
        glUniform1i(uFlashOn_, flashOn_ ? 1 : 0);
        glUniform3f(uFlashCol_, flashColor_[0], flashColor_[1], flashColor_[2]);
        const float r2 = flashRange_ * flashRange_;
        const float cosCut = std::cos(flashAngle_ * kPi / 180.0f);
        const float cut2 = cosCut * cosCut;
        const float coneBase = r2 * (1.0f - cut2);
        glUniform1f(uFlashInvR2_, 1.0f / r2);
        glUniform1f(uFlashCut2_, cut2);
        glUniform1f(uFlashSoft_, coneBase > 1e-10f ? 3.0f / coneBase : 0.0f);
    }

    // Point lights in the scene -> fragment shader uniforms (live preview of
    // what the game bakes into vertex colors; capped at the shader's 8).
    {
        float pos[8 * 4] = {};
        float col[8 * 4] = {};
        int count = 0;
        for (size_t oi = 0; oi < objects.size(); ++oi) {
            const SceneObject& o = objects[oi];
            if (o.type != PrimitiveType::PointLight || count >= 8) continue;
            if (hiddenAt(oi)) continue;  // hidden layer - light preview off too
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
                    float b, uint32_t texture = 0, const Mat4* model = nullptr,
                    bool alpha = false, float opacity = 1.0f,
                    uint32_t reflTex = 0, float reflStrength = 0.0f,
                    bool reflSky = false, bool reflRounded = false,
                    const float* reflCenter = nullptr) {
        glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp.m);
        glUniformMatrix4fv(uModel_, 1, GL_FALSE, model ? model->m : identityM.m);
        glUniform1i(uLit_, model ? 1 : 0);  // world matrix given = lit geometry
        glUniform3f(uTint_, r, g, b);
        glUniform1i(uUseTex_, texture ? 1 : 0);
        glUniform1i(uAlpha_, alpha ? 1 : 0);
        glUniform1i(uReflOn_, reflSky ? 2 : (reflTex ? 1 : 0));
        if (reflTex || reflSky) {
            glUniform1f(uReflStrength_, reflStrength);
            glUniform1i(uReflRounded_, reflRounded ? 1 : 0);
            if (reflRounded && reflCenter)
                glUniform3f(uReflCenter_, reflCenter[0], reflCenter[1],
                            reflCenter[2]);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, reflTex);
            glActiveTexture(GL_TEXTURE0);
        }
        glUniform1f(uOpacity_, opacity);
        const bool blend = alpha || opacity < 1.0f;
        if (blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        if (texture) glBindTexture(GL_TEXTURE_2D, texture);
        glBindVertexArray(mesh.vao);
        glDrawArrays(mode, 0, mesh.vertexCount);
        if (blend) glDisable(GL_BLEND);
    };

    auto meshFor = [&](const SceneObject& o) -> const Mesh* {
        switch (o.type) {
            case PrimitiveType::Box:
            case PrimitiveType::Sphere:
            case PrimitiveType::Cylinder:
            case PrimitiveType::Cone:
            case PrimitiveType::SavePoint: return &primMesh(o.type, o.primDetail);
            case PrimitiveType::Plane: return &plane_;
            case PrimitiveType::Decal: return &decal_;
            case PrimitiveType::Mirror: return &decal_;  // glass quad, +Z face
            case PrimitiveType::Portal: return &decal_;  // surface quad, +Z face
            case PrimitiveType::SpawnPoint: return &spawnMarker_;
            case PrimitiveType::Player: return &playerMarker_;
            case PrimitiveType::SoundEmitter: return &sphere_;  // speaker-ish marker
            case PrimitiveType::Empty: return &sphere_;  // pure-transform marker
            case PrimitiveType::PointLight: return &lightGizmo_;  // glowing bulb
            case PrimitiveType::Camera: return &cameraBody_;  // film camera body
            case PrimitiveType::Model: return &box_;  // placeholder (see model path below)
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
        for (const Mesh& chunk : terrainChunkMeshes_)
            draw(chunk, GL_TRIANGLES, viewProj, tintScale, tintScale, tintScale,
                 terrainTex, asLines ? nullptr : &identityM);

        // Painted terrain layers: alpha-blend each layer's pass over the base
        // chunks - the GL twin of the PS2's two-pass splatting (renderTerrain
        // in templates.cpp). Same geometry, and the frame already runs with
        // LEQUAL depth, so the equal-depth pass wins; no depth writes so later
        // object draws still z-test against the base terrain.
        if (!asLines && !terrainLayerMeshes_.empty()) {
            glUseProgram(particleProgram_);
            glUniformMatrix4fv(uPartMvp_, 1, GL_FALSE, viewProj.m);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            const size_t chunkCount = (size_t)tcChunksX_ * tcChunksZ_;
            for (size_t l = 0; l < terrainLayers_.size(); ++l) {
                const uint32_t tex = glTexture(terrainLayers_[l].texture);
                glUniform1i(uPartUseTex_, tex ? 1 : 0);
                if (tex) glBindTexture(GL_TEXTURE_2D, tex);
                for (size_t ci = 0; ci < chunkCount; ++ci) {
                    const Mesh& lm = terrainLayerMeshes_[l * chunkCount + ci];
                    if (!lm.vao || lm.vertexCount == 0) continue;
                    glBindVertexArray(lm.vao);
                    glDrawArrays(GL_TRIANGLES, 0, lm.vertexCount);
                }
            }
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glUseProgram(program_);
        }
        for (size_t oi = 0; oi < objects.size(); ++oi) {
            if (hiddenAt(oi)) continue;
            const SceneObject& o = objects[oi];
            // The camera(s) being previewed through don't draw their body -
            // it would sit on the near plane and cover the whole view.
            if (o.type == PrimitiveType::Camera && camHidden(o.name)) continue;
            // Emitters preview as live particles (drawn after the scene); in
            // the scene pass they only get a small fixed-size cone marker so
            // the gizmo has something to grab. Dimmed when disabled.
            if (o.type == PrimitiveType::Emitter) {
                // rotation aims the cone with the emission direction (custom)
                const float d2r = kPi / 180.0f;
                Mat4 marker = scaleM(0.7f, 0.7f, 0.7f);
                marker = mul(rotX(o.rotation[0] * d2r), marker);
                marker = mul(rotY(o.rotation[1] * d2r), marker);
                marker = mul(rotZ(o.rotation[2] * d2r), marker);
                marker = mul(
                    translation(o.position[0], o.position[1], o.position[2]),
                    marker);
                const float dim = o.emitterEnabled ? 1.0f : 0.35f;
                draw(cone_, GL_TRIANGLES, mul(viewProj, marker),
                     o.color[0] * dim * tintScale, o.color[1] * dim * tintScale,
                     o.color[2] * dim * tintScale);
                continue;
            }
            // Mirrors draw in their own pass after the scene (reflected
            // copies first, glass blended over them); portals blend their
            // surface after the scene too. The wire passes still outline
            // both quads here so wireframe views show the rectangle.
            if ((o.type == PrimitiveType::Mirror ||
                 o.type == PrimitiveType::Portal) && !asLines)
                continue;
            const Mat4 model = modelMatrix(o);
            const Mat4 mvp = mul(viewProj, model);
            // the bulb gizmo stays emissive - everything else receives light
            const bool lit = !asLines && o.type != PrimitiveType::PointLight;
            // animated .glb models: dynamic meshes re-lerped this frame. A
            // third-person Player (mode 2) previews as its own avatar model,
            // the same way NPC animated models draw.
            const bool tppAvatar = o.type == PrimitiveType::Player &&
                                   o.playerMode == 2 &&
                                   isAnimatedModelPath(o.modelPath);
            if ((o.type == PrimitiveType::Model || tppAvatar) &&
                isAnimatedModelPath(o.modelPath)) {
                AnimModelDraw* ad = animModelDraw(o.modelPath);
                if (ad && ad->ok) {
                    updateAnimPose(*ad, o);
                    for (const AnimModelDraw::Part& part : ad->parts)
                        draw(part.mesh, GL_TRIANGLES, mvp, o.color[0] * tintScale,
                             o.color[1] * tintScale, o.color[2] * tintScale,
                             asLines ? 0 : part.tex, lit ? &model : nullptr);
                    continue;
                }
                // unusable .glb falls through to the placeholder box
            }
            // .obj models draw one part per MTL material (an assigned .mtl
            // overrides the model's own libraries - same rule as the game)
            const ModelDraw* md = o.type == PrimitiveType::Model
                                      ? modelDraw(o.modelPath, o.materialPath)
                                      : nullptr;
            if (md) {
                for (const ModelPart& part : md->parts) {
                    // rounded env normals radiate from the part centroid -
                    // transform it to world space with the object matrix
                    float c[3] = {0, 0, 0};
                    if (part.reflRounded) {
                        const float* mm = model.m;
                        for (int a = 0; a < 3; ++a)
                            c[a] = mm[a] * part.centroid[0] +
                                   mm[a + 4] * part.centroid[1] +
                                   mm[a + 8] * part.centroid[2] + mm[a + 12];
                    }
                    draw(part.mesh, GL_TRIANGLES, mvp, o.color[0] * tintScale,
                         o.color[1] * tintScale, o.color[2] * tintScale,
                         asLines ? 0 : part.tex, lit ? &model : nullptr, false,
                         1.0f, asLines ? 0 : part.reflTex, part.reflStrength,
                         asLines ? false : part.reflSky, part.reflRounded, c);
                }
                continue;
            }
            // primitives: the assigned material's first entry = Kd tint + map_Kd
            const MaterialDraw* mat =
                o.type == PrimitiveType::Model ? nullptr : materialDraw(o.materialPath);
            const float kr = mat ? mat->kd[0] : 1.0f;
            const float kg = mat ? mat->kd[1] : 1.0f;
            const float kb = mat ? mat->kd[2] : 1.0f;
            const uint32_t tex = (asLines || !mat) ? 0 : mat->tex;
            // Decals honor their texture's alpha (cutout + blend) - matches the
            // in-game look; other primitives draw opaque.
            const bool decalAlpha = o.type == PrimitiveType::Decal && tex && !asLines;
            // Projecting decal: draw the precomputed world-space conforming mesh
            // (identity transform, unlit) instead of the flat quad. Falls back to
            // the flat quad when projection produced nothing (no receiver/material).
            if (o.type == PrimitiveType::Decal && o.decalProject) {
                auto it = projectedDecalMeshes_.find(o.id);
                if (it != projectedDecalMeshes_.end() && it->second.vertexCount > 0) {
                    draw(it->second, GL_TRIANGLES, viewProj, o.color[0] * kr * tintScale,
                         o.color[1] * kg * tintScale, o.color[2] * kb * tintScale, tex,
                         nullptr, decalAlpha);
                    continue;
                }
            }
            // primitives are modeled around their local origin, so the
            // rounded-normal centre is simply the object's world position
            draw(*meshFor(o), GL_TRIANGLES, mvp, o.color[0] * kr * tintScale,
                 o.color[1] * kg * tintScale, o.color[2] * kb * tintScale,
                 tex, lit ? &model : nullptr, decalAlpha, 1.0f,
                 (asLines || !mat) ? 0 : mat->reflTex,
                 mat ? mat->reflStrength : 0.0f,
                 (asLines || !mat) ? false : mat->reflSky,
                 mat ? mat->reflRounded : false, o.position);
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

    // Mirror objects: draw the reflected copies first (real geometry behind
    // the plane, z-tested against the finished scene), then blend the glass
    // quad over them - the same order the generated game uses. Skipped in
    // pure wireframe (the wire passes already outline the quad).
    if (viewMode_ != ViewMode::Wireframe) {
        // one reflected draw - the static subset of the scene pass (marker
        // types never make it into a mirror list)
        auto drawReflected = [&](const SceneObject& t, const Mat4& model) {
            const Mat4 mvp = mul(viewProj, model);
            if (t.type == PrimitiveType::Model && isAnimatedModelPath(t.modelPath)) {
                // pose already advanced by this frame's scene pass - reuse it
                AnimModelDraw* ad = animModelDraw(t.modelPath);
                if (ad && ad->ok) {
                    for (const AnimModelDraw::Part& part : ad->parts)
                        draw(part.mesh, GL_TRIANGLES, mvp, t.color[0], t.color[1],
                             t.color[2], part.tex, &model);
                    return;
                }
            }
            const ModelDraw* md = t.type == PrimitiveType::Model
                                      ? modelDraw(t.modelPath, t.materialPath)
                                      : nullptr;
            if (md) {
                for (const ModelPart& part : md->parts)
                    draw(part.mesh, GL_TRIANGLES, mvp, t.color[0], t.color[1],
                         t.color[2], part.tex, &model);
                return;
            }
            const MaterialDraw* mat =
                t.type == PrimitiveType::Model ? nullptr : materialDraw(t.materialPath);
            const float kr = mat ? mat->kd[0] : 1.0f;
            const float kg = mat ? mat->kd[1] : 1.0f;
            const float kb = mat ? mat->kd[2] : 1.0f;
            const uint32_t tex = mat ? mat->tex : 0;
            const bool decalAlpha = t.type == PrimitiveType::Decal && tex;
            draw(*meshFor(t), GL_TRIANGLES, mvp, t.color[0] * kr, t.color[1] * kg,
                 t.color[2] * kb, tex, &model, decalAlpha);
        };
        for (size_t mi = 0; mi < objects.size(); ++mi) {
            const SceneObject& m = objects[mi];
            if (m.type != PrimitiveType::Mirror || hiddenAt(mi)) continue;
            const Mat4 mirrorModel = modelMatrix(m);
            // plane through the mirror position, normal = the rotated +Z face
            // (third model-matrix column, normalized)
            Vec3 n = normalize({mirrorModel.m[8], mirrorModel.m[9], mirrorModel.m[10]});
            if (n.x == 0.0f && n.y == 0.0f && n.z == 0.0f) n = {0, 0, 1};
            const float d =
                n.x * m.position[0] + n.y * m.position[1] + n.z * m.position[2];
            // Householder reflection about the plane: x' = x - 2((x.n) - d) n
            Mat4 refl = identity();
            refl.m[0] = 1.0f - 2.0f * n.x * n.x;
            refl.m[1] = -2.0f * n.x * n.y;
            refl.m[2] = -2.0f * n.x * n.z;
            refl.m[4] = -2.0f * n.x * n.y;
            refl.m[5] = 1.0f - 2.0f * n.y * n.y;
            refl.m[6] = -2.0f * n.y * n.z;
            refl.m[8] = -2.0f * n.x * n.z;
            refl.m[9] = -2.0f * n.y * n.z;
            refl.m[10] = 1.0f - 2.0f * n.z * n.z;
            refl.m[12] = 2.0f * d * n.x;
            refl.m[13] = 2.0f * d * n.y;
            refl.m[14] = 2.0f * d * n.z;
            for (const std::string& tname : m.mirrorObjects) {
                int ti = -1;
                for (size_t k = 0; k < objects.size(); ++k)
                    if (objects[k].name == tname) { ti = (int)k; break; }
                if (ti < 0 || hiddenAt((size_t)ti)) continue;
                drawReflected(objects[(size_t)ti],
                              mul(refl, modelMatrix(objects[(size_t)ti])));
            }
            // "Reflect player": only a third-person avatar has a body to show
            // (same rule as the game - an FPP player casts no reflection)
            if (m.mirrorReflectPlayer) {
                for (size_t k = 0; k < objects.size(); ++k) {
                    const SceneObject& p = objects[k];
                    if (p.type != PrimitiveType::Player || p.playerMode != 2 ||
                        !isAnimatedModelPath(p.modelPath) || hiddenAt(k))
                        continue;
                    AnimModelDraw* ad = animModelDraw(p.modelPath);
                    if (ad && ad->ok) {
                        const Mat4 model = mul(refl, modelMatrix(p));
                        const Mat4 mvp = mul(viewProj, model);
                        for (const AnimModelDraw::Part& part : ad->parts)
                            draw(part.mesh, GL_TRIANGLES, mvp, p.color[0],
                                 p.color[1], p.color[2], part.tex, &model);
                    }
                    break;  // first player entity wins, like in the game
                }
            }
            // glass quad last, blended over whatever the copies drew
            draw(decal_, GL_TRIANGLES, mul(viewProj, mirrorModel), m.color[0],
                 m.color[1], m.color[2], 0, &mirrorModel, false, m.mirrorOpacity);
        }
    }

    // Portal surfaces: the editor has no live through-view (that is the
    // game's in-place render), so the quad blends as a translucent energy
    // surface in the object color - linked pairs read via the link line
    // drawn with the other gizmos below. The entry-side arrow (out of the
    // +Z front face - the side that shows the view and teleports) draws in
    // every view mode: authoring needs the orientation at a glance.
    if (viewMode_ != ViewMode::Wireframe) {
        for (size_t pi = 0; pi < objects.size(); ++pi) {
            const SceneObject& p = objects[pi];
            if (p.type != PrimitiveType::Portal || hiddenAt(pi)) continue;
            const Mat4 model = modelMatrix(p);
            draw(decal_, GL_TRIANGLES, mul(viewProj, model), p.color[0],
                 p.color[1], p.color[2], 0, &model, false, 0.7f);
        }
    }
    for (size_t pi = 0; pi < objects.size(); ++pi) {
        const SceneObject& p = objects[pi];
        if (p.type != PrimitiveType::Portal || hiddenAt(pi)) continue;
        const float d2r = kPi / 180.0f;
        Mat4 m = scaleM(1.2f, 1.2f, 1.2f);  // fixed length, quad-scale-free
        m = mul(rotX(p.rotation[0] * d2r), m);
        m = mul(rotY(p.rotation[1] * d2r), m);
        m = mul(rotZ(p.rotation[2] * d2r), m);
        m = mul(translation(p.position[0], p.position[1], p.position[2]), m);
        // brightened toward white so it pops against the tinted surface
        draw(portalArrow_, GL_LINES, mul(viewProj, m),
             0.5f + 0.5f * p.color[0], 0.5f + 0.5f * p.color[1],
             0.5f + 0.5f * p.color[2]);
    }

    // Grid lines, axes and the selection outline are unaffected by view mode
    for (const Mesh& chunkLines : terrainLineMeshes_)
        draw(chunkLines, GL_LINES, viewProj, 1.0f, 1.0f, 1.0f);
    draw(axes_, GL_LINES, viewProj, 1.0f, 1.0f, 1.0f);

    // Nav-mesh overlay: translucent green quads over the walkable cells
    // (View > Nav Mesh Overlay; baked app-side, see setNavOverlay).
    if (navOverlayOn_ && navOverlayMesh_.vertexCount)
        draw(navOverlayMesh_, GL_TRIANGLES, viewProj, 1.0f, 1.0f, 1.0f, 0,
             nullptr, false, 0.4f);

    // Point-light reach: a ring sphere at each light, scaled to its radius and
    // tinted with the light color (a rough preview of the lit volume).
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const SceneObject& o = objects[oi];
        if (o.type != PrimitiveType::PointLight || hiddenAt(oi)) continue;
        const float r = o.lightRadius > 0.01f ? o.lightRadius : 0.01f;
        const Mat4 m = mul(translation(o.position[0], o.position[1], o.position[2]),
                           scaleM(r, r, r));
        draw(wireSphere_, GL_LINES, mul(viewProj, m), o.color[0], o.color[1], o.color[2]);
    }

    // Camera entity FOV frustum: the unit frustum scaled to the entity's FOV
    // (X/Y by tan(fov/2), Z = display length), rotated/positioned with the
    // entity but ignoring its scale so the wedge always reads the actual shot.
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const SceneObject& o = objects[oi];
        if (o.type != PrimitiveType::Camera || hiddenAt(oi)) continue;
        if (camHidden(o.name)) continue;  // previewing through this camera
        const float d2r = kPi / 180.0f;
        const float len = 2.4f;
        const float t = std::tan(0.5f * o.cameraFov * d2r);
        Mat4 m = scaleM(t * len, t * len, len);
        m = mul(rotX(o.rotation[0] * d2r), m);
        m = mul(rotY(o.rotation[1] * d2r), m);
        m = mul(rotZ(o.rotation[2] * d2r), m);
        m = mul(translation(o.position[0], o.position[1], o.position[2]), m);
        draw(cameraFrustum_, GL_LINES, mul(viewProj, m), o.color[0], o.color[1],
             o.color[2]);
    }

    // Portal link line: portal -> its target portal, in the portal's color.
    // Only the segment's third matrix column and translation matter (the
    // mesh's two vertices sit at local z=0 and z=1).
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const SceneObject& o = objects[oi];
        if (o.type != PrimitiveType::Portal || hiddenAt(oi)) continue;
        if (o.portalTarget.empty()) continue;
        const SceneObject* tgt = nullptr;
        for (const SceneObject& t : objects)
            if (t.type == PrimitiveType::Portal && t.name == o.portalTarget) {
                tgt = &t;
                break;
            }
        if (!tgt) continue;
        Mat4 seg = identity();
        seg.m[8] = tgt->position[0] - o.position[0];
        seg.m[9] = tgt->position[1] - o.position[1];
        seg.m[10] = tgt->position[2] - o.position[2];
        seg.m[12] = o.position[0];
        seg.m[13] = o.position[1];
        seg.m[14] = o.position[2];
        draw(segment_, GL_LINES, mul(viewProj, seg), o.color[0], o.color[1],
             o.color[2]);
    }

    // "Highlight usable objects" preference: wire box in the highlight color
    // around every usable object (the proximity condition only applies
    // in-game; the editor just shows which objects qualify and the color).
    if (usableHighlight_) {
        for (size_t oi = 0; oi < objects.size(); ++oi) {
            const SceneObject& o = objects[oi];
            if (!o.usable || hiddenAt(oi)) continue;
            const Mat4 mvp = mul(viewProj, modelMatrix(o));
            draw(wireCube_, GL_LINES, mvp, usableHighlightCol_[0],
                 usableHighlightCol_[1], usableHighlightCol_[2]);
        }
    }

    // Session peers' selections first (their color), so the local outline
    // below always draws on top when both select the same object.
    for (const PeerSel& ps : peerSels_) {
        for (int idx : ps.indices) {
            if (idx < 0 || idx >= (int)objects.size() || hiddenAt((size_t)idx)) continue;
            const Mat4 mvp = mul(viewProj, modelMatrix(objects[idx]));
            draw(wireCube_, GL_LINES, mvp, ps.color[0], ps.color[1], ps.color[2]);
        }
    }

    // Every selected object gets an amber outline; the primary is drawn in a
    // brighter yellow so it's clear which object's values seed the multi-editor.
    // Objects on a hidden layer are skipped (they aren't drawn or picked).
    for (int idx : selection) {
        if (idx < 0 || idx >= (int)objects.size() || hiddenAt((size_t)idx)) continue;
        const Mat4 mvp = mul(viewProj, modelMatrix(objects[idx]));
        if (idx == primary) draw(wireCube_, GL_LINES, mvp, 1.0f, 0.85f, 0.35f);
        else draw(wireCube_, GL_LINES, mvp, 1.0f, 0.6f, 0.1f);
    }

    // Particle emitters last - alpha blended over the scene (same order as
    // the generated game's renderScene()).
    {
        const Vec3 fwd = normalize(sub(tgt, eye));
        drawEmitterPreviews(objects, viewProj.m, &eye.x, &fwd.x);
    }

    glBindVertexArray(0);

    // Color grading preview: full-screen pass colorTex_ -> gradeTex_ with
    // the PS2 GS math (see GRADE_FS). Skipped entirely when neutral.
    if (gradingOn_ && gradeProgram_) {
        glBindFramebuffer(GL_FRAMEBUFFER, gradeFbo_);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(gradeProgram_);
        glBindTexture(GL_TEXTURE_2D, colorTex_);
        glUniform1i(uGradeSrc_, 0);
        glUniform3f(uGradeGain_, (float)grading_.gain[0], (float)grading_.gain[1],
                    (float)grading_.gain[2]);
        auto liftPart = [&](int c, bool positive) {
            const int l = grading_.lift[c];
            return (float)(positive ? (l > 0 ? l : 0) : (l < 0 ? -l : 0));
        };
        glUniform3f(uGradeLiftPos_, liftPart(0, true), liftPart(1, true),
                    liftPart(2, true));
        glUniform3f(uGradeLiftNeg_, liftPart(0, false), liftPart(1, false),
                    liftPart(2, false));
        glUniform3f(uGradeMixCol_, (float)grading_.mixColor[0],
                    (float)grading_.mixColor[1], (float)grading_.mixColor[2]);
        glUniform1f(uGradeMixAmt_, (float)grading_.mixAmt);
        glBindVertexArray(gradeVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return gradeTex_;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return colorTex_;
}

// Material Editor live preview: gradient backdrop, checker floor and one unit
// primitive - or a project .obj model - with the material's Kd tint + map_Kd
// texture. Shares the scene shader and unit meshes, so the shading matches the
// viewport (and the PS2 bake). The camera orbits instead of the shape spinning
// - the directional shade is baked into the mesh vertex colors, so rotating
// the mesh would drag the light along with it.
uint32_t Viewport::renderMaterialPreview(int width, int height,
                                         const MatPreviewDesc& d) {
    if (!program_) return 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    ensurePreviewFramebuffer(width, height);

    if (!prevBg_.vao) {
        std::vector<float> q;
        const float bot[3] = {0.09f, 0.10f, 0.13f}, top[3] = {0.24f, 0.27f, 0.34f};
        pushVertexColor(q, -1, -1, 0, bot[0], bot[1], bot[2]);
        pushVertexColor(q, 1, -1, 0, bot[0], bot[1], bot[2]);
        pushVertexColor(q, 1, 1, 0, top[0], top[1], top[2]);
        pushVertexColor(q, -1, -1, 0, bot[0], bot[1], bot[2]);
        pushVertexColor(q, 1, 1, 0, top[0], top[1], top[2]);
        pushVertexColor(q, -1, 1, 0, top[0], top[1], top[2]);
        prevBg_ = uploadMesh(q);
    }
    if (!prevFloor_.vao) {
        // 8x8 checker plate, unit extents at y=0 - scaled/placed per draw so
        // it can sit under a unit shape and under an arbitrary-size model
        std::vector<float> f;
        const int n = 8;
        const float ext = 2.0f, cell = 2.0f * ext / n;
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x) {
                const float g = ((x + z) % 2 == 0) ? 0.30f : 0.235f;
                const float ax = -ext + x * cell, az = -ext + z * cell;
                const float bx = ax + cell, bz = az + cell;
                pushVertexColor(f, ax, 0, az, g, g, g);
                pushVertexColor(f, bx, 0, az, g, g, g);
                pushVertexColor(f, ax, 0, bz, g, g, g);
                pushVertexColor(f, bx, 0, az, g, g, g);
                pushVertexColor(f, bx, 0, bz, g, g, g);
                pushVertexColor(f, ax, 0, bz, g, g, g);
            }
        prevFloor_ = uploadMesh(f);
    }

    // Model slot (shape 4). A missing/unparseable model falls back to the
    // sphere so the window never goes blank.
    const MatPrevModel* model = nullptr;
    int shape = d.shape;
    if (shape == 4) {
        model = matPrevModelDraw(d.modelRel, d.mtlRel);
        if (!model->ok) {
            model = nullptr;
            shape = 1;
        }
    }
    if (!model && (shape < 0 || shape > 3)) shape = 1;

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo_);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glUseProgram(program_);
    glUniform1i(uLightCount_, 0);  // no point lights in the preview

    const Mat4 id = identity();
    auto draw = [&](const Mesh& mesh, const Mat4& mvp, float r, float g, float b,
                    uint32_t texture, uint32_t reflTex = 0,
                    float reflStrength = 0.0f, bool reflSky = false,
                    bool reflRounded = false,
                    const float* reflCenter = nullptr) {
        glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp.m);
        glUniformMatrix4fv(uModel_, 1, GL_FALSE, id.m);
        glUniform1i(uLit_, 0);
        glUniform3f(uTint_, r, g, b);
        glUniform1i(uUseTex_, texture ? 1 : 0);
        glUniform1i(uAlpha_, 0);
        glUniform1i(uReflOn_, reflSky ? 2 : (reflTex ? 1 : 0));
        if (reflTex || reflSky) {
            glUniform1f(uReflStrength_, reflStrength);
            glUniform1i(uReflRounded_, reflRounded ? 1 : 0);
            if (reflRounded && reflCenter)
                glUniform3f(uReflCenter_, reflCenter[0], reflCenter[1],
                            reflCenter[2]);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, reflTex);
            glActiveTexture(GL_TEXTURE0);
        }
        if (texture) glBindTexture(GL_TEXTURE_2D, texture);
        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    };
    glUniform1i(uFogOn_, 0);  // no fog in the preview scene
    // "@sky" reflective materials sample the project's sky gradient
    glUniform3f(uReflSkyHorizon_, sky_[0], sky_[1], sky_[2]);
    glUniform3f(uReflSkyTop_, skyTop_[0], skyTop_[1], skyTop_[2]);

    // backdrop: NDC-space gradient quad, no depth
    glDisable(GL_DEPTH_TEST);
    draw(prevBg_, id, 1.0f, 1.0f, 1.0f, 0);
    glEnable(GL_DEPTH_TEST);

    // Orbit camera around the shown geometry. Unit shapes keep the historical
    // framing (dist 2.1, pivot slightly below center); models frame their AABB.
    Vec3 center{0.0f, -0.08f, 0.0f};
    float baseDist = 2.1f, floorY = -0.501f, floorScale = 1.0f;
    if (model) {
        center = {model->center[0], model->center[1], model->center[2]};
        baseDist = model->radius * 2.6f;
        floorY = model->minY - model->radius * 0.002f;
        floorScale = model->radius;
    }
    float zoom = d.zoom < 0.05f ? 0.05f : (d.zoom > 16.0f ? 16.0f : d.zoom);
    const float dist = baseDist / zoom;
    float pitch = d.pitchDeg;
    if (pitch < -5.0f) pitch = -5.0f;
    if (pitch > 85.0f) pitch = 85.0f;
    const float a = d.angleDeg * kPi / 180.0f;
    const float p = pitch * kPi / 180.0f;
    const Vec3 eye{center.x + dist * std::cos(p) * std::cos(a),
                   center.y + dist * std::sin(p),
                   center.z + dist * std::cos(p) * std::sin(a)};
    const Mat4 view = lookAt(eye, center, {0, 1, 0});
    {
        // The refl (matcap) shader path derives the camera basis from
        // uFogEye/uFogFwd - point them at the preview camera (fog itself is
        // off above).
        const Vec3 f = normalize(sub(center, eye));
        glUniform3f(uFogEye_, eye.x, eye.y, eye.z);
        glUniform3f(uFogFwd_, f.x, f.y, f.z);
    }
    const float zFar = dist + (model ? model->radius : 1.0f) * 4.0f + 50.0f;
    const Mat4 proj = perspective(45.0f * kPi / 180.0f,
                                  (float)width / (float)height,
                                  dist * 0.01f < 0.01f ? 0.01f : dist * 0.01f, zFar);
    const Mat4 viewProj = mul(proj, view);

    {
        Mat4 floorM = mul(translation(center.x, floorY, center.z),
                          scaleM(floorScale, 1.0f, floorScale));
        if (!model) floorM = translation(0.0f, floorY, 0.0f);
        draw(prevFloor_, mul(viewProj, floorM), 1.0f, 1.0f, 1.0f, 0);
    }

    if (model) {
        for (const MatPrevPart& part : matPrevModel_.parts) {
            const bool staged = part.material == d.entryName;
            const float* kd = staged ? d.kd : part.kd;
            const std::string& tex = staged ? d.texRel : part.texRel;
            const std::string& refl = staged ? d.reflRel : part.reflRel;
            draw(part.mesh, viewProj, kd[0], kd[1], kd[2], glTexture(tex),
                 glTexture(refl),
                 staged ? d.reflStrength : part.reflStrength,
                 staged ? d.reflSky : part.reflSky,
                 staged ? d.reflRounded : part.reflRounded, part.centroid);
        }
    } else {
        // unit shapes sit at the origin - that's the rounded-normal centre
        static const float origin[3] = {0.0f, 0.0f, 0.0f};
        const Mesh* mesh = shape == 0   ? &box_
                           : shape == 2 ? &cylinder_
                           : shape == 3 ? &cone_
                                        : &sphere_;
        draw(*mesh, viewProj, d.kd[0], d.kd[1], d.kd[2], glTexture(d.texRel),
             glTexture(d.reflRel), d.reflStrength, d.reflSky, d.reflRounded,
             origin);
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Snapshot the camera for materialPreviewPick (paint raycast)
    matPrevPick_.valid = true;
    matPrevPick_.shape = model ? 4 : shape;
    matPrevPick_.entryName = d.entryName;
    const Vec3 fwd = normalize(sub(center, eye));
    const Vec3 right = normalize(cross(fwd, {0, 1, 0}));
    const Vec3 up = cross(right, fwd);
    matPrevPick_.eye[0] = eye.x, matPrevPick_.eye[1] = eye.y, matPrevPick_.eye[2] = eye.z;
    matPrevPick_.fwd[0] = fwd.x, matPrevPick_.fwd[1] = fwd.y, matPrevPick_.fwd[2] = fwd.z;
    matPrevPick_.right[0] = right.x, matPrevPick_.right[1] = right.y,
    matPrevPick_.right[2] = right.z;
    matPrevPick_.up[0] = up.x, matPrevPick_.up[1] = up.y, matPrevPick_.up[2] = up.z;
    matPrevPick_.tanHalf = std::tan(45.0f * kPi / 180.0f * 0.5f);
    matPrevPick_.aspect = (float)width / (float)height;

    // CPU triangles for the primitive shapes, built once from the same
    // generators as the drawn meshes (default detail).
    if (!model && prevShapeTris_[shape].empty()) {
        const std::vector<float> src = shape == 0   ? unitBox()
                                       : shape == 2 ? unitCylinder()
                                       : shape == 3 ? unitCone()
                                                    : unitSphere();
        std::vector<float>& tris = prevShapeTris_[shape];
        tris.reserve(src.size() / 8 * 5);
        for (size_t i = 0; i + 7 < src.size(); i += 8)
            tris.insert(tris.end(),
                        {src[i], src[i + 1], src[i + 2], src[i + 6], src[i + 7]});
    }

    return prevTex_;
}

// Ray/triangle sweep over the CPU copy of the last material-preview geometry.
// Nearest hit wins; the hit's texture UV comes from barycentric interpolation
// (the same UVs the GPU sampled, so the painted texel lands under the cursor).
bool Viewport::materialPreviewPick(float u, float v, float& outU, float& outV,
                                   bool& paintable) const {
    if (!matPrevPick_.valid) return false;
    const MatPrevPick& pk = matPrevPick_;
    const Vec3 eye{pk.eye[0], pk.eye[1], pk.eye[2]};
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = 1.0f - v * 2.0f;
    const float sx = ndcX * pk.tanHalf * pk.aspect;
    const float sy = ndcY * pk.tanHalf;
    const Vec3 dir = normalize({pk.fwd[0] + pk.right[0] * sx + pk.up[0] * sy,
                                pk.fwd[1] + pk.right[1] * sx + pk.up[1] * sy,
                                pk.fwd[2] + pk.right[2] * sx + pk.up[2] * sy});

    float bestT = 1e30f;
    bool found = false;
    // Moller-Trumbore over a pos3+uv2 triangle list
    auto sweep = [&](const std::vector<float>& tris, bool canPaint) {
        for (size_t i = 0; i + 14 < tris.size(); i += 15) {
            const Vec3 v0{tris[i], tris[i + 1], tris[i + 2]};
            const Vec3 v1{tris[i + 5], tris[i + 6], tris[i + 7]};
            const Vec3 v2{tris[i + 10], tris[i + 11], tris[i + 12]};
            const Vec3 e1 = sub(v1, v0), e2 = sub(v2, v0);
            const Vec3 pv = cross(dir, e2);
            const float det = dot(e1, pv);
            if (std::fabs(det) < 1e-9f) continue;
            const float inv = 1.0f / det;
            const Vec3 tv = sub(eye, v0);
            const float bu = dot(tv, pv) * inv;
            if (bu < 0.0f || bu > 1.0f) continue;
            const Vec3 qv = cross(tv, e1);
            const float bv = dot(dir, qv) * inv;
            if (bv < 0.0f || bu + bv > 1.0f) continue;
            const float t = dot(e2, qv) * inv;
            if (t <= 1e-4f || t >= bestT) continue;
            bestT = t;
            found = true;
            const float w0 = 1.0f - bu - bv;
            outU = w0 * tris[i + 3] + bu * tris[i + 8] + bv * tris[i + 13];
            outV = w0 * tris[i + 4] + bu * tris[i + 9] + bv * tris[i + 14];
            paintable = canPaint;
        }
    };
    if (pk.shape == 4) {
        for (const MatPrevPart& part : matPrevModel_.parts)
            sweep(part.tris, part.material == pk.entryName);
    } else if (pk.shape >= 0 && pk.shape < 4) {
        sweep(prevShapeTris_[pk.shape], true);
    }
    return found;
}

// Live preview of every enabled particle emitter. The per-kind spawn /
// velocity / size / color-ramp formulas are copied from the generated game's
// updateParticles() (templates.cpp, TPL_GAME_CPP_SCENE) with the PS2's 0-128
// color scale mapped to 0-1 - when you change one side, change the other.
void Viewport::drawEmitterPreviews(const std::vector<SceneObject>& objects,
                                   const float* viewProj, const float* eyeP,
                                   const float* fwdP) {
    (void)eyeP;
    if (!particleProgram_) return;

    // Sim step from the shared wall clock, clamped so a stalled frame (modal
    // dialogs, window drags) doesn't teleport the particles.
    float dt = (float)(animClock_ - particleClock_);
    particleClock_ = animClock_;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;

    // Drop pools of removed / retyped / disabled emitters (indices shift on
    // delete; a mismatched pool also resets below via the kind/count check).
    std::erase_if(emitterPreviews_, [&](const auto& kv) {
        return kv.first >= (int)objects.size() ||
               objects[(size_t)kv.first].type != PrimitiveType::Emitter ||
               !objects[(size_t)kv.first].emitterEnabled ||
               hiddenAt((size_t)kv.first);
    });

    bool any = false;
    for (size_t oi = 0; oi < objects.size(); ++oi)
        any |= (objects[oi].type == PrimitiveType::Emitter &&
                objects[oi].emitterEnabled && !hiddenAt(oi));
    if (!any) return;

    // Camera right/up shared by every billboard (same construction as the game)
    const Vec3 fwd{fwdP[0], fwdP[1], fwdP[2]};
    float rx = fwd.z, rz = -fwd.x;
    const float rl = std::sqrt(rx * rx + rz * rz);
    if (rl > 0.0001f) rx /= rl, rz /= rl;
    else rx = 1.0f, rz = 0.0f;
    const float ux = -rz * fwd.y;
    const float uy = rz * fwd.x - rx * fwd.z;
    const float uz = rx * fwd.y;

    auto prand = [](unsigned& s) {  // 0..1, same LCG as the game
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) * (1.0f / 16777216.0f);
    };

    glUseProgram(particleProgram_);
    glUniformMatrix4fv(uPartMvp_, 1, GL_FALSE, viewProj);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);  // blend over the scene, never punch the z-buffer
    glBindVertexArray(particleVao_);
    glBindBuffer(GL_ARRAY_BUFFER, particleVbo_);

    std::vector<float> buf;
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const SceneObject& o = objects[oi];
        if (o.type != PrimitiveType::Emitter || !o.emitterEnabled || hiddenAt(oi))
            continue;
        const int count =
            o.emitterCount < 1 ? 1 : o.emitterCount > 256 ? 256 : o.emitterCount;
        EmitterPreview& ep = emitterPreviews_[(int)oi];
        if (ep.kind != o.emitterKind || ep.count != count) {
            ep.kind = o.emitterKind;
            ep.count = count;
            ep.rng = 12345u + (unsigned)oi * 7919u;
            ep.parts.assign((size_t)count, PreviewParticle{});
        }
        const int kind = o.emitterKind;
        // Follow-player emitters have no player in the editor: they preview
        // around their own position (= the offset applied to a player at the
        // world origin).
        const float bx = o.position[0], by = o.position[1], bz = o.position[2];

        // Custom kind: emission direction = the object's +Y axis rotated by
        // the object rotation + a tangent basis for the spread cone (same
        // construction as the game)
        Vec3 edir{0, 1, 0}, et1{1, 0, 0}, et2{0, 0, 1};
        if (kind == 5) {
            edir = rotateEuler({0, 1, 0}, o.rotation);
            const Vec3 seed =
                std::fabs(edir.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            et1 = normalize(cross(seed, edir));
            et2 = cross(edir, et1);
        }

        buf.clear();
        buf.reserve(ep.parts.size() * 6 * 9);
        for (PreviewParticle& p : ep.parts) {
            p.life -= dt;
            if (p.life <= 0.0f) {
                const float r1 = prand(ep.rng), r2 = prand(ep.rng),
                            r3 = prand(ep.rng);
                const float sx = bx + (r1 - 0.5f) * o.scale[0];
                const float sz = bz + (r3 - 0.5f) * o.scale[2];
                p.pos[0] = sx, p.pos[1] = by, p.pos[2] = sz;
                if (kind == 0) {  // fire: rises and flickers
                    p.vel[0] = (r1 - 0.5f) * 0.8f;
                    p.vel[1] = 1.2f + r2 * 1.2f;
                    p.vel[2] = (r3 - 0.5f) * 0.8f;
                    p.maxLife = 0.5f + r2 * 0.6f;
                } else if (kind == 1) {  // smoke: slow rise with drift
                    p.vel[0] = (r1 - 0.5f) * 0.5f;
                    p.vel[1] = 0.5f + r2 * 0.5f;
                    p.vel[2] = (r3 - 0.5f) * 0.5f;
                    p.maxLife = 2.0f + r2 * 1.5f;
                } else if (kind == 2) {  // fog: big lazy puffs hugging the ground
                    p.vel[0] = (r1 - 0.5f) * 0.25f;
                    p.vel[1] = 0.02f;
                    p.vel[2] = (r3 - 0.5f) * 0.25f;
                    p.maxLife = 3.0f + r2 * 3.0f;
                } else if (kind == 4) {  // rain: fast streaks, die on the terrain
                    const float fall = 14.0f + r2 * 6.0f;
                    p.vel[0] = (r1 - 0.5f) * 0.6f;
                    p.vel[1] = -fall;
                    p.vel[2] = (r3 - 0.5f) * 0.6f;
                    float drop = by - terrainHeight(sx, sz);
                    if (drop < 0.5f) drop = 0.5f;
                    p.maxLife = drop / fall;
                } else if (kind == 5) {  // custom: cone jet from the knobs
                    const float th =
                        o.emitterSpread * (kPi / 180.0f) * prand(ep.rng);
                    const float ph = 2.0f * kPi * prand(ep.rng);
                    const float ct = std::cos(th), st = std::sin(th);
                    const float cp = std::cos(ph), sp = std::sin(ph);
                    const float spd = o.emitterSpeed * (0.8f + 0.4f * r2);
                    p.vel[0] = (edir.x * ct + (et1.x * cp + et2.x * sp) * st) * spd;
                    p.vel[1] = (edir.y * ct + (et1.y * cp + et2.y * sp) * st) * spd;
                    p.vel[2] = (edir.z * ct + (et1.z * cp + et2.z * sp) * st) * spd;
                    p.maxLife = o.emitterLife * (0.75f + 0.5f * r1);
                } else {  // sparks: radial burst pulled down by gravity
                    p.vel[0] = (r1 - 0.5f) * 5.0f;
                    p.vel[1] = 1.5f + r2 * 2.5f;
                    p.vel[2] = (r3 - 0.5f) * 5.0f;
                    p.maxLife = 0.35f + r2 * 0.5f;
                }
                p.life = p.maxLife * (0.05f + 0.95f * prand(ep.rng));  // stagger
            }
            if (kind == 3) p.vel[1] -= 6.0f * dt;
            if (kind == 5) {
                // gravity + air drag ~ 1/weight (same terminal-velocity
                // behavior as the game)
                p.vel[1] -= o.emitterGravity * dt;
                const float w =
                    o.emitterWeight < 0.05f ? 0.05f : o.emitterWeight;
                float damp = 1.0f - (0.6f / w) * dt;
                if (damp < 0.0f) damp = 0.0f;
                p.vel[0] *= damp;
                p.vel[1] *= damp;
                p.vel[2] *= damp;
            }
            p.pos[0] += p.vel[0] * dt;
            p.pos[1] += p.vel[1] * dt;
            p.pos[2] += p.vel[2] * dt;
            // custom + Die on terrain: vanish this frame, respawn next
            if (kind == 5 && o.emitterDieOnGround &&
                p.pos[1] <= terrainHeight(p.pos[0], p.pos[2]))
                p.life = 0.0f;

            // life fraction drives size, alpha and the color ramp (the game's
            // 0-128 alpha maps to 0-1 here)
            const float t = p.life / p.maxLife;
            float size = o.emitterSize;
            float sizeUp = 0.0f;  // rain: extra world-up half-height (streaks)
            float alpha;
            float cr = o.color[0], cg = o.color[1], cb = o.color[2];
            if (kind == 0) {
                size *= 0.5f + 0.8f * t;
                alpha = 90.0f * t / 128.0f;
                cg *= 0.35f + 0.65f * t;  // orange cools to red as it dies
                cb *= 0.25f * t;
            } else if (kind == 1) {
                size *= 1.6f - t;  // smoke grows while fading
                alpha = 40.0f * t / 128.0f;
            } else if (kind == 2) {
                size *= 3.0f;
                // density knob: Opacity 0..1 -> peak alpha 0..60 (game twin)
                alpha = o.emitterOpacity * 60.0f *
                        (t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f) / 128.0f;
            } else if (kind == 4) {
                sizeUp = size * 0.5f;  // size = streak length
                size *= 0.06f;         // thin
                alpha = 70.0f * (t < 0.15f ? t * (1.0f / 0.15f) : 1.0f) / 128.0f;
            } else if (kind == 5) {
                size *= 1.0f + (o.emitterGrow - 1.0f) * (1.0f - t);
                alpha = o.emitterOpacity * (t < 0.25f ? t * 4.0f : 1.0f);
            } else {
                size *= 0.35f;
                alpha = 110.0f * t / 128.0f;
            }

            // rain streaks stay vertical (world-up quads); everything else is
            // a full camera-facing billboard. Fog puffs swirl in the camera
            // plane, alternating direction per puff - same formula as the
            // game's updateParticles() (keep in sync).
            float brx = rx, bry = 0.0f, brz = rz;
            float bux = ux, buy = uy, buz = uz;
            if (kind == 2) {
                const int pi = (int)(&p - ep.parts.data());
                const float age = p.maxLife - p.life;
                const float ang =
                    (float)pi * 2.4f + (pi & 1 ? 0.3f : -0.3f) * age;
                const float ca = std::cos(ang), sa = std::sin(ang);
                brx = rx * ca + ux * sa;
                bry = uy * sa;
                brz = rz * ca + uz * sa;
                bux = ux * ca - rx * sa;
                buy = uy * ca;
                buz = uz * ca - rz * sa;
            }
            const float Rx = brx * size, Ry = bry * size, Rz = brz * size;
            const float Ux = sizeUp > 0.0f ? 0.0f : bux * size;
            const float Uy = sizeUp > 0.0f ? sizeUp : buy * size;
            const float Uz = sizeUp > 0.0f ? 0.0f : buz * size;
            const float X = p.pos[0], Y = p.pos[1], Z = p.pos[2];
            const float v0[3] = {X - Rx - Ux, Y - Ry - Uy, Z - Rz - Uz};
            const float v1[3] = {X + Rx - Ux, Y + Ry - Uy, Z + Rz - Uz};
            const float v2[3] = {X + Rx + Ux, Y + Ry + Uy, Z + Rz + Uz};
            const float v3[3] = {X - Rx + Ux, Y - Ry + Uy, Z - Rz + Uz};
            auto vert = [&](const float* v, float tu, float tv) {
                buf.insert(buf.end(),
                           {v[0], v[1], v[2], cr, cg, cb, alpha, tu, tv});
            };
            vert(v0, 0, 1);
            vert(v1, 1, 1);
            vert(v2, 1, 0);
            vert(v0, 0, 1);
            vert(v2, 1, 0);
            vert(v3, 0, 0);
        }

        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(buf.size() * sizeof(float)),
                     buf.data(), GL_DYNAMIC_DRAW);
        // texture: the material's map_Kd, tinted by the particle color (the
        // material Kd is ignored - same rule as the game)
        const MaterialDraw* mat = materialDraw(o.materialPath);
        const uint32_t tex = mat ? mat->tex : 0;
        glUniform1i(uPartUseTex_, tex ? 1 : 0);
        if (tex) glBindTexture(GL_TEXTURE_2D, tex);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(buf.size() / 9));
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(program_);
}
