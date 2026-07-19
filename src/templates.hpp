#pragma once

#include <string>
#include <vector>

struct Project;

namespace templates {

struct File {
    std::string relativePath;  // path inside project dir, '\\' separated
    std::string content;
};

// All files generated for a new Tyra game project (sources, Makefile,
// docker-compose, run scripts...) with project values substituted.
std::vector<File> generate(const Project& p);

// Compiles the project flow graph into a C++ script (flow_graph.gen.cpp).
std::string flowGraphScript(const Project& p);

// Bakes every animated model (.glb) referenced by the project into the
// .tanm morph-frame binaries + extracted PNG textures the game loads
// (res/models/<stem>.tanm, res/models/<stem>_<image>.png). Bake failures
// are reported in `warnings` and the model is skipped (the game warns and
// renders nothing for it - same soft-fail as missing textures).
std::vector<File> bakeAnimAssets(const Project& p,
                                 std::vector<std::string>* warnings = nullptr);

// Built-in assets for the "FPP showcase" template.
const char* houseObjText();
const unsigned char* crosshairPng(size_t& size);

// Built-in "USE" prompt sprite, shipped into every project (res/hud/use.png).
const unsigned char* usePromptPng(size_t& size);

// Built-in "LOADING..." sprite (res/hud/loading.png), shown centered on
// black during scene switches when the loading screen is enabled.
const unsigned char* loadingPng(size_t& size);

// 8x8 glyph strip for in-game HUD text (res/hud/debugfont.png): the
// debug-profile overlays and the video-mode confirm prompt. Written on
// every generated-file refresh. 42 glyphs (digits, letters, symbols) in
// two rows of 32 cells of 16px each (right half of a cell transparent);
// the order must match drawHudText's atlas in the game template.
const std::vector<unsigned char>& debugFontPng();

// Built-in save-menu sprites (res/hud/save-*.png), written when missing.
// The engine has no text rendering - the menu text is baked into these.
struct BuiltinAsset {
    const char* fileName;  // name inside res/hud/
    const unsigned char* data;
    size_t size;
};
const std::vector<BuiltinAsset>& saveMenuAssets();

// True when any scene can show the sun lens flare (an authored per-scene
// amount > 0, or a Set Flare flow node that could raise it at runtime).
// Gates both the res/hud/flare-*.png bake (refreshGenerated) and the
// game-side texture load (FLARE_USED in scene_data.hpp) - keep them equal.
bool projectUsesFlare(const Project& p);

// True when any scene has a Point Light with a visible beam (corona/cone).
// Gates the res/hud/flare-corona.png bake and BEAMS_USED in scene_data.hpp.
bool projectUsesBeams(const Project& p);

// Content of a new user script created from the "New script..." action.
std::string scriptStub(const Project& p, const std::string& className,
                       const std::string& fileName);

// True when `content` is byte-identical to what an older editor version
// generated for this file - i.e. the user never edited it and it is safe
// to regenerate even though it predates the ownership marker.
bool matchesLegacy(const Project& p, const std::string& relativePath,
                   const std::string& content);

}  // namespace templates
