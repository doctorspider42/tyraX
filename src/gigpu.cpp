#include "gigpu.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

#include <GLFW/glfw3.h>

#include "bvh.hpp"

namespace gigpu {

namespace {

// --- the GL entry points this needs -----------------------------------------
// Loaded here rather than through gl_loader.h on purpose: that loader serves
// the VIEWPORT, whose context is 3.3, and putting 4.3-only symbols in it would
// read as "the viewport may call these". It may not - they belong to this
// module's own context.
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr, GLintptr;
#define GL_COMPUTE_SHADER 0x91B9
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STATIC_DRAW 0x88E4
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#define GL_NO_ERROR 0

struct Api {
    GLuint (*CreateShader)(GLenum) = nullptr;
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*,
                         const GLint*) = nullptr;
    void (*CompileShader)(GLuint) = nullptr;
    void (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    GLuint (*CreateProgram)() = nullptr;
    void (*AttachShader)(GLuint, GLuint) = nullptr;
    void (*LinkProgram)(GLuint) = nullptr;
    void (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*UseProgram)(GLuint) = nullptr;
    void (*DeleteProgram)(GLuint) = nullptr;
    void (*DeleteShader)(GLuint) = nullptr;
    GLint (*GetUniformLocation)(GLuint, const GLchar*) = nullptr;
    void (*Uniform1i)(GLint, GLint) = nullptr;
    void (*Uniform1f)(GLint, float) = nullptr;
    void (*Uniform3f)(GLint, float, float, float) = nullptr;
    void (*GenBuffers)(GLsizei, GLuint*) = nullptr;
    void (*DeleteBuffers)(GLsizei, const GLuint*) = nullptr;
    void (*BindBuffer)(GLenum, GLuint) = nullptr;
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
    void (*BindBufferBase)(GLenum, GLuint, GLuint) = nullptr;
    void (*GetBufferSubData)(GLenum, GLintptr, GLsizeiptr, void*) = nullptr;
    void (*DispatchCompute)(GLuint, GLuint, GLuint) = nullptr;
    void (*MemoryBarrier)(GLbitfield) = nullptr;
    GLenum (*GetError)() = nullptr;
    bool ok = false;
};

bool loadApi(Api& a) {
    auto get = [](const char* n) { return (void*)glfwGetProcAddress(n); };
#define G(field, name)                              \
    *(void**)&a.field = get(name);                  \
    if (!a.field) return false
    G(CreateShader, "glCreateShader");
    G(ShaderSource, "glShaderSource");
    G(CompileShader, "glCompileShader");
    G(GetShaderiv, "glGetShaderiv");
    G(GetShaderInfoLog, "glGetShaderInfoLog");
    G(CreateProgram, "glCreateProgram");
    G(AttachShader, "glAttachShader");
    G(LinkProgram, "glLinkProgram");
    G(GetProgramiv, "glGetProgramiv");
    G(GetProgramInfoLog, "glGetProgramInfoLog");
    G(UseProgram, "glUseProgram");
    G(DeleteProgram, "glDeleteProgram");
    G(DeleteShader, "glDeleteShader");
    G(GetUniformLocation, "glGetUniformLocation");
    G(Uniform1i, "glUniform1i");
    G(Uniform1f, "glUniform1f");
    G(Uniform3f, "glUniform3f");
    G(GenBuffers, "glGenBuffers");
    G(DeleteBuffers, "glDeleteBuffers");
    G(BindBuffer, "glBindBuffer");
    G(BufferData, "glBufferData");
    G(BufferSubData, "glBufferSubData");
    G(BindBufferBase, "glBindBufferBase");
    G(GetBufferSubData, "glGetBufferSubData");
    G(DispatchCompute, "glDispatchCompute");
    G(MemoryBarrier, "glMemoryBarrier");
    G(GetError, "glGetError");
#undef G
    a.ok = true;
    return true;
}

// --- the context -------------------------------------------------------------
// A hidden GLFW window rather than EGL, because GLFW is already a dependency on
// both platforms and this needs no second per-OS pair (the "Platform parity"
// rule: a file that exists twice is a maintenance cost). The price is that a
// machine with no display server has no GPU bake - which is exactly the Docker
// and build-server case the CPU reference exists for, so it costs nothing that
// was not already required.
struct Ctx {
    GLFWwindow* win = nullptr;
    GLFWwindow* prev = nullptr;  // whatever was current before we took over
    Api api;
    std::string why;
    bool tried = false;
    bool good = false;
};

Ctx& ctx() {
    static Ctx c;
    return c;
}

bool ensureCtx() {
    Ctx& c = ctx();
    if (c.tried) return c.good;
    c.tried = true;
    // The editor may already own GLFW; glfwInit is safe to call again and we
    // deliberately never call glfwTerminate, which would take the editor's own
    // window down with it.
    if (!glfwInit()) {
        c.why = "GLFW would not initialise (no display server?)";
        return false;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    c.prev = glfwGetCurrentContext();
    c.win = glfwCreateWindow(1, 1, "tyrax-gi-gpu", nullptr, nullptr);
    glfwDefaultWindowHints();
    if (!c.win) {
        c.why = "no OpenGL 4.3 core context (compute shaders need 4.3)";
        return false;
    }
    glfwMakeContextCurrent(c.win);
    if (!loadApi(c.api)) {
        c.why = "the 4.3 entry points are missing from this driver";
        glfwMakeContextCurrent(c.prev);
        glfwDestroyWindow(c.win);
        c.win = nullptr;
        return false;
    }
    glfwMakeContextCurrent(c.prev);
    c.good = true;
    return true;
}

// Makes our context current for the duration and puts back whatever was there.
// The editor draws from its own context on the main thread; a bake that left
// ours current would blank the viewport.
struct ScopedCurrent {
    GLFWwindow* prev;
    explicit ScopedCurrent(GLFWwindow* w) : prev(glfwGetCurrentContext()) {
        glfwMakeContextCurrent(w);
    }
    ~ScopedCurrent() { glfwMakeContextCurrent(prev); }
};

// --- the kernel --------------------------------------------------------------
// A line-by-line twin of gibake::gather + directAt + skyRadiance + bvh::trace /
// rayTri. Read them side by side when changing either; the constants here are
// the same literals on purpose (1e-12 in the determinant test, 1e-3 in the
// shadow-ray shortening, the 1e-3 * L1-norm surface bias).
const char* kKernel = R"GLSL(
#version 430
layout(local_size_x = 64) in;

layout(std430, binding = 0) readonly buffer Nodes { vec4 nd[]; };   // 2 per node
layout(std430, binding = 1) readonly buffer Order { int ord[]; };
layout(std430, binding = 2) readonly buffer Tris  { float tv[]; };  // 9 per tri
layout(std430, binding = 3) readonly buffer TriL  { vec4 triL[]; }; // emis+rad
layout(std430, binding = 4) readonly buffer Lites { vec4 lit[]; };  // 2 per light
layout(std430, binding = 5) readonly buffer PtsIn { vec4 pin[]; };  // 2 per point
layout(std430, binding = 6) writeonly buffer Outp { vec4 outv[]; };

uniform int  uCount;
uniform int  uRays;
uniform int  uLights;
uniform int  uSkyDome;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyTop;
uniform float uSkyExp;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uAmbientFloor;

const float kPi = 3.14159265358979;

uint hashU32(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16; return x;
}

// bvh::rayTri, including the back-face flag the gather reads.
bool rayTri(vec3 o, vec3 d, vec3 a, vec3 b, vec3 c, float tMax,
            out float tOut, out bool back) {
    vec3 e1 = b - a, e2 = c - a;
    vec3 p = cross(d, e2);
    float det = dot(e1, p);
    if (abs(det) < 1e-12) return false;
    float inv = 1.0 / det;
    vec3 tvv = o - a;
    float u = dot(tvv, p) * inv;
    if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(tvv, e1);
    float v = dot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0) return false;
    float t = dot(e2, q) * inv;
    if (t <= 0.0 || t >= tMax) return false;
    tOut = t; back = det < 0.0; return true;
}

// bvh::trace with acceptBack = true (every caller in gather uses that), so a
// back-facing hit is a hit and the gather decides what it means.
bool trace(vec3 o, vec3 d, float tMax, out int hitTri, out bool hitBack) {
    vec3 sd = vec3(abs(d.x) < 1e-12 ? (d.x < 0.0 ? -1e-12 : 1e-12) : d.x,
                   abs(d.y) < 1e-12 ? (d.y < 0.0 ? -1e-12 : 1e-12) : d.y,
                   abs(d.z) < 1e-12 ? (d.z < 0.0 ? -1e-12 : 1e-12) : d.z);
    vec3 inv = 1.0 / sd;
    hitTri = -1; hitBack = false;
    float best = tMax;
    int stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        int ni = stack[--sp];
        vec4 A = nd[ni * 2], B = nd[ni * 2 + 1];
        float t0 = 0.0, t1 = best;
        bool outside = false;
        for (int k = 0; k < 3; ++k) {
            float ta = (A[k] - o[k]) * inv[k];
            float tb = (B[k] - o[k]) * inv[k];
            float lo = min(ta, tb), hi = max(ta, tb);
            t0 = max(t0, lo); t1 = min(t1, hi);
            if (t0 > t1) { outside = true; break; }
        }
        if (outside) continue;
        int left = int(A.w), cnt = int(B.w);
        if (cnt != 0) {
            for (int i = left; i < left + cnt; ++i) {
                int tri = ord[i];
                int b9 = tri * 9;
                vec3 v0 = vec3(tv[b9],     tv[b9 + 1], tv[b9 + 2]);
                vec3 v1 = vec3(tv[b9 + 3], tv[b9 + 4], tv[b9 + 5]);
                vec3 v2 = vec3(tv[b9 + 6], tv[b9 + 7], tv[b9 + 8]);
                float t; bool bk;
                if (rayTri(o, d, v0, v1, v2, best, t, bk)) {
                    best = t; hitTri = tri; hitBack = bk;
                }
            }
        } else if (sp + 2 <= 64) {
            stack[sp++] = left;
            stack[sp++] = left + 1;
        }
    }
    return hitTri >= 0;
}

vec3 skyRadiance(vec3 d) {
    if (uSkyDome == 0) return uSkyHorizon;
    float t = clamp(d.y, 0.0, 1.0);
    t = asin(t) / (kPi * 0.5);
    float b = pow(t, uSkyExp);
    return uSkyHorizon + (uSkyTop - uSkyHorizon) * b;
}

vec3 directAt(vec3 o, vec3 n) {
    vec3 res = vec3(0.0);
    float ndl = dot(n, uSunDir);
    if (ndl > 0.0 && (uSunColor.r > 0.0 || uSunColor.g > 0.0 || uSunColor.b > 0.0)) {
        int tri; bool bk;
        if (!trace(o, uSunDir, 1e6, tri, bk)) res += uSunColor * ndl;
    }
    for (int i = 0; i < uLights; ++i) {
        vec4 P = lit[i * 2], C = lit[i * 2 + 1];
        vec3 d = P.xyz - o;
        float dist = length(d);
        if (dist >= P.w || dist < 1e-5) continue;
        d /= dist;
        float nl = dot(n, d);
        if (nl <= 0.0) continue;
        float att = 1.0 - dist / P.w;
        att *= att;
        int tri; bool bk;
        if (trace(o, d, dist - 1e-3, tri, bk)) continue;
        res += (C.w * att * nl) * C.rgb;
    }
    return res;
}

// gibake::basisAround
void basisAround(vec3 n, out vec3 t, out vec3 b) {
    if (abs(n.y) < 0.9) t = vec3(n.z, 0.0, -n.x);
    else t = vec3(1.0 - n.x * n.x, -n.x * n.y, -n.x * n.z);
    float l = length(t);
    t *= (l > 1e-6) ? (1.0 / l) : 0.0;
    b = cross(n, t);
}

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(uCount)) return;
    vec4 a = pin[gid * 2u], bb = pin[gid * 2u + 1u];
    vec3 wp = a.xyz;
    vec3 n = bb.xyz;
    uint seed = floatBitsToUint(a.w);

    int rays = max(uRays, 1);
    float eps = 1e-3 * max(1.0, abs(wp.x) + abs(wp.y) + abs(wp.z));
    vec3 o = wp + n * eps;

    vec3 res = directAt(o, n);

    vec3 t, b;
    basisAround(n, t, b);
    float rot = float(hashU32(seed) & 0xffffu) * (2.0 * kPi / 65536.0);
    vec3 acc = vec3(0.0);
    for (int i = 0; i < rays; ++i) {
        // gibake::hemiDir
        float u = (float(i) + 0.5) / float(rays);
        float r = sqrt(u);
        float zc = sqrt(1.0 - u);
        float phi = kPi * (3.0 - sqrt(5.0)) * float(i) + rot;
        vec3 h = vec3(r * cos(phi), r * sin(phi), zc);
        vec3 d = t * h.x + b * h.y + n * h.z;
        int tri; bool bk;
        if (trace(o, d, 1e6, tri, bk)) {
            if (bk) continue;          // the inside of a solid emits nothing
            acc += triL[tri].rgb;      // emission + solved radiosity
        } else {
            acc += skyRadiance(d);
        }
    }
    res += acc * (1.0 / float(rays)) + vec3(uAmbientFloor);
    outv[gid] = vec4(res, 0.0);
}
)GLSL";

}  // namespace

