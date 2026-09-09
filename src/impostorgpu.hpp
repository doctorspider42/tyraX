#pragma once
#include "impostorbake.hpp"
namespace impostorgpu {
// Main-thread, offscreen albedo capture. False leaves the CPU fallback in charge.
bool capture(const std::vector<impostorbake::Part>& parts, int size, int views,
             float cy, float hh, const float* centers, const float* halves,
             std::vector<unsigned char>& pixels, std::string& error);
}
