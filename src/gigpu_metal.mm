// The macOS backend of gigpu.hpp (docs/global-illumination.md, "The GPU
// backend"): the GI hemisphere gather as a Metal compute kernel.
//
// WHY A SECOND BACKEND: gigpu.cpp's kernel is a GL 4.3 compute shader, and
// macOS stops at OpenGL 4.1 - no compute shaders, no SSBOs - so on a Mac the
// GPU bake was simply unavailable. Metal is the API the hardware actually
// speaks there, so this file is the same interface (available(), Gather) over
// a Metal device, and gigpu.cpp compiles its GL half only off Apple. compare()
// (the oracle) and everything above the interface stay shared.
//
// THE KERNEL IS A TWIN OF A TWIN: kKernel below is a line-by-line port of
// gigpu.cpp's GLSL, which is itself the twin of gibake::gather / directAt /
// skyRadiance / bvh::trace. Change one, change all three - and run
// `--gi-gpu-check`, which diffs this against the CPU on any project.
//
// Two Metal-specific choices keep it inside the CPU's tolerance:
//  - fast math is OFF (MTLMathModeSafe / fastMathEnabled = NO): Metal compiles
//    with it ON by default, which flushes denormals and reassociates, and the
//    gather's 1e-12 determinant test and the shadow-ray epsilons are exactly
//    the arithmetic that must not move.
//  - buffers are shared (unified memory on Apple Silicon): no staging copy.
//
// Unlike the GL backend there is no hidden window and no "first call on the
// main thread" rule - a Metal device and queue can be created and used from
// any thread, and a headless `--bake-gi --gpu` works without a display.
//
// Compiled with ARC (CMakeLists.txt sets -fobjc-arc for this file only).
#include "gigpu.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace gigpu {

namespace {

const char* kKernel = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Params {
    int   count;
    int   rays;
    int   lights;
    int   skyDome;
    float4 skyHorizon;   // xyz
    float4 skyTop;       // xyz
    float4 sunDir;       // xyz
    float4 sunColor;     // xyz
    float skyExp;
    float ambientFloor;
};

constant float kPi = 3.14159265358979;

static uint hashU32(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16; return x;
}

// bvh::rayTri, including the back-face flag the gather reads.
static bool rayTri(float3 o, float3 d, float3 a, float3 b, float3 c, float tMax,
                   thread float& tOut, thread bool& back) {
    float3 e1 = b - a, e2 = c - a;
    float3 p = cross(d, e2);
    float det = dot(e1, p);
    if (fabs(det) < 1e-12) return false;
    float inv = 1.0 / det;
    float3 tvv = o - a;
    float u = dot(tvv, p) * inv;
    if (u < 0.0 || u > 1.0) return false;
    float3 q = cross(tvv, e1);
    float v = dot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0) return false;
    float t = dot(e2, q) * inv;
    if (t <= 0.0 || t >= tMax) return false;
    tOut = t; back = det < 0.0; return true;
}

struct Scene {
    device const float4* nd;    // 2 per node: (bmin, left), (bmax, count)
    device const int*    ord;
    device const float*  tv;    // 9 per tri
    device const float4* triL;  // emission + radiosity
    device const float4* lit;   // 2 per light: (pos, radius), (color, bright)
};