bool available(std::string* why) {
    const bool ok = ensureCtx();
    if (!ok && why) *why = ctx().why;
    return ok;
}

// --- Gather ------------------------------------------------------------------

struct Gather::Impl {
    GLuint prog = 0, buf[7] = {0, 0, 0, 0, 0, 0, 0};
    int lights = 0;
    int capacity = 0;  // points the in/out buffers currently hold
    bool ready = false;
    // scene uniforms, kept so run() can set them without the Scene
    int skyDome = 1;
    float skyHorizon[3] = {0, 0, 0}, skyTop[3] = {0, 0, 0}, skyExp = 1.0f;
    float sunDir[3] = {0, 1, 0}, sunColor[3] = {0, 0, 0};
    float ambientFloor = 0.0f;
};

Gather::Gather() : impl_(new Impl) {}

Gather::~Gather() {
    if (!impl_ || !impl_->ready) return;
    if (!ensureCtx()) return;
    ScopedCurrent cur(ctx().win);
    const Api& gl = ctx().api;
    if (impl_->prog) gl.DeleteProgram(impl_->prog);
    gl.DeleteBuffers(7, impl_->buf);
}

bool Gather::upload(const gibake::Scene& s, std::string* err) {
    const auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (!ensureCtx()) return fail(ctx().why);
    if (s.empty()) return fail("the scene has no triangles");
    ScopedCurrent cur(ctx().win);
    const Api& gl = ctx().api;

    // compile
    GLuint sh = gl.CreateShader(GL_COMPUTE_SHADER);
    gl.ShaderSource(sh, 1, &kKernel, nullptr);
    gl.CompileShader(sh);
    GLint ok = 0;
    gl.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        gl.GetShaderInfoLog(sh, sizeof log, nullptr, log);
        gl.DeleteShader(sh);
        return fail(std::string("compute shader: ") + log);
    }
    impl_->prog = gl.CreateProgram();
    gl.AttachShader(impl_->prog, sh);
    gl.LinkProgram(impl_->prog);
    gl.GetProgramiv(impl_->prog, GL_LINK_STATUS, &ok);
    gl.DeleteShader(sh);
    if (!ok) {
        char log[4096] = {0};
        gl.GetProgramInfoLog(impl_->prog, sizeof log, nullptr, log);
        return fail(std::string("compute link: ") + log);
    }

    // Nodes as two vec4 each: (bmin, left) and (bmax, count). std430 packs a
    // vec4 array with no padding, so this is the tightest faithful layout.
    const int nn = (int)s.tree.nodes.size();
    std::vector<float> nodes((size_t)nn * 8);
    for (int i = 0; i < nn; ++i) {
        const bvh::Node& n = s.tree.nodes[i];
        float* d = &nodes[(size_t)i * 8];
        d[0] = n.bmin[0], d[1] = n.bmin[1], d[2] = n.bmin[2];
        d[3] = (float)n.left;
        d[4] = n.bmax[0], d[5] = n.bmax[1], d[6] = n.bmax[2];
        d[7] = (float)n.count;
    }
    // The kernel only ever reads emission + radiosity, so it is summed here
    // rather than uploaded as two buffers and added per hit.
    const int nt = s.tree.triCount();
    std::vector<float> triL((size_t)nt * 4, 0.0f);
    for (int i = 0; i < nt; ++i)
        for (int k = 0; k < 3; ++k) {
            const size_t j = (size_t)i * 3 + k;
            float v = 0.0f;
            if (j < s.emission.size()) v += s.emission[j];
            if (j < s.radiosity.size()) v += s.radiosity[j];
            triL[(size_t)i * 4 + k] = v;
        }
    std::vector<float> lights;
    for (const gibake::Scene::PointLight& L : s.lights) {
        lights.push_back(L.pos[0]);
        lights.push_back(L.pos[1]);
        lights.push_back(L.pos[2]);
        lights.push_back(L.radius);
        lights.push_back(L.color[0]);
        lights.push_back(L.color[1]);
        lights.push_back(L.color[2]);
        lights.push_back(L.bright);
    }
    impl_->lights = (int)s.lights.size();

    gl.GenBuffers(7, impl_->buf);
    auto put = [&](int slot, const void* data, size_t bytes) {
        gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buf[slot]);
        gl.BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)bytes, data,
                      GL_STATIC_DRAW);
    };
    put(0, nodes.data(), nodes.size() * 4);
    put(1, s.tree.order.data(), s.tree.order.size() * 4);
    put(2, s.tree.tv.data(), s.tree.tv.size() * 4);
    put(3, triL.data(), triL.size() * 4);
    // An empty SSBO is not legal to bind; one dead light costs 32 bytes.
    static const float kNoLight[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    put(4, lights.empty() ? kNoLight : lights.data(),
        lights.empty() ? sizeof kNoLight : lights.size() * 4);

    impl_->skyDome = s.skyDome ? 1 : 0;
    for (int k = 0; k < 3; ++k) {
        impl_->skyHorizon[k] = s.skyHorizon[k];
        impl_->skyTop[k] = s.skyTop[k];
        impl_->sunDir[k] = s.sunDir[k];
        impl_->sunColor[k] = s.sunColor[k];
    }
    impl_->skyExp = s.skyExp;
    impl_->ambientFloor = s.ambientFloor;
    impl_->ready = true;
    if (gl.GetError() != GL_NO_ERROR) return fail("GL error during upload");
    return true;
}

