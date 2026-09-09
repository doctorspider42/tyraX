// Rebuild the authored tree assets from the repository's own generator.
// From repository root (MinGW g++ or Linux g++):
// g++ -std=c++20 -O2 -Isrc -Ivendor/stb examples/impostor-grove/authoring/generate_assets.cpp src/treegen.cpp src/treeimpostor.cpp src/modelimpostor.cpp src/objparser.cpp -o <temporary-executable>
// <temporary-executable> examples/impostor-grove
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include "treegen.hpp"
#include "impostorbake.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const char* names[] = {"silver-birch", "old-oak", "young-oak", "fern-bush"};
    const int presets[] = {1, 0, 0, 5};
    for (int i = 0; i < 4; ++i) {
        auto p = treegen::presets()[presets[i]].params;
        p.seed = 4107 + i * 811;
        p.leafCount = i == 3 ? 210 : 340;
        p.leafSize = i == 3 ? .19f : .16f;
        p.height = i == 0 ? 9.0f : i == 1 ? 10.0f : i == 2 ? 6.5f : 1.6f;
        p.leafColor[0] = .36f; p.leafColor[1] = .51f; p.leafColor[2] = .18f;
        p.leafColor2[0] = .16f; p.leafColor2[1] = .29f; p.leafColor2[2] = .10f;
        const auto mesh = treegen::generate(p);
        const auto bark = treegen::bakeBarkTexture(p);
        const auto leaf = treegen::bakeLeafTexture(p);
        std::string nearPath, farPath, error;
        if (!treegen::writeAssets(argv[1], names[i], p, mesh, bark, leaf, &nearPath, &error) ||
            !impostorbake::model(argv[1], nearPath, "",
                std::string("res/models/trees/")+names[i]+"-impostor",
                &farPath, nullptr, &error, 128, i == 3 ? 4 : 8)) {
            std::cerr << error << '\n'; return 1;
        }
        std::cout << names[i] << ": " << mesh.triangles() << " -> 2 visible triangles, " << (i == 3 ? 4 : 8) << " views\n";
    }
    std::string farPath, error;
    if (!impostorbake::model(argv[1], "res/models/waystone.obj", "",
            "res/models/impostors/waystone", &farPath, nullptr, &error, 128, 16)) {
        std::cerr << error << '\n'; return 1;
    }
    std::cout << "waystone: 36 -> 2 visible triangles, 16 views\n";

}
