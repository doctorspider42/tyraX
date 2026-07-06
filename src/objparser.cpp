#include "objparser.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace objparser {

bool load(const std::string& path, std::vector<float>& out) {
    std::ifstream f(path);
    if (!f) return false;

    std::vector<float> positions;  // x,y,z triples
    out.clear();

    auto vertexAt = [&](int objIndex, float* xyz) {
        // obj indices are 1-based; negative = relative to the end
        const int count = (int)positions.size() / 3;
        int i = objIndex > 0 ? objIndex - 1 : count + objIndex;
        if (i < 0 || i >= count) return false;
        xyz[0] = positions[i * 3];
        xyz[1] = positions[i * 3 + 1];
        xyz[2] = positions[i * 3 + 2];
        return true;
    };

    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 2) continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "v") {
            float x = 0, y = 0, z = 0;
            ss >> x >> y >> z;
            positions.push_back(x);
            positions.push_back(y);
            positions.push_back(z);
        } else if (tag == "f") {
            // face vertex indices ("7", "7/1", "7//2", "7/1/2", negatives)
            std::vector<int> idx;
            std::string part;
            while (ss >> part) idx.push_back(std::atoi(part.c_str()));

            for (size_t k = 2; k < idx.size(); ++k) {
                float a[3], b[3], c[3];
                if (!vertexAt(idx[0], a) || !vertexAt(idx[k - 1], b) ||
                    !vertexAt(idx[k], c))
                    continue;

                // face normal
                const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
                const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
                float nx = uy * vz - uz * vy;
                float ny = uz * vx - ux * vz;
                float nz = ux * vy - uy * vx;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-8f) nx /= len, ny /= len, nz /= len;
                else nx = 0, ny = 1, nz = 0;

                for (const float* p : {a, b, c}) {
                    out.push_back(p[0]);
                    out.push_back(p[1]);
                    out.push_back(p[2]);
                    out.push_back(nx);
                    out.push_back(ny);
                    out.push_back(nz);
                }
            }
        }
    }
    return !out.empty();
}

}  // namespace objparser
