#pragma once

// --vehicle-check: the drive model's property tests (see vehcheck.cpp).
// Returns a process exit code: 0 = every property holds.
namespace vehcheck {
int run();
}