bool Gather::run(const float* wp, const float* n, const uint32_t* seed,
                 int count, int rays, float* outRgb, std::string* err) {
    if (!impl_->ready) {
        if (err) *err = "upload() was not called or failed";
        return false;
    }
    if (count <= 0) return true;
    ScopedCurrent cur(ctx().win);
    const Api& gl = ctx().api;

    // Chunked so a whole atlas does not need one enormous buffer, and so a
    // driver watchdog never sees a single multi-second dispatch.
    const int kChunk = 1 << 16;
    std::vector<float> in((size_t)std::min(count, kChunk) * 8);
    std::vector<float> out((size_t)std::min(count, kChunk) * 4);

    gl.UseProgram(impl_->prog);
    auto uni = [&](const char* nm) {
        return gl.GetUniformLocation(impl_->prog, nm);
    };
    gl.Uniform1i(uni("uRays"), rays < 1 ? 1 : rays);
    gl.Uniform1i(uni("uLights"), impl_->lights);
    gl.Uniform1i(uni("uSkyDome"), impl_->skyDome);
    gl.Uniform3f(uni("uSkyHorizon"), impl_->skyHorizon[0], impl_->skyHorizon[1],
                 impl_->skyHorizon[2]);
    gl.Uniform3f(uni("uSkyTop"), impl_->skyTop[0], impl_->skyTop[1],
                 impl_->skyTop[2]);
    gl.Uniform1f(uni("uSkyExp"), impl_->skyExp);
    gl.Uniform3f(uni("uSunDir"), impl_->sunDir[0], impl_->sunDir[1],
                 impl_->sunDir[2]);
    gl.Uniform3f(uni("uSunColor"), impl_->sunColor[0], impl_->sunColor[1],
                 impl_->sunColor[2]);
    gl.Uniform1f(uni("uAmbientFloor"), impl_->ambientFloor);

    for (int base = 0; base < count; base += kChunk) {
        const int m = std::min(kChunk, count - base);
        for (int i = 0; i < m; ++i) {
            float* d = &in[(size_t)i * 8];
            d[0] = wp[(size_t)(base + i) * 3 + 0];
            d[1] = wp[(size_t)(base + i) * 3 + 1];
            d[2] = wp[(size_t)(base + i) * 3 + 2];
            const uint32_t sd = seed[base + i];
            std::memcpy(&d[3], &sd, 4);  // the seed rides as raw bits
            d[4] = n[(size_t)(base + i) * 3 + 0];
            d[5] = n[(size_t)(base + i) * 3 + 1];
            d[6] = n[(size_t)(base + i) * 3 + 2];
            d[7] = 0.0f;
        }
        gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buf[5]);
        gl.BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)m * 8 * 4,
                      in.data(), GL_DYNAMIC_DRAW);
        gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buf[6]);
        gl.BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)m * 4 * 4, nullptr,
                      GL_DYNAMIC_DRAW);
        for (int b = 0; b < 7; ++b)
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, b, impl_->buf[b]);
        gl.Uniform1i(uni("uCount"), m);
        gl.DispatchCompute((GLuint)((m + 63) / 64), 1, 1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buf[6]);
        gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)m * 4 * 4,
                            out.data());
        for (int i = 0; i < m; ++i)
            for (int k = 0; k < 3; ++k)
                outRgb[(size_t)(base + i) * 3 + k] = out[(size_t)i * 4 + k];
    }
    if (gl.GetError() != GL_NO_ERROR) {
        if (err) *err = "GL error during dispatch";
        return false;
    }
    return true;
}

