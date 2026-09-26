#pragma once
namespace Vehicle_playground {
constexpr int DISTRICT_NIGHT_VALUE = 0;
// Stable object-id hashes, not scene-row indices. Adding/removing an unrelated
// object must not turn the day/night switch into a visibility toggle for it.
// project::liveLinkIdHash (FNV-1a 64 of the object id PLUS the 0xFF string
// terminator) - the recipe SCENE_*_OBJECT_ID_HASHES is generated with. Both
// scenes' dressing is listed: the script matches against the current scene.
constexpr unsigned long long DISTRICT_NIGHT_OBJECTS[] = {
    // main
    0xebc2b556a5b453c4ULL, 0x65bd7f8ad89b377cULL,
    0x1172b318637275fbULL, 0x217a232b39f12bc6ULL,
    0xa5f7e1bc7c0fa3cdULL, 0x571b6780d8f997e0ULL,
    0x74af3251bec68ad0ULL, 0x3aad4e1affcecf3eULL,
    0x5ba2db2639ebb6c8ULL, 0x995c9d669f16c030ULL,
    0x1f625af6a4c08a22ULL,
    // dense
    0x2cce91b6fda37b93ULL, 0xc3f3c1d9de52e277ULL,
    0xd1350485d9dc2d78ULL, 0x62b90247b88556cdULL,
    0xe6bacc8ae09b7b4eULL, 0xf925082b6a10e6bbULL,
    0xa0f4cb36f693718bULL, 0x6e934ba11af3c735ULL,
    0x9c14ab51dd9293d7ULL, 0x576390ed8547ae97ULL,
    0x3ec488d5e27645ddULL};
}
