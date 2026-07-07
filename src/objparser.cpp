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
    std::vector<float> texcoords;  // u,v pairs
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
    auto uvAt = [&](int objIndex, float* uv) {
        const int count = (int)texcoords.size() / 2;
        int i = objIndex > 0 ? objIndex - 1 : count + objIndex;
        if (objIndex == 0 || i < 0 || i >= count) {
            uv[0] = uv[1] = 0.0f;
            return;
        }
        uv[0] = texcoords[i * 2];
        uv[1] = 1.0f - texcoords[i * 2 + 1];  // flip to image space (like Tyra)
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
        } else if (tag == "vt") {
            float u = 0, v = 0;
            ss >> u >> v;
            texcoords.push_back(u);
            texcoords.push_back(v);
        } else if (tag == "f") {
            // face vertex tokens: "7", "7/1", "7//2", "7/1/2", negatives
            std::vector<int> vIdx, tIdx;
            std::string part;
            while (ss >> part) {
                vIdx.push_back(std::atoi(part.c_str()));
                const size_t slash = part.find('/');
                int t = 0;
                if (slash != std::string::npos && slash + 1 < part.size() &&
                    part[slash + 1] != '/')
                    t = std::atoi(part.c_str() + slash + 1);
                tIdx.push_back(t);
            }

            for (size_t k = 2; k < vIdx.size(); ++k) {
                float a[3], b[3], c[3];
                if (!vertexAt(vIdx[0], a) || !vertexAt(vIdx[k - 1], b) ||
                    !vertexAt(vIdx[k], c))
                    continue;
                float uva[2], uvb[2], uvc[2];
                uvAt(tIdx[0], uva);
                uvAt(tIdx[k - 1], uvb);
                uvAt(tIdx[k], uvc);

                // face normal
                const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
                const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
                float nx = uy * vz - uz * vy;
                float ny = uz * vx - ux * vz;
                float nz = ux * vy - uy * vx;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-8f) nx /= len, ny /= len, nz /= len;
                else nx = 0, ny = 1, nz = 0;

                const float* pts[3] = {a, b, c};
                const float* uvs[3] = {uva, uvb, uvc};
                for (int i = 0; i < 3; ++i) {
                    out.push_back(pts[i][0]);
                    out.push_back(pts[i][1]);
                    out.push_back(pts[i][2]);
                    out.push_back(nx);
                    out.push_back(ny);
                    out.push_back(nz);
                    out.push_back(uvs[i][0]);
                    out.push_back(uvs[i][1]);
                }
            }
        }
    }
    return !out.empty();
}

}  // namespace objparser