// bvh::trace with acceptBack = true (every caller in gather uses that).
static bool trace(thread const Scene& S, float3 o, float3 d, float tMax,
                  thread int& hitTri, thread bool& hitBack) {
    float3 sd = float3(fabs(d.x) < 1e-12 ? (d.x < 0.0 ? -1e-12 : 1e-12) : d.x,
                       fabs(d.y) < 1e-12 ? (d.y < 0.0 ? -1e-12 : 1e-12) : d.y,
                       fabs(d.z) < 1e-12 ? (d.z < 0.0 ? -1e-12 : 1e-12) : d.z);
    float3 inv = 1.0 / sd;
    hitTri = -1; hitBack = false;
    float best = tMax;
    int stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        int ni = stack[--sp];
        float4 A = S.nd[ni * 2], B = S.nd[ni * 2 + 1];
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
                int tri = S.ord[i];
                int b9 = tri * 9;
                float3 v0 = float3(S.tv[b9],     S.tv[b9 + 1], S.tv[b9 + 2]);
                float3 v1 = float3(S.tv[b9 + 3], S.tv[b9 + 4], S.tv[b9 + 5]);
                float3 v2 = float3(S.tv[b9 + 6], S.tv[b9 + 7], S.tv[b9 + 8]);
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

static float3 skyRadiance(constant Params& P, float3 d) {
    if (P.skyDome == 0) return P.skyHorizon.xyz;
    float t = clamp(d.y, 0.0, 1.0);
    t = asin(t) / (kPi * 0.5);
    float b = pow(t, P.skyExp);
    return P.skyHorizon.xyz + (P.skyTop.xyz - P.skyHorizon.xyz) * b;
}

static float3 directAt(thread const Scene& S, constant Params& P, float3 o, float3 n) {
    float3 res = float3(0.0);
    float3 sunDir = P.sunDir.xyz, sunColor = P.sunColor.xyz;
    float ndl = dot(n, sunDir);
    if (ndl > 0.0 && (sunColor.r > 0.0 || sunColor.g > 0.0 || sunColor.b > 0.0)) {
        int tri; bool bk;
        if (!trace(S, o, sunDir, 1e6, tri, bk)) res += sunColor * ndl;
    }
    for (int i = 0; i < P.lights; ++i) {
        float4 L = S.lit[i * 2], C = S.lit[i * 2 + 1];
        float3 d = L.xyz - o;
        float dist = length(d);
        if (dist >= L.w || dist < 1e-5) continue;
        d /= dist;
        float nl = dot(n, d);
        if (nl <= 0.0) continue;
        float att = 1.0 - dist / L.w;
        att *= att;
        int tri; bool bk;
        if (trace(S, o, d, dist - 1e-3, tri, bk)) continue;
        res += (C.w * att * nl) * C.rgb;
    }
    return res;
}

// gibake::basisAround
static void basisAround(float3 n, thread float3& t, thread float3& b) {
    if (fabs(n.y) < 0.9) t = float3(n.z, 0.0, -n.x);
    else t = float3(1.0 - n.x * n.x, -n.x * n.y, -n.x * n.z);
    float l = length(t);
    t *= (l > 1e-6) ? (1.0 / l) : 0.0;
    b = cross(n, t);
}

kernel void gather(device const float4* nd   [[buffer(0)]],
                   device const int*    ord  [[buffer(1)]],
                   device const float*  tv   [[buffer(2)]],
                   device const float4* triL [[buffer(3)]],
                   device const float4* lit  [[buffer(4)]],
                   device const float4* pin  [[buffer(5)]],  // 2 per point
                   device float4*       outv [[buffer(6)]],
                   constant Params&     P    [[buffer(7)]],
                   uint gid [[thread_position_in_grid]]) {
    if (gid >= uint(P.count)) return;
    Scene S = {nd, ord, tv, triL, lit};
    float4 a = pin[gid * 2u], bb = pin[gid * 2u + 1u];
    float3 wp = a.xyz;
    float3 n = bb.xyz;
    uint seed = as_type<uint>(a.w);

    int rays = max(P.rays, 1);
    float eps = 1e-3 * max(1.0, fabs(wp.x) + fabs(wp.y) + fabs(wp.z));
    float3 o = wp + n * eps;

    float3 res = directAt(S, P, o, n);

    float3 t, b;
    basisAround(n, t, b);
    float rot = float(hashU32(seed) & 0xffffu) * (2.0 * kPi / 65536.0);
    float3 acc = float3(0.0);
    for (int i = 0; i < rays; ++i) {
        // gibake::hemiDir
        float u = (float(i) + 0.5) / float(rays);
        float r = sqrt(u);
        float zc = sqrt(1.0 - u);
        float phi = kPi * (3.0 - sqrt(5.0)) * float(i) + rot;
        float3 h = float3(r * cos(phi), r * sin(phi), zc);
        float3 d = t * h.x + b * h.y + n * h.z;
        int tri; bool bk;
        if (trace(S, o, d, 1e6, tri, bk)) {
            if (bk) continue;          // the inside of a solid emits nothing
            acc += triL[tri].rgb;      // emission + solved radiosity
        } else {
            acc += skyRadiance(P, d);
        }
    }
    res += acc * (1.0 / float(rays)) + float3(P.ambientFloor);
    outv[gid] = float4(res, 0.0);
}
)MSL";

