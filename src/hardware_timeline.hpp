#pragma once
#include <string>

namespace hardware_timeline {
// Host-only view of the boot capture; never polls or commands the running PS2.
void draw(const std::string& projectDir);
}
