#include "impostorbake.hpp"
#include "objparser.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stb_image.h>

namespace impostorbake {
bool model(const std::string& projectDir, const std::string& modelRel,
           const std::string& materialRel, const std::string& outputStem,
           std::string* outObjRel, float* outExtent, std::string* error, int size, int views, bool gpu, std::string* report) {
    namespace fs = std::filesystem;
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    const fs::path source = fs::path(projectDir)/modelRel;
    const fs::path material = fs::path(projectDir)/materialRel;
    if (source.extension() != ".obj" && source.extension() != ".OBJ")
        return fail("Select a static OBJ model");
    if (!materialRel.empty()) {
        std::vector<objparser::MtlMaterial> materials;
        if (!objparser::loadMtl(material.string(), materials))
            return fail("Cannot read material override: " + materialRel);
    }
    objparser::Model input;
    if (!objparser::load(source.string(), input,
                         materialRel.empty() ? "" : material.string()))
        return fail("Cannot read model: " + modelRel);
    // Matches the viewport and static-model bake's texture resolution rule.
    const fs::path textureDir = (materialRel.empty() ? source : material).parent_path();
    std::vector<Part> parts;
    for (const auto& sub : input.submeshes) {
        if (!sub.refl.empty() || sub.ke[0] > 0 || sub.ke[1] > 0 || sub.ke[2] > 0)
            return fail("Material '" + sub.material + "' uses reflection or emission; albedo impostors cannot preserve it");
        Part part;
        part.vertices = sub.verts;
        std::copy(sub.kd, sub.kd+3, part.kd);
        if (sub.texture.empty()) {
            part.texture = {1, 1, {255,255,255,255}};
        } else {
            const auto path = textureDir/sub.texture;
            int channels = 0;
            unsigned char* pixels = stbi_load(path.string().c_str(),
                &part.texture.w, &part.texture.h, &channels, 4);
            if (!pixels) return fail("Cannot read texture: " + path.generic_string());
            part.texture.rgba.assign(pixels, pixels+(size_t)part.texture.w*part.texture.h*4);
            stbi_image_free(pixels);
        }
        parts.push_back(std::move(part));
    }
    if (!write(projectDir, outputStem, parts, input.min, input.max, outObjRel, error, size, views, gpu, report))
        return false;
    if (outExtent) *outExtent = std::max({input.max[0]-input.min[0],
        input.max[1]-input.min[1], input.max[2]-input.min[2]});
    return true;
}
} // namespace impostorbake
