#pragma once
#include <string>

namespace hardware_timeline {
// Host-only view of the boot capture; never polls or commands the running PS2.
// `scale` = the editor's UI scale (App::scaled), for the literal sizes.
void draw(const std::string& projectDir, float scale);
}
