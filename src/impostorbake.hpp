#pragma once
#include "treegen.hpp"

namespace impostorbake {
// Image-space UVs and source albedo; object tint remains a runtime multiplier.
struct Part {
    std::vector<float> vertices;
    treegen::Image texture;
    float kd[3] = {1, 1, 1};
};
// GPU adapter returns captures with four passes of RGB-only dilation.
// Optional GPU adapter: CPU-only authoring tools do not need GLFW or OpenGL.
using GpuCapture = bool (*)(const std::vector<Part>&, int, int, float, float,
                           const float*, const float*, std::vector<unsigned char>&, std::string&);
void setGpuCapture(GpuCapture capture);
bool write(const std::string& projectDir, const std::string& outputStem,
           const std::vector<Part>& parts, const float* min, const float* max,
           std::string* outObjRel, std::string* error, int size = 128, int views = 8, bool gpu = false,
           std::string* report = nullptr);

// Static OBJ adapter. outputStem is project-relative, without an extension.
// Missing textures and unsupported reflective/emissive materials fail before writing.
bool model(const std::string& projectDir, const std::string& modelRel,
           const std::string& materialRel, const std::string& outputStem,
           std::string* outObjRel, float* outExtent, std::string* error,
           int size = 128, int views = 8, bool gpu = false,
           std::string* report = nullptr);
} // namespace impostorbake
