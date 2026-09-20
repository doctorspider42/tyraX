#pragma once
namespace Vehicle_playground {
constexpr int DISTRICT_NIGHT_VALUE = 0;
// Stable object-id hashes, not scene-row indices. Adding/removing an unrelated
// object must not turn the day/night switch into a visibility toggle for it.
constexpr unsigned long long DISTRICT_NIGHT_OBJECTS[] = {
    0xc249f3bed64feaa5ULL, 0xce5a0c0b42cf0a1dULL,
    0x9da5353a192abc20ULL, 0xe883239acd94d66fULL,
    0xe32b0225405463c6ULL, 0x604f5265f7af1be9ULL,
    0xbe3629961b504285ULL, 0x8b6b4afe10c95e9bULL,
    0x3b3513533bc14745ULL, 0x97f4b4ce9d38a929ULL,
    0x9ead8a7ce3cf4ca7ULL};
}