// --- the oracle --------------------------------------------------------------

Compare compare(const gibake::Scene& s, int rays, int maxPoints) {
    Compare c;
    if (s.empty()) {
        c.note = "the scene has no triangles";
        return c;
    }
    std::string why;
    if (!available(&why)) {
        c.note = why;
        return c;
    }
    // Sample points over the scene's own surfaces, deterministically. A scene
    // usually has FEWER triangles than the batch size that matters, so each one
    // carries several points at fixed barycentric offsets rather than just its
    // centroid: the atlas pass hands the gather a quarter of a million points
    // at a time, and a throughput number taken on a few thousand is measuring
    // dispatch latency instead of the kernel.
    // Offsets from a low-discrepancy pair folded into the triangle, so the
    // count per triangle is not capped by a hand-written table - occupancy is
    // part of what is being measured and 21k points leaves a GPU idle.
    const auto bary = [](int r, float& bu, float& bv) {
        const float a = (float)((r * 2654435761u) & 0xffffu) / 65536.0f;
        const float b = (float)((r * 40503u + 12345u) & 0xffffu) / 65536.0f;
        bu = 0.05f + 0.90f * a * (1.0f - b);
        bv = 0.05f + 0.90f * b * (1.0f - a);
    };
    const int nt = s.tree.triCount();
    const int stride = std::max(1, nt / std::max(1, maxPoints));
    const int perTri =
        std::max(1, std::min(64, maxPoints / std::max(1, nt / stride)));
    std::vector<float> wp, nn;
    std::vector<uint32_t> seed;
    for (int t = 0; t < nt; t += stride) {
        const float* v = &s.tree.tv[(size_t)t * 9];
        const float* vn = &s.tree.tn[(size_t)t * 9];
        float nx = vn[0] + vn[3] + vn[6];
        float ny = vn[1] + vn[4] + vn[7];
        float nz = vn[2] + vn[5] + vn[8];
        const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (l > 1e-6f) nx /= l, ny /= l, nz /= l;
        else nx = 0.0f, ny = 1.0f, nz = 0.0f;
        for (int r = 0; r < perTri; ++r) {
            float bu, bv;
            bary(r, bu, bv);
            const float bw = 1.0f - bu - bv;
            for (int k = 0; k < 3; ++k)
                wp.push_back(v[k] * bw + v[3 + k] * bu + v[6 + k] * bv);
            nn.push_back(nx), nn.push_back(ny), nn.push_back(nz);
            // Seeded by (triangle, offset) so the set is a property of the
            // scene and not of the order anything ran in.
            seed.push_back((uint32_t)t * 2654435761u + (uint32_t)r);
        }
    }
    c.points = (int)seed.size();
    if (c.points == 0) {
        c.note = "no sample points";
        return c;
    }

    std::vector<float> gpu((size_t)c.points * 3, 0.0f);
    std::vector<float> cpu((size_t)c.points * 3, 0.0f);

    Gather g;
    std::string err;
    if (!g.upload(s, &err)) {
        c.note = err;
        return c;
    }
    auto now = [] { return std::chrono::steady_clock::now(); };
    // Warm-up, untimed: the FIRST dispatch carries the driver's own shader
    // finalisation, and folding that into the measurement made an identical
    // scene read 1.2x once and 7.5x the next run.
    {
        const int warm = std::min(c.points, 4096);
        std::vector<float> scratch((size_t)warm * 3, 0.0f);
        g.run(wp.data(), nn.data(), seed.data(), warm, rays, scratch.data(),
              nullptr);
    }
    auto t0 = now();
    if (!g.run(wp.data(), nn.data(), seed.data(), c.points, rays, gpu.data(),
               &err)) {
        c.note = err;
        return c;
    }
    c.gpuSeconds = std::chrono::duration<double>(now() - t0).count();

    t0 = now();
    for (int i = 0; i < c.points; ++i)
        gibake::gather(s, &wp[(size_t)i * 3], &nn[(size_t)i * 3], seed[i], rays,
                       &cpu[(size_t)i * 3]);
    c.cpuSeconds = std::chrono::duration<double>(now() - t0).count();

    double sum = 0.0, ref = 0.0, worst = 0.0;
    const size_t n = (size_t)c.points * 3;
    for (size_t i = 0; i < n; ++i) {
        const double d = std::fabs((double)gpu[i] - (double)cpu[i]);
        sum += d;
        ref += std::fabs((double)cpu[i]);
        if (d > worst) worst = d;
    }
    c.meanAbs = sum / (double)n;
    c.maxAbs = worst;
    c.meanRef = ref / (double)n;
    c.ran = true;
    return c;
}

}  // namespace gigpu
