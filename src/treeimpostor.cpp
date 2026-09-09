#include "impostorbake.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stb_image_write.h>

namespace impostorbake {
static GpuCapture gpuCapture = nullptr;
void setGpuCapture(GpuCapture capture) { gpuCapture = capture; }
bool write(const std::string& projectDir, const std::string& outputStem,
           const std::vector<Part>& parts, const float* min, const float* max,
           std::string* outObjRel, std::string* outError, int size, int views, bool gpu, std::string* report) {
    auto fail = [&](const std::string& message) {
        if (outError) *outError = message;
        return false;
    };
    if ((size != 64 && size != 128 && size != 256) || parts.empty())
        return fail("Impostors require geometry and a 64, 128 or 256 pixel capture");
    if (views != 4 && views != 8 && views != 16) return fail("Choose 4, 8 or 16 capture views");
    auto valid = [](const treegen::Image& im) {
        return im.w > 0 && im.h > 0 &&
               im.rgba.size() == (size_t)im.w * im.h * 4;
    };
    for (const auto& part : parts) {
        if (!valid(part.texture) || part.vertices.empty() || part.vertices.size()%24 != 0)
            return fail("Missing geometry or texture");
        for (float v : part.vertices)
            if (!std::isfinite(v)) return fail("Model contains non-finite coordinates");
        for (float v : part.kd)
            if (!std::isfinite(v)) return fail("Material contains non-finite colour");
    }
    const int width = size * 4, height = size * (views / 4);
    std::vector<unsigned char> pixels((size_t)width * height * 4, 0);
    const float cy = (min[1] + max[1]) * .5f;
    const float hh = std::max(max[1] - min[1], .01f) * .53f;
    std::vector<float> centers(views), halves(views);
    for (int view = 0; view < views; ++view) {
        const float angle = view * 6.283185307f / views;
        const float cr = std::cos(angle), sr = std::sin(angle);
        float lo = std::numeric_limits<float>::infinity(), hi = -lo;
        for (const auto& part : parts)
            for (size_t i = 0; i + 7 < part.vertices.size(); i += 8) {
                const float x = part.vertices[i]*cr - part.vertices[i+2]*sr;
                lo = std::min(lo, x); hi = std::max(hi, x);
            }
        centers[view] = (lo+hi)*.5f;
        halves[view] = std::max(hi-lo, .01f)*.53f;
    }
    std::string gpuNote;
    const bool captured = gpu && gpuCapture && gpuCapture(parts, size, views, cy, hh,
        centers.data(), halves.data(), pixels, gpuNote);
    if (report) *report = captured ? "GPU" : (gpu ? "CPU fallback: " +
        (gpuCapture ? gpuNote : "GPU backend not linked") : "CPU");
    if (!captured) {
      std::fill(pixels.begin(), pixels.end(), 0);
      for (int view = 0; view < views; ++view) {
        const float angle = view * 6.283185307f / views;
        const float cr = std::cos(angle), sr = std::sin(angle);
        const float half = halves[view], center = centers[view];
        std::vector<float> depth((size_t)size * size,
                                  -std::numeric_limits<float>::infinity());
        auto raster = [&](const Part& part) {
            const auto& vertices = part.vertices;
            const auto& tex = part.texture;
            for (size_t t = 0; t + 23 < vertices.size(); t += 24) {
                float x[3], y[3], z[3];
                for (int k = 0; k < 3; ++k) {
                    const float* v = &vertices[t + k * 8];
                    x[k] = ((v[0]*cr - v[2]*sr - center) / half * .5f + .5f) * size;
                    y[k] = (.5f - (v[1] - cy) / hh * .5f) * size;
                    z[k] = v[0]*sr + v[2]*cr;
                }
                const float det = (y[1]-y[2])*(x[0]-x[2]) +
                                  (x[2]-x[1])*(y[0]-y[2]);
                if (std::fabs(det) < 1e-8f) continue;
                const int xmin = std::max(0, (int)std::floor(std::min({x[0],x[1],x[2]})));
                const int xmax = std::min(size-1, (int)std::ceil(std::max({x[0],x[1],x[2]})));
                const int ymin = std::max(0, (int)std::floor(std::min({y[0],y[1],y[2]})));
                const int ymax = std::min(size-1, (int)std::ceil(std::max({y[0],y[1],y[2]})));
                for (int py = ymin; py <= ymax; ++py)
                for (int px = xmin; px <= xmax; ++px) {
                    const float a = ((y[1]-y[2])*(px+.5f-x[2]) + (x[2]-x[1])*(py+.5f-y[2])) / det;
                    const float b = ((y[2]-y[0])*(px+.5f-x[2]) + (x[0]-x[2])*(py+.5f-y[2])) / det;
                    const float c = 1-a-b;
                    if (a < 0 || b < 0 || c < 0) continue;
                    const float d = a*z[0] + b*z[1] + c*z[2];
                    const size_t di = (size_t)py*size+px;
                    if (d <= depth[di]) continue;
                    const float* v = &vertices[t];
                    const float u = a*v[6] + b*v[14] + c*v[22];
                    const float w = a*v[7] + b*v[15] + c*v[23];
                    const int tx = std::clamp((int)((u-std::floor(u))*tex.w), 0, tex.w-1);
                    const int ty = std::clamp((int)((w-std::floor(w))*tex.h), 0, tex.h-1);
                    const unsigned char* sample = &tex.rgba[((size_t)ty*tex.w+tx)*4];
                    if (sample[3] < 128) continue; // transparent leaves do not occlude bark
                    depth[di] = d;
                    unsigned char* dst = &pixels[((size_t)(py+(view/4)*size)*width+(view%4)*size+px)*4];
                    for (int ch = 0; ch < 3; ++ch) dst[ch] = (unsigned char)std::clamp(sample[ch]*part.kd[ch], 0.0f, 255.0f);
                    dst[3] = 255;
                }
            }
        };
        for (const auto& part : parts) raster(part);
    }
    }
    // Dilate RGB only, independently in each capture. Alpha stays binary and
    // bilinear samples at silhouettes cannot pick up a black fringe.
    if (!captured) {
    std::vector<unsigned char> filled((size_t)width*height);
    for (size_t i = 0; i < filled.size(); ++i) filled[i] = pixels[i*4+3] != 0;
    for (int pass = 0; pass < 4; ++pass) {
        auto next = pixels;
        auto mask = filled;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const size_t i = (size_t)y*width+x;
            if (filled[i]) continue;
            int sum[3] = {}, count = 0;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x+dx, ny = y+dy;
                if (nx < 0 || nx >= width || ny < 0 || ny >= height || nx/size != x/size || ny/size != y/size) continue;
                const size_t j = (size_t)ny*width+nx;
                if (!filled[j]) continue;
                for (int k = 0; k < 3; ++k) sum[k] += pixels[j*4+k];
                ++count;
            }
            if (count) {
                for (int k = 0; k < 3; ++k) next[i*4+k] = (unsigned char)(sum[k]/count);
                mask[i] = 1;
            }
        }
        pixels.swap(next);
        filled.swap(mask);
    }
    }
    namespace fs = std::filesystem;
    const fs::path rel(outputStem);
    if (rel.is_absolute() || rel.empty() || outputStem.find("..") != std::string::npos)
        return fail("Output must be a project-relative asset path");
    const fs::path dir = (fs::path(projectDir)/rel).parent_path();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return fail(ec.message());
    const std::string stem = rel.filename().string();
    if (!stbi_write_png((dir/(stem+".png")).string().c_str(), width, height, 4,
                         pixels.data(), width*4)) return fail("Cannot write impostor PNG");
    std::ofstream mtl(dir/(stem+".mtl")), obj(dir/(stem+".obj"));
    if (!mtl || !obj) return fail("Cannot write impostor model");
    obj << "# TyraX cylindrical impostor: select one part\nmtllib " << stem << ".mtl\n";
    obj << "# backend " << (captured ? "GPU" : "CPU") << " views " << views << " size " << size << '\n';
    // All cards live in XY. Runtime selects one part and rotates its X axis
    // towards the camera; each view keeps its own projected bounds and origin.
    for (int view = 0; view < views; ++view) {
        mtl << "newmtl view" << view << "\nKd 1 1 1\nmap_Kd " << stem << ".png\n";
        obj << "usemtl view" << view << '\n';
        for (int k = 0; k < 4; ++k) {
            const float x = centers[view] + ((k == 0 || k == 3) ? -halves[view] : halves[view]);
            obj << "v " << x << ' ' << cy + (k < 2 ? -hh : hh) << " 0\n";
        }
        const float u0 = (view%4)*.25f, u1 = u0+.25f;
        const float row = 4.0f/views;
        const float v1 = 1.0f-(view/4)*row, v0 = v1-row;
        obj << "vt " << u0 << ' ' << v0 << "\nvt " << u1 << ' ' << v0
            << "\nvt " << u1 << ' ' << v1 << "\nvt " << u0 << ' ' << v1 << '\n';
        const int b = view*4+1;
        for (const auto& tri : {std::initializer_list<int>{0,1,2}, {0,2,3}}) {
            obj << "f";
            for (int k : tri) obj << ' ' << b+k << '/' << b+k;
            obj << '\n';
        }
    }
    obj.flush(); mtl.flush();
    if (!obj || !mtl) return fail("Cannot finish impostor model");
    if (outObjRel) *outObjRel = outputStem + ".obj";
    return true;
}
} // namespace impostorbake

namespace treegen {
bool writeImpostor(const std::string& projectDir, const std::string& name,
                   const Mesh& mesh, const Image& bark, const Image& leaf,
                   std::string* outObjRel, std::string* outError, int size, int views, bool gpu, std::string* report) {
    std::vector<impostorbake::Part> parts;
    if (!mesh.bark.empty()) parts.push_back({mesh.bark, bark});
    if (!mesh.leaves.empty()) parts.push_back({mesh.leaves, leaf});
    return impostorbake::write(projectDir, "res/models/trees/"+name+"-impostor",
        parts, mesh.min, mesh.max, outObjRel, outError, size, views, gpu, report);
}
} // namespace treegen
