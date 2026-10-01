#pragma once

#include <string>
#include <vector>

namespace occlusionbake {

struct Box {
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
};

struct Result {
    std::vector<Box> boxes;
    std::string reason;
    int inputTriangles = 0;
};

// Builds an UNDER-approximation of a static OBJ. The source must be
// geometrically closed (T-junctions are allowed when parity still proves the
// volume) and every referenced texture must be fully opaque. A sampled interior
// grid is eroded by one cell, then merged into boxes; refusing an uncertain
// model is intentional because a false occluder deletes pixels.
Result build(const std::string& modelPath,
             const std::string& materialOverride = "");

}  // namespace occlusionbake
