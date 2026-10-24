// Host authoring tool. Link src/treegen.cpp + src/treeimpostor.cpp, -Isrc -Ivendor/stb.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include "treegen.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <array>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const std::filesystem::path root(argv[1]);
    for (int i = 0; i < 3; ++i) {
        auto p = treegen::presets()[6].params;
        p.seed = 1234 + i * 997;
        p.height = 5.5f;
        const std::string name = "forest-oak-" + std::string(1, 'a' + i);
        auto mesh = treegen::generate(p);
        auto bark = treegen::bakeBarkTexture(p), leaf = treegen::bakeLeafTexture(p);
        std::string rel, error;
        if (!treegen::writeAssets(root.string(), name, p, mesh, bark, leaf, &rel, &error) ||
            !treegen::writeImpostor(root.string(), name, mesh, bark, leaf, &rel, &error, 128, 4)) {
            std::cerr << error << '\n'; return 2;
        }
        const auto dir = root / "res/models/trees";
        std::ifstream source(dir / (name + "-impostor.obj"));
        std::vector<std::array<float, 3>> positions;
        std::vector<std::array<float, 2>> uvs;
        std::string line;
        while (std::getline(source, line)) {
            std::istringstream s(line); std::string tag; s >> tag;
            if (tag == "v") { std::array<float, 3> v; s >> v[0] >> v[1] >> v[2]; positions.push_back(v); }
            if (tag == "vt") { std::array<float, 2> v; s >> v[0] >> v[1]; uvs.push_back(v); }
        }
        if (positions.size() != 16 || uvs.size() != 16) return 3;
        // An instanced LOD is an ordinary mesh, not a camera-facing impostor:
        // two perpendicular captures, one material, four triangles total.
        std::ofstream obj(dir / (name + "-far.obj")), mtl(dir / (name + "-far.mtl"));
        obj << "# Crossed broadleaf crown, four triangles, no billboard dependency\nmtllib "
            << name << "-far.mtl\nusemtl crown\n";
        for (int n = 0; n < 8; ++n) {
            auto v = positions[n];
            obj << "v " << (n < 4 ? v[0] : 0) << ' ' << v[1] << ' ' << (n < 4 ? 0 : -v[0]) << '\n';
        }
        for (int n = 0; n < 8; ++n) obj << "vt " << uvs[n][0] << ' ' << uvs[n][1] << '\n';
        obj << "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\nf 5/5 6/6 7/7\nf 5/5 7/7 8/8\n";
        mtl << "newmtl crown\nKa 1 1 1\nKd 1 1 1\nKs 0 0 0\nd 1\nillum 1\nmap_Kd " << name << "-impostor.png\n";
        if (!obj || !mtl) return 4;
        std::cout << name << " seed=" << p.seed << " near=" << mesh.triangles() << " far=4\n";
    }
}
