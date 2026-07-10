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

// Built-in assets for the "FPP showcase" template.
const char* houseObjText();
const unsigned char* crosshairPng(size_t& size);

// Built-in "USE" prompt sprite, shipped into every project (res/hud/use.png).
const unsigned char* usePromptPng(size_t& size);

// Built-in "LOADING..." sprite (res/hud/loading.png), shown centered on
// black during scene switches when the loading screen is enabled.
const unsigned char* loadingPng(size_t& size);

// 8x8 glyph strip for the debug-profile HUD (res/hud/debugfont.png),
// written when the debug profile enables an overlay. Glyph order
// "0123456789.FPSMBE", one glyph per 16px cell (right half transparent).
const std::vector<unsigned char>& debugFontPng();

// Built-in save-menu sprites (res/hud/save-*.png), written when missing.
// The engine has no text rendering - the menu text is baked into these.
struct BuiltinAsset {
    const char* fileName;  // name inside res/hud/
    const unsigned char* data;
    size_t size;
};
const std::vector<BuiltinAsset>& saveMenuAssets();

// Content of a new user script created from the "New script..." action.
std::string scriptStub(const Project& p, const std::string& className,
                       const std::string& fileName);

// True when `content` is byte-identical to what an older editor version
// generated for this file - i.e. the user never edited it and it is safe
// to regenerate even though it predates the ownership marker.
bool matchesLegacy(const Project& p, const std::string& relativePath,
                   const std::string& content);

}  // namespace templates