// The host side of the kernel's Params - same field order, and float4 for the
// float3s so the layouts agree without relying on packed_float3 rules.
struct Params {
    int count, rays, lights, skyDome;
    float skyHorizon[4], skyTop[4], sunDir[4], sunColor[4];
    float skyExp, ambientFloor;
    float pad[2];
};

// One device, one queue and one pipeline per process: the kernel is a constant,
// so it is compiled once and shared by every Gather (a multi-scene bake is the
// case the GPU backend exists for).
struct Ctx {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> pipe = nil;
    std::string why;
    bool good = false;
};

Ctx& ctx() {
    static Ctx c;
    static std::once_flag once;
    std::call_once(once, [] {
        @autoreleasepool {
            c.device = MTLCreateSystemDefaultDevice();
            if (!c.device) {
                c.why = "no Metal device";
                return;
            }
            MTLCompileOptions* opt = [MTLCompileOptions new];
            // Fast math off - see the file comment. mathMode replaced
            // fastMathEnabled in the macOS 15 SDK; both mean the same here.
            if (@available(macOS 15.0, *)) {
                opt.mathMode = MTLMathModeSafe;
            } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                opt.fastMathEnabled = NO;
#pragma clang diagnostic pop
            }
            NSError* err = nil;
            id<MTLLibrary> lib = [c.device newLibraryWithSource:[NSString stringWithUTF8String:kKernel]
                                                        options:opt
                                                          error:&err];
            if (!lib) {
                c.why = std::string("Metal kernel: ") +
                        (err ? err.localizedDescription.UTF8String : "did not compile");
                return;
            }
            id<MTLFunction> fn = [lib newFunctionWithName:@"gather"];
            c.pipe = fn ? [c.device newComputePipelineStateWithFunction:fn error:&err] : nil;
            if (!c.pipe) {
                c.why = std::string("Metal pipeline: ") +
                        (err ? err.localizedDescription.UTF8String : "no gather function");
                return;
            }
            c.queue = [c.device newCommandQueue];
            if (!c.queue) {
                c.why = "Metal: no command queue";
                return;
            }
            c.good = true;
        }
    });
    return c;
}

}  // namespace

bool available(std::string* why) {
    Ctx& c = ctx();
    if (!c.good && why) *why = c.why;
    return c.good;
}

// --- Gather ------------------------------------------------------------------

struct Gather::Impl {
    id<MTLBuffer> buf[5] = {nil, nil, nil, nil, nil};  // nodes, order, tris, triL, lights
    Params params{};
    bool ready = false;
};

Gather::Gather() : impl_(new Impl) {}

Gather::~Gather() = default;  // ARC releases the buffers

