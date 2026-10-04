#pragma once

#include <cstddef>
#include <cstdint>

// The Character Generator's kit (resources/chargen-kit.bin), linked into the
// editor binary by chargen_kit.cpp. See tools/chargen-kit/build_kit.py for the
// layout and how it is made.
namespace chargenkit {
const uint8_t* data();
size_t size();
}  // namespace chargenkit
