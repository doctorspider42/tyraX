#include "skytex.hpp"

#include <algorithm>

#include <stb_image.h>  // implementation lives in app.cpp

namespace skytex {

std::vector<unsigned char> crop(const std::string& absPath, std::string& err) {
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load(absPath.c_str(), &w, &h, &comp, 4);
    if (!px) {
        err = "cannot read the image";
        return {};
    }
    if (w < kWidth / 4 || h < kHeight / 4) {
        stbi_image_free(px);
        err = "the image is too small";
        return {};
    }
    // An AREA average, not a bilinear tap: a 2K panorama shrinks eight times
    // here, and a tap per texel turns cloud edges into sparkle.
    const float srcH = (float)h * kVMax;
    std::vector<unsigned char> out((size_t)kWidth * kHeight * 4, 255);
    for (int y = 0; y < kHeight; ++y) {
        const int y0 = std::min(h - 1, (int)(y * srcH / kHeight));
        const int y1 = std::max(y0 + 1, std::min(h, (int)((y + 1) * srcH / kHeight)));
        for (int x = 0; x < kWidth; ++x) {
            const int x0 = x * w / kWidth;
            const int x1 = std::max(x0 + 1, (x + 1) * w / kWidth);
            unsigned long acc[3] = {0, 0, 0};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    const unsigned char* s = px + ((size_t)sy * w + sx) * 4;
                    for (int c = 0; c < 3; ++c) acc[c] += s[c];
                }
            const unsigned long n = (unsigned long)(y1 - y0) * (x1 - x0);
            unsigned char* d = &out[((size_t)y * kWidth + x) * 4];
            for (int c = 0; c < 3; ++c) d[c] = (unsigned char)((acc[c] + n / 2) / n);
            // Alpha stays 255: StaPip discards alpha-0 texels, and a panorama
            // with a transparent sky would punch holes in the dome.
        }
    }
    stbi_image_free(px);
    return out;
}

bool usable(const std::string& absPath) {
    int w = 0, h = 0, comp = 0;
    return stbi_info(absPath.c_str(), &w, &h, &comp) != 0 && w >= kWidth / 4 &&
           h >= kHeight / 4;
}

}  // namespace skytex