bool Gather::upload(const gibake::Scene& s, std::string* err) {
    const auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    Ctx& c = ctx();
    if (!c.good) return fail(c.why);
    if (s.empty()) return fail("the scene has no triangles");

    @autoreleasepool {
        // The same packing as the GL backend: nodes as two float4, the
        // emission + radiosity sum per triangle, two float4 per light.
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
            lights.insert(lights.end(), {L.pos[0], L.pos[1], L.pos[2], L.radius, L.color[0],
                                         L.color[1], L.color[2], L.bright});
        }
        if (lights.empty()) lights.assign(8, 0.0f);  // a zero-length buffer is not bindable

        auto make = [&](const void* data, size_t bytes) -> id<MTLBuffer> {
            return [c.device newBufferWithBytes:data
                                         length:std::max<size_t>(bytes, 16)
                                        options:MTLResourceStorageModeShared];
        };
        impl_->buf[0] = make(nodes.data(), nodes.size() * 4);
        impl_->buf[1] = make(s.tree.order.data(), s.tree.order.size() * 4);
        impl_->buf[2] = make(s.tree.tv.data(), s.tree.tv.size() * 4);
        impl_->buf[3] = make(triL.data(), triL.size() * 4);
        impl_->buf[4] = make(lights.data(), lights.size() * 4);
        for (id<MTLBuffer> b : impl_->buf)
            if (!b) return fail("Metal: could not allocate the scene buffers");

        Params& p = impl_->params;
        p = Params{};
        p.lights = (int)s.lights.size();
        p.skyDome = s.skyDome ? 1 : 0;
        for (int k = 0; k < 3; ++k) {
            p.skyHorizon[k] = s.skyHorizon[k];
            p.skyTop[k] = s.skyTop[k];
            p.sunDir[k] = s.sunDir[k];
            p.sunColor[k] = s.sunColor[k];
        }
        p.skyExp = s.skyExp;
        p.ambientFloor = s.ambientFloor;
    }
    impl_->ready = true;
    return true;
}

bool Gather::run(const float* wp, const float* n, const uint32_t* seed, int count, int rays,
                 float* outRgb, std::string* err) {
    if (!impl_->ready) {
        if (err) *err = "upload() was not called or failed";
        return false;
    }
    if (count <= 0) return true;
    Ctx& c = ctx();

    // Chunked like the GL backend: no single enormous buffer, and no single
    // command buffer long enough for the GPU watchdog to kill.
    const int kChunk = 1 << 16;
    const int cap = std::min(count, kChunk);
    @autoreleasepool {
        id<MTLBuffer> in = [c.device newBufferWithLength:(size_t)cap * 8 * 4
                                                 options:MTLResourceStorageModeShared];
        id<MTLBuffer> out = [c.device newBufferWithLength:(size_t)cap * 4 * 4
                                                  options:MTLResourceStorageModeShared];
        if (!in || !out) {
            if (err) *err = "Metal: could not allocate the point buffers";
            return false;
        }
        Params p = impl_->params;
        p.rays = rays < 1 ? 1 : rays;
        const NSUInteger tg = std::min<NSUInteger>(64, c.pipe.maxTotalThreadsPerThreadgroup);

        for (int base = 0; base < count; base += kChunk) {
            const int m = std::min(kChunk, count - base);
            float* d = (float*)in.contents;
            for (int i = 0; i < m; ++i, d += 8) {
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
            p.count = m;

            id<MTLCommandBuffer> cb = [c.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            [enc setComputePipelineState:c.pipe];
            for (int b = 0; b < 5; ++b) [enc setBuffer:impl_->buf[b] offset:0 atIndex:(NSUInteger)b];
            [enc setBuffer:in offset:0 atIndex:5];
            [enc setBuffer:out offset:0 atIndex:6];
            [enc setBytes:&p length:sizeof p atIndex:7];
            [enc dispatchThreadgroups:MTLSizeMake(((NSUInteger)m + tg - 1) / tg, 1, 1)
                threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status != MTLCommandBufferStatusCompleted) {
                if (err)
                    *err = std::string("Metal dispatch failed: ") +
                           (cb.error ? cb.error.localizedDescription.UTF8String : "unknown error");
                return false;
            }
            const float* o = (const float*)out.contents;
            for (int i = 0; i < m; ++i)
                for (int k = 0; k < 3; ++k) outRgb[(size_t)(base + i) * 3 + k] = o[(size_t)i * 4 + k];
        }
    }
    return true;
}

}  // namespace gigpu
